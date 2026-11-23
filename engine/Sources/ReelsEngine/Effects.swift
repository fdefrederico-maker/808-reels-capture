import Foundation
import CoreImage

// Efeitos retro aplicados à imagem final 1080x1920 (vídeo e pré-visualização).
// Só usa filtros que já vêm com o macOS (Core Image).
final class Effects {
    enum Kind: String {
        case none, vhs, film, glitch, tv
    }

    struct Context {
        /// Segundos desde o arranque do motor (anima ruído, tremor, etc.).
        let time: Double
        /// Segundos de gravação (nil = não está a gravar).
        let recordingSeconds: Double?
    }

    var kind: Kind = .none

    private let rect = CGRect(origin: .zero, size: Compositor.outputSize)
    private var textCache: [String: CIImage] = [:]
    private lazy var scanlinesLight = makeScanlines(alpha: 0.16, period: 4)
    private lazy var scanlinesStrong = makeScanlines(alpha: 0.32, period: 5)

    // Ritmo (graves vindos do plugin)
    private var incomingLevel: CGFloat = 0
    private var slowAverage: CGFloat = 0
    private var punch: CGFloat = 0
    private var runningMax: CGFloat = 0.1
    private var lastTime: Double = 0

    /// Nível dos graves (0...1) enviado pelo plugin ~30x por segundo.
    func pushLevel(_ value: Double) {
        incomingLevel = max(incomingLevel, CGFloat(max(0, min(value, 2))))
    }

    /// Intensidade atual do "soco" do glitch (0...1), já normalizada ao volume da música.
    private(set) var beatAmount: CGFloat = 0

    func apply(_ input: CIImage, context: Context) -> CIImage {
        updateEnvelope(time: context.time)

        switch kind {
        case .none:   return input
        case .vhs:    return vhs(input, context)
        case .film:   return film(input, context)
        case .glitch: return glitch(input, context)
        case .tv:     return tv(input, context)
        }
    }

    // MARK: - Envolvente dos graves

    /// Reage aos ATAQUES (início de cada kick/808), não ao volume contínuo:
    /// compara o nível atual com a média dos últimos ~0,4 s.
    private func updateEnvelope(time: Double) {
        let dt = max(0, min(time - lastTime, 0.25))
        lastTime = time

        let level = incomingLevel
        incomingLevel = 0

        runningMax = max(level, runningMax * CGFloat(pow(0.85, dt)))     // adapta-se ao volume da música
        runningMax = max(runningMax, 0.05)

        let hit = max(0, level - slowAverage) / runningMax
        slowAverage += (level - slowAverage) * CGFloat(1 - exp(-dt / 0.4))

        punch = max(hit, punch * CGFloat(pow(0.002, dt)))                 // o "soco" dura ~0,25 s
        beatAmount = clamp((punch - 0.15) / 0.5)
    }

    // MARK: - VHS

    private func vhs(_ input: CIImage, _ c: Context) -> CIImage {
        let f = frameIndex(c)

        var img = input
            .clampedToExtent()
            .applyingGaussianBlur(sigma: 1.3)
            .cropped(to: rect)
            .applyingFilter("CIColorControls", parameters: [
                kCIInputSaturationKey: 1.18,
                kCIInputContrastKey: 1.06,
                kCIInputBrightnessKey: 0.01,
            ])

        img = rgbSplit(img, dx: 4, dy: 0)

        // Faixa de "tracking" a descer pelo ecrã
        let cycle = Double(rect.height + 600)
        let bandY = rect.height + 300 - CGFloat((c.time * 260).truncatingRemainder(dividingBy: cycle))
        let bandRect = CGRect(x: 0, y: bandY, width: rect.width, height: 50)
        let band = img.clampedToExtent()
            .transformed(by: CGAffineTransform(translationX: 16, y: 0))
            .cropped(to: bandRect)
            .applyingFilter("CIColorControls", parameters: [kCIInputBrightnessKey: 0.1, kCIInputSaturationKey: 0.5])
        img = band.composited(over: img)

        img = scanlinesLight.composited(over: img)
        img = noise(alpha: 0.07, scale: 1, seed: f).composited(over: img)

        // Texto de câmara antiga
        let recording = c.recordingSeconds != nil
        let blinkOn = Int(c.time * 2) % 2 == 0
        let top = rect.height - 70

        if recording {
            img = text("REC", size: 46).transformed(by: CGAffineTransform(translationX: 112, y: top)).composited(over: img)
            if blinkOn {
                img = redDot(center: CGPoint(x: 78, y: top + 22), radius: 16).composited(over: img)
            }
        } else {
            img = text("PLAY  >", size: 46).transformed(by: CGAffineTransform(translationX: 60, y: top)).composited(over: img)
        }

        let seconds = Int(c.recordingSeconds ?? 0)
        let counter = String(format: "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60)
        img = text("SP  " + counter, size: 40).transformed(by: CGAffineTransform(translationX: 60, y: 70)).composited(over: img)

        let date = Effects.vhsDate()
        let dateImage = text(date, size: 40)
        img = dateImage.transformed(by: CGAffineTransform(translationX: rect.width - 60 - dateImage.extent.width, y: 70))
            .composited(over: img)

        return img.cropped(to: rect)
    }

    private static let dateFormatter: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "MMM. dd"
        return f
    }()

