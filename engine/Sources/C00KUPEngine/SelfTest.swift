import Foundation
import AVFoundation
import CoreImage
import CoreVideo
import ImageIO

// Autoteste executado no GitHub Actions:  C00KUPEngine --selftest
// Verifica o recorte vertical, a pré-visualização, a gravação H.264,
// a exportação MP4 e a sincronização áudio/vídeo — sem precisar de ecrã.
enum SelfTest {
    private static var failures = 0

    private static func check(_ condition: Bool, _ message: String) {
        print((condition ? "OK    " : "FALHA ") + message)
        if !condition { failures += 1 }
    }

    static func run() -> Int32 {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("c00kup-selftest-\(getpid())")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)

        testCompositor()
        testCameraLayout()
        testZoom()
        testEffects()
        testFrameRing()
        testRecordAndExport(dir: dir)

        print(failures == 0 ? "\nAUTOTESTE: TUDO OK" : "\nAUTOTESTE: \(failures) FALHA(S)")
        return failures == 0 ? 0 : 1
    }

    // MARK: - Recorte e orientação

    private static func testCompositor() {
        // "Janela do FL" sintética 3414x1920: terços vermelho/verde/azul, faixa branca no topo
        let source = makeBuffer(width: 3414, height: 1920) { x, y in
            if y < 100 { return (255, 255, 255) }
            if x < 3414 / 3 { return (255, 0, 0) }
            if x < 2 * 3414 / 3 { return (0, 255, 0) }
            return (0, 0, 255)
        }

        let compositor = Compositor()
        compositor.cameraEnabled = false   // FL em ecrã inteiro
        for (cropX, expected) in [(0.0, "vermelho"), (0.5, "verde"), (1.0, "azul")] {
            compositor.cropX = CGFloat(cropX)
            let image = compositor.compose(fl: source, camera: nil)
            let output = makeBuffer(width: 1080, height: 1920) { _, _ in (0, 0, 0) }
            compositor.render(image, to: output)

            let centre = readPixel(output, x: 540, y: 960)
            check(colourName(centre) == expected, "recorte x=\(cropX): centro \(colourName(centre)) (esperado \(expected))")

            let top = readPixel(output, x: 540, y: 20)
            check(top.r > 200 && top.g > 200 && top.b > 200, "recorte x=\(cropX): faixa branca no topo (orientação correta)")

            // Pré-visualização = mesma imagem reduzida
            if let jpeg = compositor.previewJPEG(image),
               let src = CGImageSourceCreateWithData(jpeg as CFData, nil),
               let cg = CGImageSourceCreateImageAtIndex(src, 0, nil) {
                check(cg.width == 540 && cg.height == 960, "pré-visualização \(cg.width)x\(cg.height) (esperado 540x960)")
                let p = pixelOf(cg, x: 270, y: 480)
                check(colourName(p) == expected, "pré-visualização x=\(cropX): centro \(colourName(p)) igual ao vídeo")
                let t = pixelOf(cg, x: 270, y: 8)
                check(t.r > 200 && t.g > 200 && t.b > 200, "pré-visualização x=\(cropX): topo branco")
            } else {
                check(false, "pré-visualização JPEG não gerada")
            }
        }
    }

    // MARK: - Zoom e posição vertical do FL

    private static func testZoom() {
        // Janela 3000x1920: faixa branca nos 200 px de cima, faixa preta nos 200 de baixo,
        // e um quadrado amarelo no centro (1400..1600 x 860..1060)
        let source = makeBuffer(width: 3000, height: 1920) { x, y in
            if y < 200 { return (255, 255, 255) }
            if y >= 1720 { return (0, 0, 0) }
            if (1400 ..< 1600).contains(x) && (860 ..< 1060).contains(y) { return (255, 255, 0) }
            return (255, 0, 0)
        }
        let compositor = Compositor()
        compositor.cameraEnabled = false

        func render() -> CVPixelBuffer {
            let out = makeBuffer(width: 1080, height: 1920) { _, _ in (0, 0, 0) }
            compositor.render(compositor.compose(fl: source, camera: nil), to: out)
            return out
        }

        // Zoom 1: a janela toda na altura (topo branco, fundo preto)
        compositor.flZoom = 1; compositor.cropX = 0.5; compositor.cropY = 0.5
        var out = render()
        var p = readPixel(out, x: 540, y: 50)
        check(p.r > 200 && p.g > 200 && p.b > 200, "zoom 1x: topo da janela visível")
        p = readPixel(out, x: 540, y: 1880)
        check(p.r < 50 && p.g < 50 && p.b < 50, "zoom 1x: fundo da janela visível")

        // Zoom 4 no centro: o quadrado amarelo (200 px na janela) passa a ~800 px
        compositor.flZoom = 4
        out = render()
        p = readPixel(out, x: 540, y: 960)
        check(p.r > 200 && p.g > 200 && p.b < 60, "zoom 4x centro: quadrado amarelo no meio")
        p = readPixel(out, x: 540, y: 960 - 350)
        check(p.r > 200 && p.g > 200 && p.b < 60, "zoom 4x: quadrado ampliado (~800 px)")

        // Zoom 3, posição vertical em cima: vê-se a faixa branca
        compositor.flZoom = 3; compositor.cropY = 0
        out = render()
        p = readPixel(out, x: 540, y: 100)
        check(p.r > 200 && p.g > 200 && p.b > 200, "zoom 3x + topo: faixa branca visível")

        // Zoom 3, posição vertical em baixo: vê-se a faixa preta
        compositor.cropY = 1
        out = render()
        p = readPixel(out, x: 540, y: 1850)
        check(p.r < 50 && p.g < 50 && p.b < 50, "zoom 3x + fundo: faixa preta visível")
    }

    // MARK: - Sincronização da câmara (imagens com a hora real de captura)

    private static func testFrameRing() {
        let ring = FrameRing(maxAge: 0.9)
        let t0 = HostClock.now()
        func at(_ s: Double) -> UInt64 { t0 &+ HostClock.ticks(seconds: s) }

        // Câmara a 30 fps; cada imagem tem um valor = número do frame
        var frames: [CVPixelBuffer] = []
        for i in 0 ..< 60 {
            let v = UInt8(i * 4)
            frames.append(makeBuffer(width: 4, height: 4) { _, _ in (v, v, v) })
        }

        // As imagens CHEGAM com 250 ms de atraso e algumas fora de ordem,
        // mas são guardadas com a hora em que foram CAPTADAS.
        var order = Array(0 ..< 60)
        order.swapAt(10, 11); order.swapAt(30, 32)
        for i in order { ring.push(frames[i], ticks: at(Double(i) / 30)) }

        // O frame de vídeo do instante 1,50 s deve usar a imagem captada em 1,50 s (n.º 45),
        // e não a última que chegou (n.º 59).
        let chosen = ring.frame(at: at(1.5 + 0.001))
        let value = chosen.map { readPixel($0, x: 0, y: 0).r } ?? -1
        check(value == 45 * 4, "anel de imagens: instante 1,50 s usa a imagem captada em 1,50 s (\(value / 4))")

        // Imagens antigas são descartadas (só ~0,9 s fica guardado)
        check(ring.count <= 29, "anel de imagens: descarta imagens antigas (\(ring.count) guardadas)")

        // Instante anterior a tudo o que existe: usa a mais antiga em vez de nada
        check(ring.frame(at: t0) != nil, "anel de imagens: devolve sempre uma imagem")

        // Latência do dispositivo de saída de som (no Mac do GitHub pode não haver nenhum)
        if let out = AudioOutputLatency.current() {
            print(String(format: "      saída de som: %@ = %.0f ms", out.deviceName, out.seconds * 1000))
            check(out.seconds >= 0 && out.seconds <= 1, "latência da saída de som dentro de limites")
        } else {
            print("      saída de som: nenhum dispositivo (normal no Mac do GitHub)")
        }

        // Conversão segundos <-> relógio do sistema
        let back = HostClock.seconds(from: t0, to: at(0.4))
        check(abs(back - 0.4) < 0.0001, String(format: "relógio: 0,4 s ida e volta (%.6f)", back))
    }

    // MARK: - Efeitos retro

    private static func testEffects() {
        let fl = makeBuffer(width: 3000, height: 1920) { x, y in
            ((x / 40 + y / 40) % 2 == 0) ? (220, 60, 40) : (40, 120, 220)
        }
        let cam = makeBuffer(width: 1280, height: 720) { _, _ in (200, 180, 160) }
        let compositor = Compositor()
        compositor.mirrorCamera = false
        let base = compositor.compose(fl: fl, camera: CameraFrame(image: cam, mask: nil))

        func render(_ image: CIImage) -> (CVPixelBuffer, Double) {
            let out = makeBuffer(width: 1080, height: 1920) { _, _ in (0, 0, 0) }
            let start = Date()
            compositor.render(image, to: out)
            return (out, Date().timeIntervalSince(start) * 1000)
        }

        let plain = render(base).0
        saveSample(plain, name: "0-sem-efeito")
        let effects = Effects()

        for kind in [Effects.Kind.vhs, .film, .tv] {
            effects.kind = kind
            var worst = 0.0
            var out = plain
            for frame in 0 ..< 10 {
                let ctx = Effects.Context(time: Double(frame) / 30, recordingSeconds: kind == .vhs ? 12 : nil)
                let r = render(effects.apply(base, context: ctx))
                out = r.0
                if frame > 1 { worst = max(worst, r.1) }
            }
            saveSample(out, name: "efeito-" + kind.rawValue)
            let a = readPixel(out, x: 540, y: 1200), b = readPixel(plain, x: 540, y: 1200)
            let changed = abs(a.r - b.r) + abs(a.g - b.g) + abs(a.b - b.b) > 10
            check(changed, "efeito \(kind.rawValue): imagem alterada")
            print(String(format: "      efeito %@: pior frame %.1f ms (limite em tempo real: 33 ms)", kind.rawValue, worst))
            if kind == .tv {
                let p = readPixel(out, x: 540, y: 1200)
                check(abs(p.r - p.g) < 12 && abs(p.g - p.b) < 12, "TV antiga: preto e branco (\(p.r),\(p.g),\(p.b))")
                let corner = readPixel(out, x: 2, y: 2)
                check(corner.r < 10 && corner.g < 10 && corner.b < 10, "TV antiga: cantos arredondados pretos")
            }
        }

        // Glitch: sem graves não mexe; com um golpe forte de graves altera a imagem
        effects.kind = .glitch
        var t = 0.0
        for _ in 0 ..< 60 {   // 2 s de música "calma" para o nível de referência
            effects.pushLevel(0.2); t += 1.0 / 30
            _ = effects.apply(base, context: Effects.Context(time: t, recordingSeconds: nil))
        }
        t += 1.0
        _ = effects.apply(base, context: Effects.Context(time: t, recordingSeconds: nil))   // deixa a envolvente cair
        let quiet = render(effects.apply(base, context: Effects.Context(time: t + 1.0 / 30, recordingSeconds: nil))).0
        let q = readPixel(quiet, x: 540, y: 1200), b = readPixel(plain, x: 540, y: 1200)
        check(abs(q.r - b.r) + abs(q.g - b.g) + abs(q.b - b.b) <= 6, "glitch: sem golpe de graves a imagem fica igual")

        effects.pushLevel(1.0)
        let (hit, ms) = render(effects.apply(base, context: Effects.Context(time: t + 2.0 / 30, recordingSeconds: nil)))
        saveSample(hit, name: "efeito-glitch-golpe")
        check(effects.beatAmount > 0.5, String(format: "glitch: golpe de graves detetado (%.2f)", effects.beatAmount))
        var diff = 0
        for y in stride(from: 100, to: 1900, by: 150) {
            let h = readPixel(hit, x: 300, y: y), p = readPixel(plain, x: 300, y: y)
            if abs(h.r - p.r) + abs(h.g - p.g) + abs(h.b - p.b) > 30 { diff += 1 }
        }
        check(diff >= 3, "glitch: imagem alterada no golpe (\(diff) pontos diferentes)")
        print(String(format: "      efeito glitch: %.1f ms", ms))
    }

    // MARK: - Câmara no topo + troca de fundo

    private static func testCameraLayout() {
        // FL vermelho, câmara amarela; "pessoa" = metade esquerda da imagem da câmara
        let fl = makeBuffer(width: 3000, height: 1920) { _, _ in (255, 0, 0) }
        let cam = makeBuffer(width: 1280, height: 720) { _, _ in (255, 255, 0) }
        let mask = makeMask(width: 512, height: 288) { x, _ in x < 256 ? 255 : 0 }

        let compositor = Compositor()
        compositor.cameraEnabled = true
        compositor.mirrorCamera = false

        func render(_ frame: CameraFrame?) -> CVPixelBuffer {
            let out = makeBuffer(width: 1080, height: 1920) { _, _ in (0, 0, 0) }
            compositor.render(compositor.compose(fl: fl, camera: frame), to: out)
            return out
        }

        // Sala original
        compositor.backgroundMode = .original
        var out = render(CameraFrame(image: cam, mask: mask))
        var p = readPixel(out, x: 540, y: 300)
        check(p.r > 200 && p.g > 200 && p.b < 60, "câmara na faixa de cima (\(p.r),\(p.g),\(p.b))")
        p = readPixel(out, x: 540, y: 1300)
        check(colourName(p) == "vermelho", "FL na parte de baixo: \(colourName(p))")
        p = readPixel(out, x: 540, y: 590)
        check(p.r > 200 && p.g > 200, "câmara ocupa até ~600 px")
        p = readPixel(out, x: 540, y: 610)
        check(colourName(p) == "vermelho", "FL começa logo abaixo da câmara")

        // Fundo trocado por cor azul: lado esquerdo (pessoa) amarelo, lado direito azul
        compositor.backgroundMode = .color
        compositor.backgroundColor = CIColor(red: 0, green: 0, blue: 1)
        out = render(CameraFrame(image: cam, mask: mask))
        p = readPixel(out, x: 200, y: 300)
        check(p.r > 200 && p.g > 200 && p.b < 60, "fundo trocado: pessoa mantém-se (\(p.r),\(p.g),\(p.b))")
        p = readPixel(out, x: 880, y: 300)
        check(colourName(p) == "azul", "fundo trocado: sala substituída por azul (\(colourName(p)))")

        // Espelho: com a câmara espelhada, a pessoa passa para o lado direito
        compositor.mirrorCamera = true
        out = render(CameraFrame(image: cam, mask: mask))
        p = readPixel(out, x: 880, y: 300)
        check(p.r > 200 && p.g > 200 && p.b < 60, "câmara espelhada: pessoa à direita")

        // Quarto escuro: o brilho da câmara é corrigido automaticamente
        let darkCam = makeBuffer(width: 1280, height: 720) { _, _ in (28, 26, 24) }
        compositor.backgroundMode = .original
        for _ in 0 ..< 60 { _ = render(CameraFrame(image: darkCam, mask: nil)) }
        out = render(CameraFrame(image: darkCam, mask: nil))
        p = readPixel(out, x: 540, y: 300)
        check(p.r > 50, String(format: "pouca luz: câmara aclarada automaticamente (%d -> %d, ganho %.1fx)", 28, p.r, compositor.cameraGain))

        // Câmara bem iluminada: não mexe
        let brightCam = makeBuffer(width: 1280, height: 720) { _, _ in (150, 140, 130) }
        let fresh = Compositor()
        fresh.mirrorCamera = false
        for _ in 0 ..< 30 {
            let o = makeBuffer(width: 1080, height: 1920) { _, _ in (0, 0, 0) }
            fresh.render(fresh.compose(fl: fl, camera: CameraFrame(image: brightCam, mask: nil)), to: o)
        }
        check(fresh.cameraGain < 1.05, String(format: "boa luz: brilho da câmara inalterado (ganho %.2fx)", fresh.cameraGain))

        // Desfocado e sem câmara não podem rebentar
        compositor.backgroundMode = .blur
        _ = render(CameraFrame(image: cam, mask: mask))
        _ = render(nil)
        check(true, "fundo desfocado e câmara em falta")
    }

    // MARK: - Gravação + exportação + sincronização

    private static func testRecordAndExport(dir: URL) {
        let videoURL = dir.appendingPathComponent("teste.mov")
        let wavURL = dir.appendingPathComponent("teste.wav")
        let outURL = dir.appendingPathComponent("teste.mp4")

        // Vídeo: 90 frames (3 s); flash branco nos frames 30-32 (t = 1,0 s)
        let compositor = Compositor()
        let rect = CGRect(origin: .zero, size: Compositor.outputSize)
        let dark = CIImage(color: CIColor(red: 0.1, green: 0.1, blue: 0.1)).cropped(to: rect)
        let white = CIImage(color: CIColor(red: 1, green: 1, blue: 1)).cropped(to: rect)

        do {
            let recorder = try VideoRecorder(url: videoURL, startTicks: HostClock.now())
            for i in 0 ..< 90 {
                let img = (30 ... 32).contains(i) ? white : dark
                recorder.appendNextFrame(waitIfBusy: true) { compositor.render(img, to: $0) }
            }
            check(recorder.droppedFrames == 0, "gravação H.264: \(recorder.droppedFrames) frames perdidos")

            let sem = DispatchSemaphore(value: 0)
            var finished: Result<URL, Error>?
            recorder.finish { finished = $0; sem.signal() }
            sem.wait()
            if case .failure(let e) = finished! { check(false, "fechar vídeo: \(e.localizedDescription)"); return }
        } catch {
            check(false, "criar gravador de vídeo: \(error.localizedDescription)")
            return
        }

        // WAV 48 kHz: o áudio começou 0,25 s ANTES do vídeo, por isso o clique
        // que corresponde ao flash está em 1,25 s no WAV.
        let audioOffset = -0.25
        do {
            let settings: [String: Any] = [
                AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2,
                AVLinearPCMBitDepthKey: 24, AVLinearPCMIsFloatKey: false, AVLinearPCMIsBigEndianKey: false,
            ]
            let file = try AVAudioFile(forWriting: wavURL, settings: settings, commonFormat: .pcmFormatFloat32, interleaved: false)
            let frames = AVAudioFrameCount(48000 * 3.5)
            let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: frames)!
            buffer.frameLength = frames
            let clickStart = Int(1.25 * 48000)
            for ch in 0 ..< 2 {
                let data = buffer.floatChannelData![ch]
                for i in 0 ..< Int(frames) { data[i] = (i >= clickStart && i < clickStart + 480) ? 0.9 : 0 }
            }
            try file.write(from: buffer)
        } catch {
            check(false, "criar WAV: \(error.localizedDescription)")
            return
        }

        // Exportar
        let sem = DispatchSemaphore(value: 0)
        var exported: Result<URL, Error>?
        Exporter.run(ExportJob(videoURL: videoURL, wavURL: wavURL, outputURL: outURL, audioOffset: audioOffset),
                     progress: { _ in },
                     completion: { exported = $0; sem.signal() })
        sem.wait()

        if case .failure(let e) = exported! {
            check(false, "exportar MP4: \(e.localizedDescription)")
            return
        }
        check(true, "exportar MP4")

        // Verificar o MP4
        let asset = AVURLAsset(url: outURL)
        let videoTracks = asset.tracks(withMediaType: .video)
        let audioTracks = asset.tracks(withMediaType: .audio)
        check(videoTracks.count == 1, "MP4 tem 1 pista de vídeo")
        check(audioTracks.count == 1, "MP4 tem 1 pista de áudio")
        guard let vt = videoTracks.first, let at = audioTracks.first else { return }

        let size = vt.naturalSize
        check(Int(size.width) == 1080 && Int(size.height) == 1920, "resolução \(Int(size.width))x\(Int(size.height))")
        check(abs(vt.nominalFrameRate - 30) < 0.5, "frame rate \(vt.nominalFrameRate)")
        check(abs(asset.duration.seconds - 3.0) < 0.1, "duração \(String(format: "%.3f", asset.duration.seconds)) s")

        let formats = at.formatDescriptions as! [CMFormatDescription]
        if let f = formats.first, let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(f)?.pointee {
            check(asbd.mFormatID == kAudioFormatMPEG4AAC, "áudio AAC")
            check(asbd.mSampleRate == 48000, "áudio 48 kHz")
        }
        if let f = (vt.formatDescriptions as! [CMFormatDescription]).first {
            check(CMFormatDescriptionGetMediaSubType(f) == kCMVideoCodecType_H264, "vídeo H.264")
        }

        guard let flash = findFlash(asset: asset, track: vt), let click = findClick(asset: asset, track: at) else {
            check(false, "não foi possível encontrar o flash e/ou o clique")
            return
        }
        let diffMs = (click - flash) * 1000
        print(String(format: "      flash no vídeo: %.3f s | clique no áudio: %.3f s | diferença: %.1f ms", flash, click, diffMs))
        check(abs(flash - 1.0) < 0.001, "flash no instante 1,000 s")
        check(abs(diffMs) <= 40, "sincronização áudio/vídeo dentro de 1 frame")
    }

    private static func findFlash(asset: AVAsset, track: AVAssetTrack) -> Double? {
        guard let reader = try? AVAssetReader(asset: asset) else { return nil }
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        ])
        reader.add(output)
        reader.startReading()
        while let sample = output.copyNextSampleBuffer() {
            guard let pb = CMSampleBufferGetImageBuffer(sample) else { continue }
            if readPixel(pb, x: 540, y: 960).r > 200 {
                reader.cancelReading()
                return CMSampleBufferGetPresentationTimeStamp(sample).seconds
            }
        }
        return nil
    }

    private static func findClick(asset: AVAsset, track: AVAssetTrack) -> Double? {
        guard let reader = try? AVAssetReader(asset: asset) else { return nil }
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 32, AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsBigEndianKey: false, AVLinearPCMIsNonInterleaved: false,
            AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2,
        ])
        reader.add(output)
        reader.startReading()
        while let sample = output.copyNextSampleBuffer() {
            guard let block = CMSampleBufferGetDataBuffer(sample) else { continue }
            let length = CMBlockBufferGetDataLength(block)
            var floats = [Float](repeating: 0, count: length / 4)
            CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: length, destination: &floats)
            if let idx = floats.firstIndex(where: { abs($0) > 0.3 }) {
                reader.cancelReading()
                let start = CMSampleBufferGetPresentationTimeStamp(sample).seconds
                return start + Double(idx / 2) / 48000.0
            }
        }
        return nil
    }

    // MARK: - Utilitários de píxeis

    private typealias RGB = (r: Int, g: Int, b: Int)

    private static func colourName(_ p: RGB) -> String {
        if p.r > 150 && p.g < 100 && p.b < 100 { return "vermelho" }
        if p.g > 150 && p.r < 100 && p.b < 100 { return "verde" }
        if p.b > 150 && p.r < 100 && p.g < 100 { return "azul" }
        return "(\(p.r),\(p.g),\(p.b))"
    }

    private static func makeBuffer(width: Int, height: Int, fill: (Int, Int) -> (UInt8, UInt8, UInt8)) -> CVPixelBuffer {
        var buffer: CVPixelBuffer?
        let attrs: [String: Any] = [kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any]()]
        CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA, attrs as CFDictionary, &buffer)
        let pb = buffer!
        CVPixelBufferLockBaseAddress(pb, [])
        let base = CVPixelBufferGetBaseAddress(pb)!.assumingMemoryBound(to: UInt8.self)
        let bpr = CVPixelBufferGetBytesPerRow(pb)
        for y in 0 ..< height {
            for x in 0 ..< width {
                let (r, g, b) = fill(x, y)
                let o = y * bpr + x * 4
                base[o] = b; base[o + 1] = g; base[o + 2] = r; base[o + 3] = 255
            }
        }
        CVPixelBufferUnlockBaseAddress(pb, [])
        return pb
    }

    /// Guarda uma imagem PNG de exemplo (se SELFTEST_OUT estiver definido) para inspeção visual.
    private static func saveSample(_ buffer: CVPixelBuffer, name: String) {
        guard let dir = ProcessInfo.processInfo.environment["SELFTEST_OUT"] else { return }
        let url = URL(fileURLWithPath: dir).appendingPathComponent(name + ".png")
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let context = CIContext()
        let image = CIImage(cvPixelBuffer: buffer)
        try? context.writePNGRepresentation(of: image, to: url, format: .RGBA8,
                                            colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!)
    }

    private static func makeMask(width: Int, height: Int, fill: (Int, Int) -> UInt8) -> CVPixelBuffer {
        var buffer: CVPixelBuffer?
        let attrs: [String: Any] = [kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any]()]
        CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_OneComponent8, attrs as CFDictionary, &buffer)
        let pb = buffer!
        CVPixelBufferLockBaseAddress(pb, [])
        let base = CVPixelBufferGetBaseAddress(pb)!.assumingMemoryBound(to: UInt8.self)
        let bpr = CVPixelBufferGetBytesPerRow(pb)
        for y in 0 ..< height { for x in 0 ..< width { base[y * bpr + x] = fill(x, y) } }
        CVPixelBufferUnlockBaseAddress(pb, [])
        return pb
    }

    /// (x, y) com y = 0 na linha de CIMA da imagem.
    private static func readPixel(_ pb: CVPixelBuffer, x: Int, y: Int) -> RGB {
        CVPixelBufferLockBaseAddress(pb, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pb, .readOnly) }
        let base = CVPixelBufferGetBaseAddress(pb)!.assumingMemoryBound(to: UInt8.self)
        let o = y * CVPixelBufferGetBytesPerRow(pb) + x * 4
        return (Int(base[o + 2]), Int(base[o + 1]), Int(base[o]))
    }

    private static func pixelOf(_ image: CGImage, x: Int, y: Int) -> RGB {
        var data = [UInt8](repeating: 0, count: 4)
        let ctx = CGContext(data: &data, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        // Desenha a imagem de forma a que o píxel (x, y) [y a partir de cima] caia em (0, 0)
        ctx.draw(image, in: CGRect(x: -x, y: -(image.height - 1 - y), width: image.width, height: image.height))
        return (Int(data[0]), Int(data[1]), Int(data[2]))
    }
}
