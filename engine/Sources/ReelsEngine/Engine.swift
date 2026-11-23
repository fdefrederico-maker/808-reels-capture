import Foundation
import AppKit
import CoreGraphics
import CoreImage

// Coordena tudo. Todo o estado é alterado apenas na fila `queue`.
final class Engine {
    static let port: UInt16 = 48808
    static let version = (Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String) ?? "dev"

    private enum State { case idle, recording, finishing, exporting }

    private struct Take {
        let videoURL: URL
        let wavURL: URL
        let outputURL: URL
        let audioOffset: Double
    }

    private let queue = DispatchQueue(label: "808reels.engine", qos: .userInteractive)
    private lazy var server = IPCServer(queue: queue)
    private lazy var capture = WindowCapture(queue: queue)
    private let camera = CameraCapture()
    private let compositor = Compositor()
    private let effects = Effects()
    private let engineStartTicks = HostClock.now()
    private var cameraRunning = false

    private var state: State = .idle
    private var recorder: VideoRecorder?
    private var pendingOutputURL: URL?
    private var lastTake: Take?

    private var frameTimer: DispatchSourceTimer?
    private var slowTimer: DispatchSourceTimer?
    private var tick: Int = 0
    private var lastClientTime = Date()
    private var lastProgressSent = Date.distantPast

    // MARK: - Arranque

    func start() {
        // Pede a permissão de Gravação de ecrã (o macOS só mostra o pedido uma vez)
        if !CGPreflightScreenCaptureAccess() {
            CGRequestScreenCaptureAccess()
        }

        queue.async { self.setUp() }
    }

    private func setUp() {
        server.onMessage = { [weak self] client, type, body in
            self?.handleMessage(from: client, type: type, body: body)
        }
        server.onClientsChanged = { [weak self] count in
            guard let self else { return }
            self.lastClientTime = Date()
            if count > 0 { self.capture.refresh() }
            self.updateCamera()
        }
        server.onFatalError = { message in
            // Normalmente: já existe outro motor a correr nesta porta
            NSLog("808 Reels Engine: \(message)")
            DispatchQueue.main.async { NSApp.terminate(nil) }
        }

        do {
            try server.start(port: Engine.port)
        } catch {
            NSLog("808 Reels Engine: não foi possível abrir o socket: \(error)")
            DispatchQueue.main.async { NSApp.terminate(nil) }
            return
        }

        // 60 ticks/s: escreve frames a 30 fps e envia a pré-visualização a 15 fps
        let frameTimer = DispatchSource.makeTimerSource(flags: .strict, queue: queue)
        frameTimer.schedule(deadline: .now(), repeating: .milliseconds(16), leeway: .milliseconds(1))
        frameTimer.setEventHandler { [weak self] in self?.onFrameTick() }
        frameTimer.resume()
        self.frameTimer = frameTimer

        // 1x/s: procura a janela do FL, envia o estado, fecha se ninguém o usar
        let slowTimer = DispatchSource.makeTimerSource(queue: queue)
        slowTimer.schedule(deadline: .now() + .milliseconds(200), repeating: .seconds(1))
        slowTimer.setEventHandler { [weak self] in self?.onSlowTick() }
        slowTimer.resume()
        self.slowTimer = slowTimer
    }

    // MARK: - Relógio

    private func onFrameTick() {
        tick += 1
        let now = HostClock.now()

        var composed: CIImage?
        func image() -> CIImage {
            if let composed { return composed }
            let base = compositor.compose(fl: capture.latestFrame,
                                          camera: compositor.cameraEnabled ? camera.latestFrame : nil)
            var recordingSeconds: Double?
            if state == .recording, let recorder {
                recordingSeconds = max(0, HostClock.seconds(from: recorder.startTicks, to: now))
            }
            let img = effects.apply(base, context: Effects.Context(
                time: HostClock.seconds(from: engineStartTicks, to: now),
                recordingSeconds: recordingSeconds))
            composed = img
            return img
        }

        if state == .recording, let recorder {
            let target = recorder.targetFrameIndex(at: now)

            // Se ficou muito para trás (ex.: Mac bloqueado), salta em vez de encher de cópias
            if target - recorder.nextFrameIndex > Int64(VideoRecorder.fps) {
                recorder.skip(to: target)
            }

            while recorder.nextFrameIndex <= target {
                let img = image()
                recorder.appendNextFrame { buffer in
                    self.compositor.render(img, to: buffer)
                }
            }
        }

        if tick % 4 == 0, server.clientCount > 0, capture.latestFrame != nil {
            if let jpeg = compositor.previewJPEG(image()) {
                server.broadcast(type: IPCServer.typeFrame, body: jpeg, droppable: true)
            }
        }
    }

