import Foundation

// Relógio do sistema (mach_absolute_time). O plugin usa exatamente o mesmo relógio
// (juce::Time::getHighResolutionTicks no macOS), por isso as marcas de tempo do
// áudio e do vídeo são diretamente comparáveis.
enum HostClock {
    private static let timebase: mach_timebase_info_data_t = {
        var tb = mach_timebase_info_data_t()
        mach_timebase_info(&tb)
        return tb
    }()

    static func now() -> UInt64 { mach_absolute_time() }

    /// Segundos de `from` até `to` (pode ser negativo).
    static func seconds(from: UInt64, to: UInt64) -> Double {
        let delta = Int64(bitPattern: to &- from)
        return Double(delta) * Double(timebase.numer) / Double(timebase.denom) / 1_000_000_000.0
    }
}
