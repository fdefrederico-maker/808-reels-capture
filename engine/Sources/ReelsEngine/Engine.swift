import Foundation
import AppKit
import CoreGraphics
import CoreImage

// Coordena tudo. Todo o estado é alterado apenas na fila `queue`.
final class Engine {
    static let port: UInt16 = 48808
    static let version = (Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String) ?? "dev"

    private enum State { case idle, recording, draining, finishing, exporting }

    private struct Take {
        let videoURL: URL
        let wavURL: URL
        let outputURL: URL
        /// Posição do áudio na linha de tempo do vídeo, sem o ajuste manual.
        let audioOffset: Double
    }

    private struct PendingStop {
        let wavURL: URL
        let audioStartTicks: UInt64
        let syncOffset: Double
        let stopIndex: Int64
    }

    /// O vídeo é montado com este atraso fixo, para a imagem da câmara e do FL de cada
    /// instante já terem chegado (as webcams entregam as imagens 0,1-0,3 s depois).
    static let renderDelay: Double = 0.4

    private let queue = DispatchQueue(label: "808reels.engine", qos: .userInteractive)
    private lazy var server = IPCServer(queue: queue)
    private lazy var capture = WindowCapture(queue: queue)
    private let camera = CameraCapture()
    private let compositor = Compositor()
    private let previewEffects = Effects()      // pré-visualização ao vivo
    private let videoEffects = Effects()        // vídeo gravado (alinhado pela hora real)
    private let engineStartTicks = HostClock.now()
    private var cameraRunning = false

    // Imagens recentes com a hora real de captura
    private let flRing = FrameRing(maxAge: Engine.renderDelay + 0.5)
    private let cameraRing = FrameRing(maxAge: Engine.renderDelay + 0.5)
    private var levelHistory: [(ticks: UInt64, value: Double)] = []
    private var levelsFedUntil: UInt64 = 0

    private var state: State = .idle
    private var recorder: VideoRecorder?
    private var pendingOutputURL: URL?
    private var pendingStop: PendingStop?
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

        // Cada imagem do FL é logo reduzida à faixa final e guardada com a hora em que foi mostrada
        capture.onFrame = { [weak self] pixelBuffer, ticks in
            guard let self, let band = self.compositor.renderFLBand(pixelBuffer) else { return }
            self.flRing.push(band, ticks: ticks)
        }