    /// A câmara só fica ligada enquanto houver um plugin aberto (ou a gravar) e estiver ativa.
    private func updateCamera() {
        let wanted = compositor.cameraEnabled && (server.clientCount > 0 || state != .idle)
        camera.segmentationEnabled = compositor.backgroundMode != .original
        if wanted && !cameraRunning {
            camera.start()
            cameraRunning = true
        } else if !wanted && cameraRunning {
            camera.stop()
            cameraRunning = false
        }
    }

    private func onSlowTick() {
        updateCamera()

        if server.clientCount > 0 {
            lastClientTime = Date()
            capture.refresh()
            sendStatus()
            return
        }

        // Sem plugins ligados: fecha para não gastar recursos
        let idleLimit: TimeInterval = (capture.problem == .noPermission) ? 1 : 60
        if state == .idle, Date().timeIntervalSince(lastClientTime) > idleLimit {
            DispatchQueue.main.async { NSApp.terminate(nil) }
        }
    }

    private func statusMessage() -> String {
        let screen = screenStatusMessage()
        guard compositor.cameraEnabled else { return screen }

        switch camera.problem {
        case .none:
            return screen
        case .noPermission:
            return "Sem permissão da Câmara. Ative \"808 Reels Engine\" em Definições do Sistema > Privacidade e Segurança > Câmara. " + screen
        case .noCamera:
            return "Nenhuma câmara encontrada. " + screen
        case .failed(let text):
            return text + " " + screen
        }
    }

    private func screenStatusMessage() -> String {
        switch capture.problem {
        case .noPermission:
            return "Sem permissão de Gravação de ecrã. Ative \"808 Reels Engine\" em Definições do Sistema > Privacidade e Segurança > Gravação de ecrã e áudio do sistema. Depois feche e volte a abrir esta janela."
        case .noWindow:
            return "Janela do FL Studio não encontrada (está minimizada?)."
        case .failed(let text):
            return "Erro na captura: \(text)"
        case .none:
            switch state {
            case .recording: return "A gravar o FL Studio (1080x1920, 30 fps)."
            case .finishing, .exporting: return "A exportar o MP4..."
            case .idle: return capture.isCapturing ? "A captar o FL Studio." : "A iniciar a captura..."
            }
        }
    }

    private func sendStatus() {
        server.broadcastJSON([
            "event": "status",
            "capturing": capture.isCapturing,
            "recording": state == .recording,
            "message": statusMessage(),
            "version": Engine.version,
        ])
    }

    // MARK: - Comandos do plugin

    private func handleMessage(from client: IPCServer.Client, type: UInt8, body: Data) {
        guard type == IPCServer.typeJSON,
              let object = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
              let command = object["cmd"] as? String
        else { return }

        switch command {
        case "hello":
            client.sendJSON(["event": "welcome", "version": Engine.version])
            capture.refresh()
            sendStatus()

        case "crop":
            if let x = (object["x"] as? NSNumber)?.doubleValue {
                compositor.cropX = CGFloat(x)
            }
            if let y = (object["y"] as? NSNumber)?.doubleValue {
                compositor.cropY = CGFloat(y)
            }
            if let zoom = (object["zoom"] as? NSNumber)?.doubleValue {
                compositor.flZoom = CGFloat(zoom)
            }

        case "camera":
            if let enabled = object["enabled"] as? Bool {
                compositor.cameraEnabled = enabled
                updateCamera()
            }

        case "effect":
            if let name = object["name"] as? String {
                effects.kind = Effects.Kind(rawValue: name) ?? .none
            }

        case "level":
            if let bass = (object["bass"] as? NSNumber)?.doubleValue {
                effects.pushLevel(bass)
            }

        case "background":
            applyBackground(object)
            updateCamera()

        case "start":
            startRecording(object)

        case "stop":
            stopRecording(object)

        case "cancel":
            recorder?.cancel()
            recorder = nil
            state = .idle

        case "forget":
            // O plugin apagou a pasta: esquecer a última gravação
            if state == .idle { lastTake = nil }

        case "export":
            if let take = lastTake, state == .idle {
                export(take)
            }

        default:
            break
        }
    }

