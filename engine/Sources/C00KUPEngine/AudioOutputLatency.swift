import Foundation
import CoreAudio

// Atraso entre o som sair do programa e chegar aos ouvidos, para o dispositivo
// de saída atual do macOS (ex.: AirPods/Bluetooth ~150-250 ms, colunas com cabo ~5-15 ms).
// O motor soma-o à hora do áudio, para a imagem ficar alinhada com o que se OUVIU.
enum AudioOutputLatency {
    struct Info {
        let seconds: Double
        let deviceName: String
    }

    static func current() -> Info? {
        var deviceID = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)

        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &deviceID) == noErr,
              deviceID != 0 else { return nil }

        func uint32(_ selector: AudioObjectPropertySelector, _ object: AudioObjectID = deviceID) -> UInt32 {
            var value: UInt32 = 0
            var size = UInt32(MemoryLayout<UInt32>.size)
            var address = AudioObjectPropertyAddress(mSelector: selector,
                                                     mScope: kAudioObjectPropertyScopeOutput,
                                                     mElement: kAudioObjectPropertyElementMain)
            return AudioObjectGetPropertyData(object, &address, 0, nil, &size, &value) == noErr ? value : 0
        }

        var sampleRate: Float64 = 0
        var rateSize = UInt32(MemoryLayout<Float64>.size)
        var rateAddress = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyNominalSampleRate,
                                                     mScope: kAudioObjectPropertyScopeGlobal,
                                                     mElement: kAudioObjectPropertyElementMain)
        guard AudioObjectGetPropertyData(deviceID, &rateAddress, 0, nil, &rateSize, &sampleRate) == noErr,
              sampleRate > 0 else { return nil }

        // Latência do dispositivo + margem de segurança (o buffer já é contado pelo plugin)
        var frames = UInt64(uint32(kAudioDevicePropertyLatency)) + UInt64(uint32(kAudioDevicePropertySafetyOffset))

        // + latência do 1.º stream de saída
        var streamsSize: UInt32 = 0
        var streamsAddress = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreams,
                                                        mScope: kAudioObjectPropertyScopeOutput,
                                                        mElement: kAudioObjectPropertyElementMain)
        if AudioObjectGetPropertyDataSize(deviceID, &streamsAddress, 0, nil, &streamsSize) == noErr, streamsSize > 0 {
            var streams = [AudioStreamID](repeating: 0, count: Int(streamsSize) / MemoryLayout<AudioStreamID>.size)
            if AudioObjectGetPropertyData(deviceID, &streamsAddress, 0, nil, &streamsSize, &streams) == noErr,
               let first = streams.first {
                var latency: UInt32 = 0
                var latencySize = UInt32(MemoryLayout<UInt32>.size)
                var latencyAddress = AudioObjectPropertyAddress(mSelector: kAudioStreamPropertyLatency,
                                                                mScope: kAudioObjectPropertyScopeGlobal,
                                                                mElement: kAudioObjectPropertyElementMain)
                if AudioObjectGetPropertyData(first, &latencyAddress, 0, nil, &latencySize, &latency) == noErr {
                    frames += UInt64(latency)
                }
            }
        }

        // Nome do dispositivo (para o estado)
        var nameRef: Unmanaged<CFString>?
        var nameSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        var nameAddress = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName,
                                                     mScope: kAudioObjectPropertyScopeGlobal,
                                                     mElement: kAudioObjectPropertyElementMain)
        let nameStatus = AudioObjectGetPropertyData(deviceID, &nameAddress, 0, nil, &nameSize, &nameRef)
        let deviceName = (nameStatus == noErr ? nameRef?.takeRetainedValue() as String? : nil) ?? "saída"

        let seconds = min(Double(frames) / sampleRate, 1.0)   // limite de segurança
        return Info(seconds: seconds, deviceName: deviceName)
    }
}
