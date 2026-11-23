import Foundation
import CoreImage
import CoreVideo
import ImageIO

// Monta a imagem vertical 1080x1920:
//   - câmara numa faixa no topo (opcional), com o fundo da sala trocado se pedido;
//   - janela do FL Studio a ocupar o resto.
// A pré-visualização é a MESMA imagem, apenas reduzida.
final class Compositor {
    enum BackgroundMode: String {
        case original, blur, color, image
    }

    static let outputSize = CGSize(width: 1080, height: 1920)
    static let cameraBandHeight: CGFloat = 600            // ~31% do topo, como nos reels de produtores
    static let previewScale: CGFloat = 0.25               // 270 x 480
    static var previewSize: CGSize {
        CGSize(width: outputSize.width * previewScale, height: outputSize.height * previewScale)
    }

    private let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    private let context: CIContext

    /// Posição horizontal do recorte do FL: 0 = esquerda, 1 = direita.
    var cropX: CGFloat = 0.5
    var cameraEnabled = true
    var mirrorCamera = true
    var backgroundMode: BackgroundMode = .original
    var backgroundColor = CIColor(red: 0.07, green: 0.07, blue: 0.09)
    var backgroundImage: CIImage?

    init() {
        context = CIContext(options: [
            .workingColorSpace: colorSpace,
            .outputColorSpace: colorSpace,
            .cacheIntermediates: false,
        ])
    }

    private var outputRect: CGRect { CGRect(origin: .zero, size: Compositor.outputSize) }

    // Coordenadas do Core Image: origem no canto INFERIOR esquerdo.
    var cameraRect: CGRect {
        CGRect(x: 0, y: Compositor.outputSize.height - Compositor.cameraBandHeight,
               width: Compositor.outputSize.width, height: Compositor.cameraBandHeight)
    }

    var flRect: CGRect {
        cameraEnabled
            ? CGRect(x: 0, y: 0, width: Compositor.outputSize.width, height: Compositor.outputSize.height - Compositor.cameraBandHeight)
            : outputRect
    }

    /// Imagem final (1080x1920).
    func compose(fl: CVPixelBuffer?, camera: CameraFrame?) -> CIImage {
        var result = CIImage(color: CIColor(red: 0.04, green: 0.04, blue: 0.05)).cropped(to: outputRect)

        if let fl {
            let image = Compositor.fill(CIImage(cvPixelBuffer: fl), into: flRect, alignX: cropX)
            result = image.composited(over: result)
        }

        if cameraEnabled {
            if let camera {
                result = composeCamera(camera).composited(over: result)
            } else {
                let placeholder = CIImage(color: CIColor(red: 0.12, green: 0.12, blue: 0.14)).cropped(to: cameraRect)
                result = placeholder.composited(over: result)
            }
        }

        return result.cropped(to: outputRect)
    }

    private func composeCamera(_ frame: CameraFrame) -> CIImage {
        let rect = cameraRect
        let source = CIImage(cvPixelBuffer: frame.image)
        let camera = Compositor.fill(source, into: rect, alignX: 0.5, mirror: mirrorCamera)

        guard backgroundMode != .original, let maskBuffer = frame.mask else { return camera }

        // A máscara tem outra resolução: esticá-la ao tamanho da câmara e aplicar
        // exatamente a mesma transformação que à imagem.
        var mask = CIImage(cvPixelBuffer: maskBuffer)
        let e = source.extent, m = mask.extent
        mask = mask.transformed(by: CGAffineTransform(translationX: -m.minX, y: -m.minY)
            .concatenating(CGAffineTransform(scaleX: e.width / m.width, y: e.height / m.height))
            .concatenating(CGAffineTransform(translationX: e.minX, y: e.minY)))
        mask = Compositor.fill(mask, into: rect, alignX: 0.5, mirror: mirrorCamera)
            .clampedToExtent()
            .applyingGaussianBlur(sigma: 1.5)        // contorno suave
            .cropped(to: rect)

        let background: CIImage
        switch backgroundMode {
        case .blur:
            background = camera.clampedToExtent().applyingGaussianBlur(sigma: 28).cropped(to: rect)
        case .image:
            if let backgroundImage {
                background = Compositor.fill(backgroundImage, into: rect, alignX: 0.5)
            } else {
                background = CIImage(color: backgroundColor).cropped(to: rect)
            }
        case .color, .original:
            background = CIImage(color: backgroundColor).cropped(to: rect)
        }

        return camera.applyingFilter("CIBlendWithMask", parameters: [
            kCIInputBackgroundImageKey: background,
            kCIInputMaskImageKey: mask,
        ]).cropped(to: rect)
    }

    /// Escala a imagem para PREENCHER `rect` (cortando o que sobra) e posiciona-a.
    static func fill(_ image: CIImage, into rect: CGRect, alignX: CGFloat, alignY: CGFloat = 0.5, mirror: Bool = false) -> CIImage {
        let e = image.extent
        guard e.width > 0, e.height > 0 else { return CIImage.empty() }

        let scale = max(rect.width / e.width, rect.height / e.height)
        let excessX = e.width * scale - rect.width
        let excessY = e.height * scale - rect.height

        var t = CGAffineTransform(translationX: -e.minX, y: -e.minY)
        if mirror {
            t = t.concatenating(CGAffineTransform(scaleX: -1, y: 1))
                 .concatenating(CGAffineTransform(translationX: e.width, y: 0))
        }
        t = t.concatenating(CGAffineTransform(scaleX: scale, y: scale))
             .concatenating(CGAffineTransform(translationX: rect.minX - excessX * min(max(alignX, 0), 1),
                                              y: rect.minY - excessY * min(max(alignY, 0), 1)))

        return image.transformed(by: t).cropped(to: rect)
    }

    func loadBackgroundImage(path: String) -> Bool {
        let url = URL(fileURLWithPath: path)
        guard let image = CIImage(contentsOf: url, options: [.applyOrientationProperty: true]) else { return false }
        backgroundImage = image
        return true
    }

    func render(_ image: CIImage, to pixelBuffer: CVPixelBuffer) {
        context.render(image, to: pixelBuffer, bounds: outputRect, colorSpace: colorSpace)
    }

    func previewJPEG(_ image: CIImage) -> Data? {
        let small = image
            .applyingFilter("CILanczosScaleTransform", parameters: [
                kCIInputScaleKey: Compositor.previewScale,
                kCIInputAspectRatioKey: 1.0,
            ])
            .cropped(to: CGRect(origin: .zero, size: Compositor.previewSize))

        let quality = CIImageRepresentationOption(rawValue: kCGImageDestinationLossyCompressionQuality as String)
        return context.jpegRepresentation(of: small, colorSpace: colorSpace, options: [quality: 0.7])
    }
}