    private static func vhsDate() -> String {
        dateFormatter.string(from: Date()).uppercased() + " 1998"
    }

    // MARK: - Filme 8mm

    private func film(_ input: CIImage, _ c: Context) -> CIImage {
        let f = frameIndex(c)
        let flicker = (rnd(f, 1) - 0.5) * 0.04

        // Tremor do projetor
        let jx = CGFloat(sin(c.time * 7.0)) * 2 + (rnd(f, 2) - 0.5) * 3
        let jy = CGFloat(cos(c.time * 5.0)) * 2 + (rnd(f, 3) - 0.5) * 3

        var img = input
            .applyingFilter("CISepiaTone", parameters: [kCIInputIntensityKey: 0.35])
            .applyingFilter("CIColorControls", parameters: [
                kCIInputSaturationKey: 0.8,
                kCIInputContrastKey: 0.92,
                kCIInputBrightnessKey: 0.02 + flicker,
            ])
            .applyingFilter("CIColorMatrix", parameters: [
                "inputBiasVector": CIVector(x: 0.05, y: 0.035, z: 0.015, w: 0),   // pretos levantados e quentes
            ])
            .clampedToExtent()
            .transformed(by: CGAffineTransform(translationX: jx, y: jy))
            .applyingGaussianBlur(sigma: 0.8)
            .cropped(to: rect)

        img = img.applyingFilter("CIVignetteEffect", parameters: [
            kCIInputCenterKey: CIVector(x: rect.midX, y: rect.midY),
            kCIInputRadiusKey: 1150,
            kCIInputIntensityKey: 0.9,
        ]).cropped(to: rect)

        img = noise(alpha: 0.11, scale: 1.7, seed: f).composited(over: img)

        // Riscos ocasionais na película
        if rnd(f, 4) > 0.75 {
            let x = rnd(f, 5) * rect.width
            let scratch = CIImage(color: CIColor(red: 1, green: 0.95, blue: 0.85, alpha: 0.22))
                .cropped(to: CGRect(x: x, y: 0, width: 2, height: rect.height))
            img = scratch.composited(over: img)
        }
        if rnd(f, 6) > 0.9 {
            let p = CGPoint(x: rnd(f, 7) * rect.width, y: rnd(f, 8) * rect.height)
            let dust = CIImage(color: CIColor(red: 0.05, green: 0.04, blue: 0.03, alpha: 0.6))
                .cropped(to: CGRect(x: p.x, y: p.y, width: 5 + rnd(f, 9) * 8, height: 4 + rnd(f, 10) * 6))
            img = dust.composited(over: img)
        }

        return img.cropped(to: rect)
    }

    // MARK: - Glitch ao ritmo

