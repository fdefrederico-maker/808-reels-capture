import Foundation
import CoreGraphics
import CoreMedia
import CoreVideo
import ScreenCaptureKit

// Capta a janela principal do FL Studio com o ScreenCaptureKit.
// Guarda sempre a imagem mais recente em `latestFrame` (acedido só na fila do motor).
final class WindowCapture: NSObject, SCStreamOutput, SCStreamDelegate {
    enum Problem: Equatable {
        case none
        case noPermission
        case noWindow
        case failed(String)
    }

    /// Altura (em píxeis) a que a janela é captada: igual à altura do vídeo final.
    static let captureHeight: CGFloat = 1920

    private let queue: DispatchQueue
    private var stream: SCStream?
    private var windowID: CGWindowID?
    private var capturedSize: CGSize = .zero
    private var refreshInFlight = false

    private(set) var latestFrame: CVPixelBuffer?
    /// Chamado (na fila do motor) para cada imagem nova, com a hora em que foi mostrada no ecrã.
    var onFrame: ((CVPixelBuffer, UInt64) -> Void)?
    private(set) var problem: Problem = .none
    private(set) var windowTitle: String = ""

    var isCapturing: Bool { stream != nil && latestFrame != nil }

    init(queue: DispatchQueue) {
        self.queue = queue
        super.init()
    }

    /// Procura a janela do FL Studio e (re)inicia a captura se necessário.
    /// Chamado a cada ~2 s na fila do motor.
    func refresh() {
        guard !refreshInFlight else { return }
        refreshInFlight = true

        Task {
            do {
                let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
                self.queue.async {
                    self.refreshInFlight = false
                    self.apply(content)
                }
            } catch {
                self.queue.async {
                    self.refreshInFlight = false
                    self.stopStream()
                    if let scError = error as? SCStreamError, scError.code == .userDeclined {
                        self.problem = .noPermission
                    } else if !CGPreflightScreenCaptureAccess() {
                        self.problem = .noPermission
                    } else {
                        self.problem = .failed(error.localizedDescription)
                    }
                }
            }
        }
    }

    private static func isFLStudio(_ window: SCWindow) -> Bool {
        guard let app = window.owningApplication else { return false }
        let name = app.applicationName.lowercased()
        let bundle = app.bundleIdentifier.lowercased()
        return name.contains("fl studio") || name.hasPrefix("fl64") || bundle.contains("image-line") || bundle.contains("flstudio")
    }

    private func apply(_ content: SCShareableContent) {
        let candidates = content.windows.filter { w in
            WindowCapture.isFLStudio(w) && w.windowLayer == 0 && w.frame.width >= 400 && w.frame.height >= 300
        }

        guard let window = candidates.max(by: { $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height }) else {
            stopStream()
            problem = .noWindow
            return
        }

        windowTitle = window.title ?? "FL Studio"
        let config = WindowCapture.makeConfiguration(for: window.frame.size)
        let newSize = CGSize(width: config.width, height: config.height)

        if let stream, windowID == window.windowID {
            // Mesma janela: só atualiza se o tamanho mudou
            if abs(newSize.width - capturedSize.width) > 2 {
                capturedSize = newSize
                stream.updateConfiguration(config) { error in
                    if let error { NSLog("updateConfiguration: \(error)") }
                }
            }
            return
        }

        stopStream()

        let filter = SCContentFilter(desktopIndependentWindow: window)
        let newStream = SCStream(filter: filter, configuration: config, delegate: self)

        do {
            try newStream.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
        } catch {
            problem = .failed(error.localizedDescription)
            return
        }

        stream = newStream
        windowID = window.windowID
        capturedSize = newSize
        problem = .none

        newStream.startCapture { [weak self] error in
            guard let self, let error else { return }
            self.queue.async {
                if self.stream === newStream {
                    self.stream = nil
                    self.windowID = nil
                    self.latestFrame = nil
                    self.problem = CGPreflightScreenCaptureAccess() ? .failed(error.localizedDescription) : .noPermission
                }
            }
        }
    }

    private static func makeConfiguration(for windowSize: CGSize) -> SCStreamConfiguration {
        let config = SCStreamConfiguration()
        let scale = captureHeight / max(windowSize.height, 1)
        var width = Int((windowSize.width * scale).rounded())
        width -= width % 2
        config.width = max(width, 2)
        config.height = Int(captureHeight)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 30)
        config.pixelFormat = kCVPixelFormatType_32BGRA
        config.colorSpaceName = CGColorSpace.sRGB
        config.queueDepth = 6
        config.showsCursor = true
        return config
    }

    private func stopStream() {
        if let stream {
            stream.stopCapture { _ in }
        }
        stream = nil
        windowID = nil
        latestFrame = nil
    }

    // MARK: - SCStreamOutput (chamado na fila do motor)

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, stream === self.stream, CMSampleBufferIsValid(sampleBuffer) else { return }

        guard let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let info = attachments.first,
              let statusRaw = info[.status] as? Int,
              let status = SCFrameStatus(rawValue: statusRaw),
              status == .complete,
              let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer)
        else { return }

        latestFrame = pixelBuffer

        let displayTicks = (info[.displayTime] as? UInt64) ?? HostClock.now()
        onFrame?(pixelBuffer, displayTicks)
    }

    // MARK: - SCStreamDelegate

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        queue.async {
            guard stream === self.stream else { return }
            self.stream = nil
            self.windowID = nil
            self.latestFrame = nil
            self.problem = .failed(error.localizedDescription)
        }
    }
}
