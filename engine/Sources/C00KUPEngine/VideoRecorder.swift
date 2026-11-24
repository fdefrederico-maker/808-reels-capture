import Foundation
import AVFoundation
import CoreVideo

enum EngineError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self {
        case .message(let text): return text
        }
    }
}

// Grava o vídeo (só imagem) H.264 1080x1920 a 30 fps em tempo real.
// O frame n corresponde ao instante  startTicks + n/30 s  no relógio do sistema.
final class VideoRecorder {
    static let fps: Int32 = 30

    let url: URL
    let startTicks: UInt64
    private(set) var nextFrameIndex: Int64 = 0
    private(set) var lastWrittenIndex: Int64 = -1
    private(set) var droppedFrames = 0

    private let writer: AVAssetWriter
    private let input: AVAssetWriterInput
    private let adaptor: AVAssetWriterInputPixelBufferAdaptor

    init(url: URL, startTicks: UInt64) throws {
        self.url = url
        self.startTicks = startTicks

        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? FileManager.default.removeItem(at: url)

        writer = try AVAssetWriter(outputURL: url, fileType: .mov)

        let size = Compositor.outputSize
        let compression: [String: Any] = [
            AVVideoAverageBitRateKey: 12_000_000,
            AVVideoExpectedSourceFrameRateKey: Int(VideoRecorder.fps),
            AVVideoMaxKeyFrameIntervalKey: Int(VideoRecorder.fps),
            AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            AVVideoAllowFrameReorderingKey: false,
        ]
        let settings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: Int(size.width),
            AVVideoHeightKey: Int(size.height),
            AVVideoCompressionPropertiesKey: compression,
            AVVideoColorPropertiesKey: [
                AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
                AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
                AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2,
            ],
        ]

        input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
        input.expectsMediaDataInRealTime = true

        adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: Int(size.width),
            kCVPixelBufferHeightKey as String: Int(size.height),
            kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any](),
        ])

        guard writer.canAdd(input) else { throw EngineError.message("Não foi possível preparar o vídeo.") }
        writer.add(input)

        guard writer.startWriting() else {
            throw writer.error ?? EngineError.message("Não foi possível começar a gravar o vídeo.")
        }
        writer.startSession(atSourceTime: .zero)
    }

    /// Instante (relógio do sistema) que o frame `index` representa.
    func ticks(forFrame index: Int64) -> UInt64 {
        startTicks &+ HostClock.ticks(seconds: Double(index) / Double(VideoRecorder.fps))
    }

    /// Índice do frame que corresponde ao instante `ticks`.
    func targetFrameIndex(at ticks: UInt64) -> Int64 {
        let elapsed = HostClock.seconds(from: startTicks, to: ticks)
        return elapsed < 0 ? -1 : Int64((elapsed * Double(VideoRecorder.fps)).rounded(.down))
    }

    /// Escreve o próximo frame; `render` desenha a imagem no buffer fornecido.
    func appendNextFrame(waitIfBusy: Bool = false, render: (CVPixelBuffer) -> Void) {
        let index = nextFrameIndex
        nextFrameIndex += 1

        if waitIfBusy {   // só para o autoteste (não em tempo real)
            var waited = 0
            while !input.isReadyForMoreMediaData && writer.status == .writing && waited < 2000 {
                Thread.sleep(forTimeInterval: 0.005)
                waited += 1
            }
        }

        guard writer.status == .writing,
              input.isReadyForMoreMediaData,
              let pool = adaptor.pixelBufferPool
        else {
            droppedFrames += 1
            return
        }

        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &buffer)
        guard let pixelBuffer = buffer else {
            droppedFrames += 1
            return
        }

        render(pixelBuffer)

        if adaptor.append(pixelBuffer, withPresentationTime: CMTime(value: index, timescale: VideoRecorder.fps)) {
            lastWrittenIndex = index
        } else {
            droppedFrames += 1
        }
    }

    /// Salta frames em atraso (ex.: o Mac esteve bloqueado) sem os escrever.
    func skip(to index: Int64) {
        if index > nextFrameIndex {
            droppedFrames += Int(index - nextFrameIndex)
            nextFrameIndex = index
        }
    }

    func finish(completion: @escaping (Result<URL, Error>) -> Void) {
        guard lastWrittenIndex >= 0 else {
            writer.cancelWriting()
            completion(.failure(EngineError.message("Não foi gravado nenhum frame de vídeo.")))
            return
        }

        input.markAsFinished()
        writer.endSession(atSourceTime: CMTime(value: lastWrittenIndex + 1, timescale: VideoRecorder.fps))

        let writer = self.writer
        let url = self.url
        writer.finishWriting {
            if writer.status == .completed {
                completion(.success(url))
            } else {
                completion(.failure(writer.error ?? EngineError.message("Falha ao fechar o vídeo.")))
            }
        }
    }

    func cancel() {
        writer.cancelWriting()
        try? FileManager.default.removeItem(at: url)
    }
}