    private func glitch(_ input: CIImage, _ c: Context) -> CIImage {
        let k = beatAmount
        guard k > 0.02 else { return input }

        let f = frameIndex(c)

        // "Soco" de zoom
        let s = 1 + 0.07 * k
        var img = input.clampedToExtent()
            .transformed(by: CGAffineTransform(translationX: -rect.midX, y: -rect.midY)
                .concatenating(CGAffineTransform(scaleX: s, y: s))
                .concatenating(CGAffineTransform(translationX: rect.midX, y: rect.midY)))
            .cropped(to: rect)

        img = rgbSplit(img, dx: 24 * k, dy: 4 * k)

        // Fatias deslocadas
        if k > 0.4 {
            let count = 3 + Int(6 * k)
            let source = img.clampedToExtent()
            for i in 0 ..< count {
                let y = rnd(f, 20 + i) * rect.height
                let h = 12 + rnd(f, 40 + i) * 110
                let dx = (rnd(f, 60 + i) - 0.5) * 160 * k
                let slice = source
                    .transformed(by: CGAffineTransform(translationX: dx, y: 0))
                    .cropped(to: CGRect(x: 0, y: y, width: rect.width, height: h))
                img = slice.composited(over: img)
            }
        }

        // Flash de cor invertida nos golpes mais fortes
        if k > 0.85 && rnd(f, 90) > 0.6 {
            let y = rnd(f, 91) * rect.height
            let flash = img.applyingFilter("CIColorInvert")
                .cropped(to: CGRect(x: 0, y: y, width: rect.width, height: 40 + rnd(f, 92) * 120))
            img = flash.composited(over: img)
        }

        return img.cropped(to: rect)
    }

    // MARK: - TV antiga (preto e branco)

    private func tv(_ input: CIImage, _ c: Context) -> CIImage {
        let f = frameIndex(c)
        let flicker = (rnd(f, 1) - 0.5) * 0.05

        var img = input
            .applyingFilter("CIColorControls", parameters: [
                kCIInputSaturationKey: 0,
                kCIInputContrastKey: 1.35,
                kCIInputBrightnessKey: flicker,
            ])
            .clampedToExtent()
            .applyingFilter("CIBumpDistortion", parameters: [
                kCIInputCenterKey: CIVector(x: rect.midX, y: rect.midY),
                kCIInputRadiusKey: 1400,
                kCIInputScaleKey: 0.16,
            ])
            .applyingGaussianBlur(sigma: 0.9)
            .cropped(to: rect)

        img = scanlinesStrong.composited(over: img)
        img = noise(alpha: 0.06, scale: 1, seed: f).composited(over: img)
        img = img.applyingFilter("CIVignetteEffect", parameters: [
            kCIInputCenterKey: CIVector(x: rect.midX, y: rect.midY),
            kCIInputRadiusKey: 1050,
            kCIInputIntensityKey: 1.2,
        ]).cropped(to: rect)

        // Cantos arredondados do ecrã de tubo
        if let mask = CIFilter(name: "CIRoundedRectangleGenerator", parameters: [
            "inputExtent": CIVector(cgRect: rect.insetBy(dx: 24, dy: 24)),
            "inputRadius": 80,
            "inputColor": CIColor(red: 1, green: 1, blue: 1),
        ])?.outputImage {
            let black = CIImage(color: CIColor(red: 0, green: 0, blue: 0)).cropped(to: rect)
            img = img.applyingFilter("CIBlendWithMask", parameters: [
                kCIInputBackgroundImageKey: black,
                kCIInputMaskImageKey: mask.composited(over: CIImage(color: CIColor(red: 0, green: 0, blue: 0)).cropped(to: rect)),
            ])
        }

        return img.cropped(to: rect)
    }

    // MARK: - Peças reutilizáveis

    private func frameIndex(_ c: Context) -> Int { Int(c.time * 30) }

    private func clamp(_ v: CGFloat) -> CGFloat { min(max(v, 0), 1) }

    /// Número pseudo-aleatório 0...1, estável para o mesmo frame (pré-visualização = vídeo).
    private func rnd(_ frame: Int, _ salt: Int) -> CGFloat {
        var x = UInt64(bitPattern: Int64(frame &* 73_856_093 ^ salt &* 19_349_663))
        x ^= x >> 33
        x = x &* 0xff51afd7ed558ccd
        x ^= x >> 33
        x = x &* 0xc4ceb9fe1a85ec53
        x ^= x >> 33
        return CGFloat(x % 10_000) / 10_000
    }

    private func makeScanlines(alpha: CGFloat, period: CGFloat) -> CIImage {
        guard let stripes = CIFilter(name: "CIStripesGenerator", parameters: [
            "inputCenter": CIVector(x: 0, y: 0),
            "inputColor0": CIColor(red: 0, green: 0, blue: 0, alpha: alpha),
            "inputColor1": CIColor(red: 0, green: 0, blue: 0, alpha: 0),
            "inputWidth": period / 2,
            "inputSharpness": 0.6,
        ])?.outputImage else { return CIImage.empty() }

        // As riscas do gerador são verticais: rodar 90° para ficarem horizontais
        return stripes.transformed(by: CGAffineTransform(rotationAngle: .pi / 2)).cropped(to: rect)
    }

