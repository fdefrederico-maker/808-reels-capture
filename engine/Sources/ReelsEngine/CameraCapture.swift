import Foundation
import AVFoundation
import CoreVideo
import Vision

/// Imagem da câmara + máscara da pessoa (branco = pessoa), se pedida.
struct CameraFrame {
    let image: CVPixelBuffer
    let mask: CVPixelBuffer?
}

// Câmara (AVFoundation) com recorte da pessoa (Vision) para trocar o fundo.
// Corre na sua própria fila; o motor lê `latestFrame` quando precisa.
final class CameraCapture: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    enum Problem: Equatable {
        case none
        case noPermission
        case noCamera
        case failed(String)
    }

    private let session = AVCaptureSession()
    private let videoQueue = DispatchQueue(label: "808reels.camera", qos: .userInteractive)
    private let lock = NSLock()

    private var configured = false
    private var running = false
    private var latest: CameraFrame?
    private var _problem: Problem = .none
    private var _segmentation = false
    private var deviceName = ""

    private let segmentationRequest: VNGeneratePersonSegmentationRequest = {
        let request = VNGeneratePersonSegmentationRequest()
        request.qualityLevel = .balanced
        request.outputPixelFormat = kCVPixelFormatType_OneComponent8
        return request
    }()
    private let sequenceHandler = VNSequenceRequestHandler()

    var latestFrame: CameraFrame? {
        lock.lock(); defer { lock.unlock() }
        return latest
    }

    var problem: Problem {
        lock.lock(); defer { lock.unlock() }
        return _problem
    }

    /// Ligar só quando o fundo não é a sala original (poupa processador).
    var segmentationEnabled: Bool {
        get { lock.lock(); defer { lock.unlock() }; return _segmentation }
        set { lock.lock(); _segmentation = newValue; lock.unlock() }
    }

    private func setProblem(_ p: Problem) {
        lock.lock(); _problem = p; lock.unlock()
    }

    // MARK: - Ligar / desligar

    func start() {
        videoQueue.async {
            guard !self.running else { return }

            switch AVCaptureDevice.authorizationStatus(for: .video) {
            case .authorized:
                self.configureAndRun()
            case .notDetermined:
                AVCaptureDevice.requestAccess(for: .video) { granted in
                    self.videoQueue.async {
                        if granted { self.configureAndRun() } else { self.setProblem(.noPermission) }
                    }
                }
            default:
                self.setProblem(.noPermission)
            }
        }
    }

    func stop() {
        videoQueue.async {
            if self.running {
                self.session.stopRunning()
                self.running = false
            }
            self.lock.lock(); self.latest = nil; self.lock.unlock()
        }
    }

    private static func pickDevice() -> AVCaptureDevice? {
        // Preferir a câmara do próprio Mac; senão, qualquer câmara disponível
        var types: [AVCaptureDevice.DeviceType] = [.builtInWideAngleCamera]
        if #available(macOS 14.0, *) {
            types += [.external, .continuityCamera]
        } else {
            types += [.externalUnknown]
        }
        let discovery = AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: .unspecified)
        if let builtIn = discovery.devices.first(where: { $0.deviceType == .builtInWideAngleCamera }) {
            return builtIn
        }
        return discovery.devices.first ?? AVCaptureDevice.default(for: .video)
    }

    private func configureAndRun() {
        if !configured {
            guard let device = CameraCapture.pickDevice() else {
                setProblem(.noCamera)
                return
            }

            session.beginConfiguration()
            if session.canSetSessionPreset(.hd1280x720) {
                session.sessionPreset = .hd1280x720
            } else {
                session.sessionPreset = .high
            }

            do {
                let input = try AVCaptureDeviceInput(device: device)
                guard session.canAddInput(input) else { throw EngineError.message("câmara ocupada") }
                session.addInput(input)
            } catch {
                session.commitConfiguration()
                setProblem(.failed("Câmara: \(error.localizedDescription)"))
                return
            }

            let output = AVCaptureVideoDataOutput()
            output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
            output.alwaysDiscardsLateVideoFrames = true
            output.setSampleBufferDelegate(self, queue: videoQueue)
            guard session.canAddOutput(output) else {
                session.commitConfiguration()
                setProblem(.failed("Câmara: não foi possível ler as imagens."))
                return
            }
            session.addOutput(output)
            session.commitConfiguration()

            deviceName = device.localizedName
            configured = true
        }

        session.startRunning()
        running = session.isRunning
        setProblem(running ? .none : .failed("A câmara não arrancou (está a ser usada por outra app?)."))
    }

    // MARK: - Imagens da câmara (fila da câmara)

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        var mask: CVPixelBuffer?
        if segmentationEnabled {
            do {
                try sequenceHandler.perform([segmentationRequest], on: pixelBuffer, orientation: .up)
                mask = segmentationRequest.results?.first?.pixelBuffer
            } catch {
                mask = nil
            }
        }

        lock.lock()
        latest = CameraFrame(image: pixelBuffer, mask: mask)
        lock.unlock()
    }
}
