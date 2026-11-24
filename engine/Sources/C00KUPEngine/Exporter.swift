import Foundation
import AVFoundation
import CoreMedia

// Junta o vídeo gravado (H.264, copiado sem perda) com o WAV do plugin (convertido
// para AAC) num MP4. O áudio é posicionado pela diferença entre as marcas de tempo.
struct ExportJob {
    let videoURL: URL
    let wavURL: URL
    let outputURL: URL
    /// Instante (em segundos, na linha de tempo do vídeo) da 1.ª amostra do WAV.
    /// Negativo = o áudio começou antes do vídeo (o início do WAV é cortado).
    let audioOffset: Double
}

enum Exporter {
    static func run(_ job: ExportJob,
                    progress: @escaping (Double) -> Void,
                    completion: @escaping (Result<URL, Error>) -> Void) {
        Task.detached(priority: .userInitiated) {
            do {
                let url = try await export(job, progress: progress)
                completion(.success(url))
            } catch {
                completion(.failure(error))
            }
        }
    }

    private static func export(_ job: ExportJob, progress: @escaping (Double) -> Void) async throws -> URL {
        let videoAsset = AVURLAsset(url: job.videoURL)
        let audioAsset = AVURLAsset(url: job.wavURL)

        guard let videoTrack = try await videoAsset.loadTracks(withMediaType: .video).first else {
            throw EngineError.message("O ficheiro de vídeo não tem imagem.")
        }
        guard let audioTrack = try await audioAsset.loadTracks(withMediaType: .audio).first else {
            throw EngineError.message("O WAV não tem áudio.")
        }

        let videoDuration = try await videoAsset.load(.duration)
        let audioDuration = try await audioAsset.load(.duration)
        let videoFormats = try await videoTrack.load(.formatDescriptions)

        // Parte do WAV que cai dentro do vídeo
        let audioStart = CMTime(seconds: max(0, -job.audioOffset), preferredTimescale: 48000)
        guard CMTimeCompare(audioStart, audioDuration) < 0, job.audioOffset < videoDuration.seconds else {
            throw EngineError.message("O áudio e o vídeo não se sobrepõem no tempo.")
        }

        // Leitores
        let videoReader = try AVAssetReader(asset: videoAsset)
        let videoOutput = AVAssetReaderTrackOutput(track: videoTrack, outputSettings: nil)
        videoOutput.alwaysCopiesSampleData = false
        videoReader.add(videoOutput)

        let audioReader = try AVAssetReader(asset: audioAsset)
        audioReader.timeRange = CMTimeRange(start: audioStart, end: audioDuration)
        let audioOutput = AVAssetReaderTrackOutput(track: audioTrack, outputSettings: [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVLinearPCMBitDepthKey: 32,
            AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
            AVSampleRateKey: 48000,
            AVNumberOfChannelsKey: 2,
        ])
        audioReader.add(audioOutput)

        // Escritor MP4 (num ficheiro temporário, renomeado no fim)
        let tempURL = job.outputURL.deletingLastPathComponent()
            .appendingPathComponent(".a-exportar-" + job.outputURL.lastPathComponent)
        try FileManager.default.createDirectory(at: tempURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? FileManager.default.removeItem(at: tempURL)

        let writer = try AVAssetWriter(outputURL: tempURL, fileType: .mp4)
        writer.shouldOptimizeForNetworkUse = true

        let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: nil, sourceFormatHint: videoFormats.first)
        videoInput.expectsMediaDataInRealTime = false

        let audioInput = AVAssetWriterInput(mediaType: .audio, outputSettings: [
            AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVSampleRateKey: 48000,
            AVNumberOfChannelsKey: 2,
            AVEncoderBitRateKey: 256_000,
        ])
        audioInput.expectsMediaDataInRealTime = false

        guard writer.canAdd(videoInput), writer.canAdd(audioInput) else {
            throw EngineError.message("Não foi possível preparar o MP4.")
        }
        writer.add(videoInput)
        writer.add(audioInput)

        guard videoReader.startReading() else {
            throw videoReader.error ?? EngineError.message("Não foi possível ler o vídeo.")
        }
        guard audioReader.startReading() else {
            throw audioReader.error ?? EngineError.message("Não foi possível ler o WAV.")
        }
        guard writer.startWriting() else {
            throw writer.error ?? EngineError.message("Não foi possível escrever o MP4.")
        }
        writer.startSession(atSourceTime: .zero)

        let offset = CMTime(seconds: job.audioOffset, preferredTimescale: 48000)
        let totalSeconds = max(videoDuration.seconds, 0.001)

        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let group = DispatchGroup()
            group.enter()
            group.enter()

            // Vídeo: cópia direta dos frames já comprimidos
            var videoFinished = false
            videoInput.requestMediaDataWhenReady(on: DispatchQueue(label: "export.video")) {
                while videoInput.isReadyForMoreMediaData && !videoFinished {
                    guard let sample = videoOutput.copyNextSampleBuffer() else {
                        videoFinished = true
                        break
                    }
                    if !videoInput.append(sample) {
                        videoFinished = true
                        break
                    }
                    progress(min(1, CMSampleBufferGetPresentationTimeStamp(sample).seconds / totalSeconds))
                }
                if videoFinished {
                    videoInput.markAsFinished()
                    group.leave()
                }
            }

            // Áudio: PCM do WAV -> AAC, com a marca de tempo deslocada
            var audioFinished = false
            audioInput.requestMediaDataWhenReady(on: DispatchQueue(label: "export.audio")) {
                while audioInput.isReadyForMoreMediaData && !audioFinished {
                    guard let sample = audioOutput.copyNextSampleBuffer() else {
                        audioFinished = true
                        break
                    }
                    guard let shifted = retime(sample, by: offset) else { continue }

                    if CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(shifted), videoDuration) >= 0 {
                        audioFinished = true   // já passou o fim do vídeo
                        break
                    }
                    if !audioInput.append(shifted) {
                        audioFinished = true
                        break
                    }
                }
                if audioFinished {
                    audioInput.markAsFinished()
                    group.leave()
                }
            }