    private func noise(alpha: CGFloat, scale: CGFloat, seed: Int) -> CIImage {
        guard let random = CIFilter(name: "CIRandomGenerator")?.outputImage else { return CIImage.empty() }
        let ox = rnd(seed, 101) * 1000, oy = rnd(seed, 102) * 1000
        let grey = CIVector(x: 0.33, y: 0.33, z: 0.33, w: 0)
        return random
            .transformed(by: CGAffineTransform(translationX: -ox, y: -oy))
            .transformed(by: CGAffineTransform(scaleX: scale, y: scale))
            .applyingFilter("CIColorMatrix", parameters: [
                "inputRVector": grey,
                "inputGVector": grey,
                "inputBVector": grey,
                "inputAVector": CIVector(x: 0, y: 0, z: 0, w: 0),
                "inputBiasVector": CIVector(x: 0, y: 0, z: 0, w: alpha),
            ])
            .cropped(to: rect)
    }

    /// Separa os canais vermelho e azul (aberração cromática).
    private func rgbSplit(_ img: CIImage, dx: CGFloat, dy: CGFloat) -> CIImage {
        let base = img.clampedToExtent()
        func channel(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> CIImage {
            base.applyingFilter("CIColorMatrix", parameters: [
                "inputRVector": CIVector(x: r, y: 0, z: 0, w: 0),
                "inputGVector": CIVector(x: 0, y: g, z: 0, w: 0),
                "inputBVector": CIVector(x: 0, y: 0, z: b, w: 0),
                "inputAVector": CIVector(x: 0, y: 0, z: 0, w: 1),
            ])
        }
        let red = channel(1, 0, 0).transformed(by: CGAffineTransform(translationX: dx, y: dy))
        let green = channel(0, 1, 0)
        let blue = channel(0, 0, 1).transformed(by: CGAffineTransform(translationX: -dx, y: -dy))

        return red
            .applyingFilter("CIMaximumCompositing", parameters: [kCIInputBackgroundImageKey: green])
            .applyingFilter("CIMaximumCompositing", parameters: [kCIInputBackgroundImageKey: blue])
            .cropped(to: rect)
    }

    /// Texto branco com sombra (estilo ecrã de câmara de vídeo).
    private func text(_ string: String, size: CGFloat) -> CIImage {
        let key = "\(size)|\(string)"
        if let cached = textCache[key] { return cached }

        guard let raw = CIFilter(name: "CITextImageGenerator", parameters: [
            "inputText": string,
            "inputFontName": "Menlo-Bold",
            "inputFontSize": size,
            "inputScaleFactor": 1.0,
        ])?.outputImage else { return CIImage.empty() }

        func tinted(_ v: CGFloat) -> CIImage {
            raw.applyingFilter("CIColorMatrix", parameters: [
                "inputRVector": CIVector(x: 0, y: 0, z: 0, w: 0),
                "inputGVector": CIVector(x: 0, y: 0, z: 0, w: 0),
                "inputBVector": CIVector(x: 0, y: 0, z: 0, w: 0),
                "inputBiasVector": CIVector(x: v, y: v, z: v, w: 0),
            ])
        }

        let e = raw.extent
        let normalised = CGAffineTransform(translationX: -e.minX, y: -e.minY)
        let shadow = tinted(0).transformed(by: normalised.concatenating(CGAffineTransform(translationX: 3, y: -3)))
        let result = tinted(1).transformed(by: normalised).composited(over: shadow)

        if textCache.count > 40 { textCache.removeAll() }
        textCache[key] = result
        return result
    }

    private func redDot(center: CGPoint, radius: CGFloat) -> CIImage {
        guard let dot = CIFilter(name: "CIRadialGradient", parameters: [
            "inputCenter": CIVector(x: center.x, y: center.y),
            "inputRadius0": radius - 1.5,
            "inputRadius1": radius,
            "inputColor0": CIColor(red: 1, green: 0.1, blue: 0.1, alpha: 1),
            "inputColor1": CIColor(red: 1, green: 0.1, blue: 0.1, alpha: 0),
        ])?.outputImage else { return CIImage.empty() }
        return dot.cropped(to: CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2))
    }
}
