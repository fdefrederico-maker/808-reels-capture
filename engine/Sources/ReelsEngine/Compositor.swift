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
    /// Posição vertical do recorte do FL: 0 = topo da janela, 1 = fundo.
    var cropY: CGFloat = 0.5
    /// Zoom da janela do FL (1 = altura toda da janela visível).
    var flZoom: CGFloat = 1.0
    var cameraEnabled = true
    var mirrorCamera = true
    var backgroundMode: BackgroundMode = .original
    var backgroundColor = CIColor(red: 0.07, green: 0.07, blue: 0.09)
    var backgroundImage: CIImage?

    /// Ganho automático de brilho da câmara (1 = sem alteração). Ajustado aos poucos.
    private(set) var cameraGain: CGFloat = 1
    var autoExposure = true
    private var exposureFrameCounter = 0

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

    /// Imagem final (1080x1920) a partir das imagens originais (usado nos testes).
    func compose(fl: CVPixelBuffer?, camera: CameraFrame?) -> CIImage {
        composeBands(fl: fl.flatMap { renderFLBand($0) },
                     camera: camera.flatMap { renderCameraBand($0) })
    }

    /// Imagem final (1080x1920) a partir das faixas já preparadas (FL e câmara).
    func composeBands(fl: CVPixelBuffer?, camera: CVPixelBuffer?) -> CIImage {
        var result = CIImage(color: CIColor(red: 0.04, green: 0.04, blue: 0.05)).cropped(to: outputRect)

        if let fl {
            result = Compositor.fill(CIImage(cvPixelBuffer: fl), into: flRect, alignX: 0.5).composited(over: result)
        }

        if cameraEnabled {
            if let camera {
                result = Compositor.fill(CIImage(cvPixelBuffer: camera), into: cameraRect, alignX: 0.5)
                    .composited(over: result)
            } else {
                let placeholder = CIImage(color: CIColor(red: 0.12, green: 0.12, blue: 0.14)).cropped(to: cameraRect)
                result = placeholder.composited(over: result)
            }
        }

        return result.cropped(to: outputRect)
    }

    // MARK: - Faixas (preparadas logo que cada imagem chega)

    private var flPool: CVPixelBufferPool?
    private var flPoolSize: CGSize = .zero
    private var cameraPool: CVPixelBufferPool?

    /// Recorta/amplia a janela do FL para o tamanho da faixa de baixo (ou do ecrã todo).
    func renderFLBand(_ frame: CVPixelBuffer) -> CVPixelBuffer? {
        let size = flRect.size
        if flPool == nil || flPoolSize != size {
            flPool = Compositor.makePool(size)
            flPoolSize = size
        }
        guard let pool = flPool, let out = Compositor.newBuffer(from: pool) else { return nil }

        let local = CGRect(origin: .zero, size: size)
        let background = CIImage(color: CIColor(red: 0.04, green: 0.04, blue: 0.05)).cropped(to: local)
        // alignY do Core Image conta a partir de baixo, por isso inverte-se
        let image = Compositor.fill(CIImage(cvPixelBuffer: frame), into: local,
                                    alignX: cropX, alignY: 1 - cropY, zoom: flZoom)
        context.render(image.composited(over: background), to: out, bounds: local, colorSpace: colorSpace)
        return out
    }

    /// Câmara com o fundo já trocado, no tamanho da faixa de cima.
    func renderCameraBand(_ frame: CameraFrame) -> CVPixelBuffer? {
        if cameraPool == nil {
            cameraPool = Compositor.makePool(cameraRect.size)
        }
        guard let pool = cameraPool, let out = Compositor.newBuffer(from: pool) else { return nil }

        let local = CGRect(origin: .zero, size: cameraRect.size)
        context.render(composeCamera(frame, in: local), to: out, bounds: local, colorSpace: colorSpace)
        return out
    }

    private static func makePool(_ size: CGSize) -> CVPixelBufferPool? {
        var pool: CVPixelBufferPool?
        let attributes: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: Int(size.width),
            kCVPixelBufferHeightKey as String: Int(size.height),
            kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any](),
        ]
        CVPixelBufferPoolCreate(kCFAllocatorDefault, nil, attributes as CFDictionary, &pool)
        return pool
    }

    private static func newBuffer(from pool: CVPixelBufferPool) -> CVPixelBuffer? {
        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &buffer)
        if let buffer {
            CVBufferSetAttachment(buffer, kCVImageBufferCGColorSpaceKey,
                                  CGColorSpace(name: CGColorSpace.sRGB)!, .shouldPropagate)
        }
        return buffer
    }

    /// Mede o brilho médio da câmara (de vez em quando) e ajusta o ganho para um nível agradável.
    private func updateExposure(_ source: CIImage) {
        exposureFrameCounter += 1
        guard autoExposure, exposureFrameCounter % 6 == 1 else { return }

        guard let average = CIFilter(name: "CIAreaAverage", parameters: [
            kCIInputImageKey: source,
            kCIInputExtentKey: CIVector(cgRect: source.extent),
        ])?.outputImage else { return }

        var pixel = [UInt8](repeating: 0, count: 4)
        context.render(average, toBitmap: &pixel, rowBytes: 4,
                       bounds: CGRect(x: 0, y: 0, width: 1, height: 1),
                       format: .RGBA8, colorSpace: colorSpace)
        let luminance = (0.2126 * CGFloat(pixel[0]) + 0.7152 * CGFloat(pixel[1]) + 0.0722 * CGFloat(pixel[2])) / 255

        let wanted = min(max(0.36 / max(luminance, 0.01), 1), 4)    // nunca escurece; no máximo 4x
        cameraGain += (wanted - cameraGain) * 0.3                   // muda devagar (sem piscar)
    }

    /// Câmara com o brilho corrigido (e menos grão quando é preciso aclarar muito).
    private func exposed(_ source: CIImage) -> CIImage {
        updateExposure(source)
        guard cameraGain > 1.05 else { return source }

        var image = source.applyingFilter("CIExposureAdjust", parameters: [kCIInputEVKey: log2(cameraGain)])
        if cameraGain > 1.6 {
            image = image.applyingFilter("CINoiseReduction", parameters: [
                "inputNoiseLevel": 0.03,
                "inputSharpness": 0.5,
            ])
        }
        return image.cropped(to: source.extent)
    }

    private func composeCamera(_ frame: CameraFrame, in rect: CGRect) -> CIImage {
        let source = exposed(CIImage(cvPixelBuffer: frame.image))
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
    static func fill(_ image: CIImage, into rect: CGRect, alignX: CGFloat, alignY: CGFloat = 0.5,
                     zoom: CGFloat = 1, mirror: Bool = false) -> CIImage {
        let e = image.extent
        guard e.width > 0, e.height > 0 else { return CIImage.empty() }

        let scale = max(rect.width / e.width, rect.height / e.height) * min(max(zoom, 1), 8)
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
