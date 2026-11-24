import Foundation
import CoreVideo

// Guarda as imagens recentes (já reduzidas ao tamanho final) com a hora REAL
// em que foram captadas, para o vídeo poder usar, para cada instante, a imagem
// da câmara e do FL desse mesmo instante — independentemente do atraso de cada fonte.
final class FrameRing {
    private var items: [(ticks: UInt64, buffer: CVPixelBuffer)] = []
    let maxAge: Double

    init(maxAge: Double) {
        self.maxAge = maxAge
    }

    var isEmpty: Bool { items.isEmpty }
    var latest: CVPixelBuffer? { items.last?.buffer }
    var count: Int { items.count }

    func push(_ buffer: CVPixelBuffer, ticks: UInt64) {
        if let last = items.last, ticks < last.ticks {
            // Fora de ordem (raro): inserir no sítio certo
            let index = items.firstIndex(where: { $0.ticks > ticks }) ?? items.count
            items.insert((ticks, buffer), at: index)
        } else {
            items.append((ticks, buffer))
        }

        // Descartar o que já é demasiado antigo (fica sempre pelo menos 1)
        guard let newest = items.last?.ticks else { return }
        while items.count > 1, HostClock.seconds(from: items[0].ticks, to: newest) > maxAge {
            items.removeFirst()
        }
    }

    /// A imagem mais recente captada até `ticks` (ou a mais antiga, se todas forem posteriores).
    func frame(at ticks: UInt64) -> CVPixelBuffer? {
        guard var chosen = items.first?.buffer else { return nil }
        for item in items {
            if item.ticks <= ticks { chosen = item.buffer } else { break }
        }
        return chosen
    }

    func clear() {
        items.removeAll()
    }
}