        // Cada imagem da câmara (com o fundo já trocado) é guardada com a hora em que foi CAPTADA
        camera.onFrame = { [weak self] frame, ticks in
            guard let self else { return }
            self.queue.async {
                guard self.cameraRunning, let band = self.compositor.renderCameraBand(frame) else { return }
                self.cameraRing.push(band, ticks: ticks)
            }
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

    /// Volta a preparar a faixa do FL com o enquadramento atual (zoom/recorte mudaram
    /// e o FL pode estar parado, sem enviar imagens novas).
    private func refreshFLBand() {
        guard let raw = capture.latestFrame, let band = compositor.renderFLBand(raw) else { return }
        flRing.push(band, ticks: HostClock.now())
    }

    // MARK: - Relógio

    private func onFrameTick() {
        tick += 1
        let now = HostClock.now()

        if state == .recording || state == .draining {
            writeDueFrames(now: now)
        }

        // Pré-visualização ao vivo (imagens mais recentes)
        if tick % 4 == 0, server.clientCount > 0, !flRing.isEmpty {
            var recordingSeconds: Double?
            if state == .recording, let recorder {
                recordingSeconds = max(0, HostClock.seconds(from: recorder.startTicks, to: now))
            }
            let base = compositor.composeBands(fl: flRing.latest,
                                               camera: compositor.cameraEnabled ? cameraRing.latest : nil)
            let image = previewEffects.apply(base, context: Effects.Context(
                time: HostClock.seconds(from: engineStartTicks, to: now),
                recordingSeconds: recordingSeconds))
            if let jpeg = compositor.previewJPEG(image) {
                server.broadcast(type: IPCServer.typeFrame, body: jpeg, droppable: true)
            }
        }
    }

    /// Escreve os frames cujo instante já passou há mais de `renderDelay`. Cada frame usa a
    /// imagem da câmara e do FL captadas nesse MESMO instante (o áudio usa o mesmo relógio).
    private func writeDueFrames(now: UInt64) {
        guard let recorder else { return }

        var target = recorder.targetFrameIndex(at: now &- HostClock.ticks(seconds: Engine.renderDelay))
        if let stop = pendingStop {
            target = min(target, stop.stopIndex)
        }

        // Se ficou muito para trás (ex.: Mac bloqueado), salta em vez de encher de cópias
        if target - recorder.nextFrameIndex > Int64(VideoRecorder.fps) {
            recorder.skip(to: target)
        }

        while recorder.nextFrameIndex <= target {
            let frameTicks = recorder.ticks(forFrame: recorder.nextFrameIndex)
            feedLevels(upTo: frameTicks)

            let base = compositor.composeBands(fl: flRing.frame(at: frameTicks),
                                               camera: compositor.cameraEnabled ? cameraRing.frame(at: frameTicks) : nil)
            let image = videoEffects.apply(base, context: Effects.Context(
                time: HostClock.seconds(from: engineStartTicks, to: frameTicks),
                recordingSeconds: max(0, HostClock.seconds(from: recorder.startTicks, to: frameTicks))))

            recorder.appendNextFrame { buffer in
                self.compositor.render(image, to: buffer)
            }
        }

        if state == .draining, let stop = pendingStop, recorder.nextFrameIndex > stop.stopIndex {
            finishRecording()
        }
    }

    /// Passa ao efeito do vídeo os níveis de graves até ao instante do frame.
    private func feedLevels(upTo ticks: UInt64) {
        for entry in levelHistory where entry.ticks > levelsFedUntil && entry.ticks <= ticks {
            videoEffects.pushLevel(entry.value)
        }
        levelsFedUntil = max(levelsFedUntil, ticks)
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
            cameraRing.clear()
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
            let ms = Int((camera.measuredLatency * 1000).rounded())
            guard cameraRunning, ms > 0 else { return screen }
            let note = Double(ms) / 1000 > Engine.renderDelay
                ? " Câmara: atraso \(ms) ms (acima do compensado: use a Sincronização)."
                : " Câmara: atraso \(ms) ms (compensado)."
            return screen + note
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
            case .draining, .finishing, .exporting: return "A exportar o MP4..."
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
            refreshFLBand()

        case "camera":
            if let enabled = object["enabled"] as? Bool {
                compositor.cameraEnabled = enabled
                updateCamera()
                refreshFLBand()   // a faixa do FL muda de tamanho
            }

        case "effect":
            if let name = object["name"] as? String {
                let kind = Effects.Kind(rawValue: name) ?? .none
                previewEffects.kind = kind
                videoEffects.kind = kind
            }

        case "level":
            if let bass = (object["bass"] as? NSNumber)?.doubleValue {
                previewEffects.pushLevel(bass)
                let ticks = (object["ticks"] as? String).flatMap { UInt64($0) } ?? HostClock.now()
                levelHistory.append((ticks, bass))
                if levelHistory.count > 200 { levelHistory.removeFirst(levelHistory.count - 200) }
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
            pendingStop = nil
            state = .idle

        case "forget":
            // O plugin apagou a pasta: esquecer a última gravação
            if state == .idle { lastTake = nil }

        case "export":
            let sync = ((object["syncOffsetMs"] as? NSNumber)?.doubleValue ?? 0) / 1000
            if let take = lastTake, state == .idle {
                export(take, syncOffset: sync)
            } else if lastTake == nil {
                sendError("Não há gravação para voltar a exportar.")
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
            pendingStop = nil
            levelsFedUntil = HostClock.now()
            state = .recording
            server.broadcastJSON(["event": "recording"])
            sendStatus()
        } catch {
            recorder = nil
            sendError("Não foi possível gravar o vídeo: \(error.localizedDescription)")
        }
    }

    private func stopRecording(_ object: [String: Any]) {
        guard state == .recording, let recorder else {
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

        // O vídeo continua a ser montado até ao instante do "Parar" (com o atraso fixo)
        pendingStop = PendingStop(
            wavURL: URL(fileURLWithPath: wavPath),
            audioStartTicks: audioStartTicks,
            syncOffset: ((object["syncOffsetMs"] as? NSNumber)?.doubleValue ?? 0) / 1000,
            stopIndex: max(0, recorder.targetFrameIndex(at: HostClock.now())))
        state = .draining
        sendStatus()
    }

    private func finishRecording() {
        guard let recorder, let stop = pendingStop, let outputURL = pendingOutputURL else { return }

        // Onde cai a 1.ª amostra do WAV na linha de tempo do vídeo
        let audioOffset = HostClock.seconds(from: recorder.startTicks, to: stop.audioStartTicks)

        state = .finishing
        self.recorder = nil
        pendingStop = nil

        if recorder.droppedFrames > 0 {
            NSLog("808 Reels Engine: \(recorder.droppedFrames) frames perdidos")
        }

        recorder.finish { [weak self] result in
            guard let self else { return }
            self.queue.async {
                switch result {
                case .success(let videoURL):
                    let take = Take(videoURL: videoURL, wavURL: stop.wavURL, outputURL: outputURL, audioOffset: audioOffset)
                    self.lastTake = take
                    self.export(take, syncOffset: stop.syncOffset)
                case .failure(let error):
                    self.state = .idle
                    self.sendError("Erro ao fechar o vídeo: \(error.localizedDescription)")
                }
            }
        }
    }

    private func export(_ take: Take, syncOffset: Double) {
        state = .exporting
        server.broadcastJSON(["event": "exporting", "progress": 0.0])

        // syncOffset > 0 atrasa o som (para quando a imagem parece atrasada)
        let job = ExportJob(videoURL: take.videoURL, wavURL: take.wavURL,
                            outputURL: take.outputURL, audioOffset: take.audioOffset + syncOffset)

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