    private func applyBackground(_ object: [String: Any]) {
        guard let modeText = object["mode"] as? String, let mode = Compositor.BackgroundMode(rawValue: modeText) else { return }

        if let rgb = object["color"] as? [NSNumber], rgb.count == 3 {
            compositor.backgroundColor = CIColor(red: CGFloat(rgb[0].doubleValue),
                                                 green: CGFloat(rgb[1].doubleValue),
                                                 blue: CGFloat(rgb[2].doubleValue))
        }

        if mode == .image {
            guard let path = object["path"] as? String, compositor.loadBackgroundImage(path: path) else {
                sendError("Não foi possível abrir a imagem de fundo.")
                compositor.backgroundMode = .blur
                return
            }
        }

        compositor.backgroundMode = mode
    }

    private func sendError(_ message: String) {
        server.broadcastJSON(["event": "error", "message": message])
    }

    private func startRecording(_ object: [String: Any]) {
        guard state == .idle else {
            sendError("O motor ainda está ocupado com a gravação anterior.")
            return
        }
        guard let videoPath = object["video"] as? String, let outputPath = object["output"] as? String else {
            sendError("Comando de gravação inválido.")
            return
        }
        guard capture.latestFrame != nil else {
            sendError("Sem imagem do FL Studio: " + statusMessage() + " A gravar só o áudio.")
            return
        }

        do {
            recorder = try VideoRecorder(url: URL(fileURLWithPath: videoPath), startTicks: HostClock.now())
            pendingOutputURL = URL(fileURLWithPath: outputPath)
            state = .recording
            server.broadcastJSON(["event": "recording"])
            sendStatus()
        } catch {
            recorder = nil
            sendError("Não foi possível gravar o vídeo: \(error.localizedDescription)")
        }
    }

    private func stopRecording(_ object: [String: Any]) {
        guard state == .recording, let recorder, let outputURL = pendingOutputURL else {
            sendError("Não havia vídeo a ser gravado.")
            return
        }

        guard let wavPath = object["wav"] as? String,
              let ticksText = object["audioStartTicks"] as? String,
              let audioStartTicks = UInt64(ticksText)
        else {
            recorder.cancel()
            self.recorder = nil
            state = .idle
            sendError("Dados do áudio em falta: o vídeo foi descartado.")
            return
        }

        // Onde cai a 1.ª amostra do WAV na linha de tempo do vídeo
        let audioOffset = HostClock.seconds(from: recorder.startTicks, to: audioStartTicks)
        let wavURL = URL(fileURLWithPath: wavPath)

        state = .finishing
        self.recorder = nil
        sendStatus()

        if recorder.droppedFrames > 0 {
            NSLog("808 Reels Engine: \(recorder.droppedFrames) frames perdidos")
        }

        recorder.finish { [weak self] result in
            guard let self else { return }
            self.queue.async {
                switch result {
                case .success(let videoURL):
                    let take = Take(videoURL: videoURL, wavURL: wavURL, outputURL: outputURL, audioOffset: audioOffset)
                    self.lastTake = take
                    self.export(take)
                case .failure(let error):
                    self.state = .idle
                    self.sendError("Erro ao fechar o vídeo: \(error.localizedDescription)")
                }
            }
        }
    }

    private func export(_ take: Take) {
        state = .exporting
        server.broadcastJSON(["event": "exporting", "progress": 0.0])

        let job = ExportJob(videoURL: take.videoURL, wavURL: take.wavURL,
                            outputURL: take.outputURL, audioOffset: take.audioOffset)

        Exporter.run(job, progress: { [weak self] value in
            guard let self else { return }
            self.queue.async {
                let now = Date()
                if now.timeIntervalSince(self.lastProgressSent) > 0.1 {
                    self.lastProgressSent = now
                    self.server.broadcastJSON(["event": "exporting", "progress": value])
                }
            }
        }, completion: { [weak self] result in
            guard let self else { return }
            self.queue.async {
                self.state = .idle
                switch result {
                case .success(let url):
                    self.server.broadcastJSON(["event": "exported", "path": url.path])
                case .failure(let error):
                    self.sendError("Erro na exportação: \(error.localizedDescription)")
                }
                self.sendStatus()
            }
        })
    }
}
