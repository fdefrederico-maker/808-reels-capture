import Foundation
import AVFoundation
import CoreImage
import CoreVideo
import ImageIO

// Autoteste executado no GitHub Actions:  ReelsEngine --selftest
// Verifica o recorte vertical, a pré-visualização, a gravação H.264,
// a exportação MP4 e a sincronização áudio/vídeo — sem precisar de ecrã.
enum SelfTest {
    private static var failures = 0

    private static func check(_ condition: Bool, _ message: String) {
        print((condition ? "OK    " : "FALHA ") + message)
        if !condition { failures += 1 }
    }

    static func run() -> Int32 {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("808-selftest-\(getpid())")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)

        testCompositor()
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
        for (cropX, expected) in [(0.0, "vermelho"), (0.5, "verde"), (1.0, "azul")] {
            compositor.cropX = CGFloat(cropX)
            let image = compositor.compose(source)
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
                check(cg.width == 270 && cg.height == 480, "pré-visualização \(cg.width)x\(cg.height) (esperado 270x480)")
                let p = pixelOf(cg, x: 135, y: 240)
                check(colourName(p) == expected, "pré-visualização x=\(cropX): centro \(colourName(p)) igual ao vídeo")
                let t = pixelOf(cg, x: 135, y: 4)
                check(t.r > 200 && t.g > 200 && t.b > 200, "pré-visualização x=\(cropX): topo branco")
            } else {
                check(false, "pré-visualização JPEG não gerada")
            }
        }
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
