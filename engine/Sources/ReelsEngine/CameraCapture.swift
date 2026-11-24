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

    /// Chamado (na fila da câmara) para cada imagem, com a hora REAL em que foi captada.
    var onFrame: ((CameraFrame, UInt64) -> Void)?

    private var _latency: Double = 0
    private var _fps: Double = 0
    private var frameTimes: [UInt64] = []
    private var device: AVCaptureDevice?

    /// Imagens por segundo que a câmara está REALMENTE a entregar (com pouca luz desce para ~15).
    var measuredFPS: Double {
        lock.lock(); defer { lock.unlock() }
        return _fps
    }

    /// Atraso médio medido entre a captura e a chegada ao motor (segundos).
    var measuredLatency: Double {
        lock.lock(); defer { lock.unlock() }
        return _latency
    }

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
            self.device = device
            configured = true
        }

        session.startRunning()
        lockFrameRate()
        running = session.isRunning
        setProblem(running ? .none : .failed("A câmara não arrancou (está a ser usada por outra app?)."))
    }

    /// Obriga a câmara a 30 imagens/s. Sem isto, com pouca luz o macOS desce para ~15 fps
    /// (cada imagem fica o dobro do tempo exposta: movimento aos saltos e atrasado).
    /// A imagem fica mais escura, mas o Compositor compensa o brilho.
    private func lockFrameRate() {
        guard let device else { return }
        let target = CMTime(value: 1, timescale: 30)
        let supports30 = device.activeFormat.videoSupportedFrameRateRanges.contains {
            $0.minFrameRate <= 30.0 && $0.maxFrameRate >= 29.9
        }
        guard supports30 else { return }

        do {
            try device.lockForConfiguration()
            device.activeVideoMinFrameDuration = target
            device.activeVideoMaxFrameDuration = target
            device.unlockForConfiguration()
        } catch {
            NSLog("808 Reels Engine: não foi possível fixar 30 fps: \(error)")
        }
    }

    // MARK: - Imagens da câmara (fila da câmara)

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        // Hora de captura no relógio do sistema (o mesmo do áudio do plugin)
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let hostClock = CMClockGetHostTimeClock()
        let sourceClock = session.synchronizationClock ?? hostClock
        let hostTime = CMSyncConvertTime(pts, from: sourceClock, to: hostClock)
        let now = HostClock.now()
        var captureTicks = hostTime.isValid ? CMClockConvertHostTimeToSystemUnits(hostTime) : now
        if captureTicks > now || HostClock.seconds(from: captureTicks, to: now) > 2 {
            captureTicks = now   // carimbo inválido: usar a hora de chegada
        }

        var mask: CVPixelBuffer?
        if segmentationEnabled {
            do {
                try sequenceHandler.perform([segmentationRequest], on: pixelBuffer, orientation: .up)
                mask = segmentationRequest.results?.first?.pixelBuffer
            } catch {
                mask = nil
            }
        }

        let frame = CameraFrame(image: pixelBuffer, mask: mask)
        let latency = HostClock.seconds(from: captureTicks, to: HostClock.now())

        // Imagens por segundo reais (último segundo)
        frameTimes.append(captureTicks)
        while let first = frameTimes.first, HostClock.seconds(from: first, to: captureTicks) > 1.0 {
            frameTimes.removeFirst()
        }

        lock.lock()
        latest = frame
        _latency = _latency == 0 ? latency : _latency * 0.95 + latency * 0.05
        _fps = Double(frameTimes.count)
        lock.unlock()

        onFrame?(frame, captureTicks)
    }
}