            group.notify(queue: .global()) {
                continuation.resume()
            }
        }

        if videoReader.status == .failed {
            writer.cancelWriting()
            throw videoReader.error ?? EngineError.message("Erro a ler o vídeo.")
        }
        if audioReader.status == .failed {
            writer.cancelWriting()
            throw audioReader.error ?? EngineError.message("Erro a ler o WAV.")
        }
        if writer.status == .failed {
            throw writer.error ?? EngineError.message("Erro a escrever o MP4.")
        }

        writer.endSession(atSourceTime: videoDuration)
        await writer.finishWriting()

        guard writer.status == .completed else {
            throw writer.error ?? EngineError.message("Erro a terminar o MP4.")
        }

        try? FileManager.default.removeItem(at: job.outputURL)
        try FileManager.default.moveItem(at: tempURL, to: job.outputURL)
        progress(1)
        return job.outputURL
    }

    /// Cópia do sample buffer com todas as marcas de tempo deslocadas.
    private static func retime(_ sample: CMSampleBuffer, by offset: CMTime) -> CMSampleBuffer? {
        var count: CMItemCount = 0
        CMSampleBufferGetSampleTimingInfoArray(sample, entryCount: 0, arrayToFill: nil, entriesNeededOut: &count)
        guard count > 0 else { return nil }

        var timings = [CMSampleTimingInfo](repeating: CMSampleTimingInfo(), count: count)
        CMSampleBufferGetSampleTimingInfoArray(sample, entryCount: count, arrayToFill: &timings, entriesNeededOut: &count)

        for i in 0 ..< count {
            timings[i].presentationTimeStamp = CMTimeAdd(timings[i].presentationTimeStamp, offset)
            if timings[i].decodeTimeStamp.isValid {
                timings[i].decodeTimeStamp = CMTimeAdd(timings[i].decodeTimeStamp, offset)
            }
        }

        var result: CMSampleBuffer?
        CMSampleBufferCreateCopyWithNewTiming(allocator: kCFAllocatorDefault,
                                              sampleBuffer: sample,
                                              sampleTimingEntryCount: count,
                                              sampleTimingArray: &timings,
                                              sampleBufferOut: &result)
        return result
    }
}
