import Foundation
import CoreImage
import CoreVideo
import ImageIO

// Monta a imagem vertical 1080x1920. A pré-visualização é a MESMA imagem,
// apenas reduzida, por isso o que se vê no plugin é igual ao vídeo final.
final class Compositor {
    static let outputSize = CGSize(width: 1080, height: 1920)
    static let previewScale: CGFloat = 0.25             // 270 x 480
    static var previewSize: CGSize {
        CGSize(width: outputSize.width * previewScale, height: outputSize.height * previewScale)
    }

    private let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    private let context: CIContext

    /// Posição horizontal do recorte: 0 = lado esquerdo da janela do FL, 1 = lado direito.
    var cropX: CGFloat = 0.5

    init() {
        context = CIContext(options: [
            .workingColorSpace: colorSpace,
            .outputColorSpace: colorSpace,
            .cacheIntermediates: false,
        ])
    }

    private var outputRect: CGRect { CGRect(origin: .zero, size: Compositor.outputSize) }

    /// Imagem final (no espaço 1080x1920) a partir da última captura do FL.
    func compose(_ frame: CVPixelBuffer?) -> CIImage {
        let background = CIImage(color: CIColor(red: 0.04, green: 0.04, blue: 0.05)).cropped(to: outputRect)

        guard let frame else { return background }

        let source = CIImage(cvPixelBuffer: frame)
        let extent = source.extent
        guard extent.width > 0, extent.height > 0 else { return background }

        // "Preencher": a janela ocupa a altura toda e é cortada nos lados
        let size = Compositor.outputSize
        let scale = max(size.width / extent.width, size.height / extent.height)
        let scaledWidth = extent.width * scale
        let scaledHeight = extent.height * scale
        let offsetX = -(scaledWidth - size.width) * min(max(cropX, 0), 1)
        let offsetY = -(scaledHeight - size.height) / 2

        let transform = CGAffineTransform(translationX: -extent.minX, y: -extent.minY)
            .concatenating(CGAffineTransform(scaleX: scale, y: scale))
            .concatenating(CGAffineTransform(translationX: offsetX, y: offsetY))

        return source.transformed(by: transform)
            .composited(over: background)
            .cropped(to: outputRect)
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
