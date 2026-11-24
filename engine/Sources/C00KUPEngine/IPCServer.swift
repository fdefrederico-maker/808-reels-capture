import Foundation
import Network

// Servidor de socket local. Formato das mensagens (compatível com o
// juce::InterprocessConnection do plugin):
//   [UInt32 LE número mágico][UInt32 LE tamanho][conteúdo]
// O 1.º byte do conteúdo é o tipo: 'J' = JSON, 'F' = JPEG.
final class IPCServer {
    static let magic: UInt32 = 0x4B30_3043   // "C00K"
    static let typeJSON: UInt8 = 0x4A        // 'J'
    static let typeFrame: UInt8 = 0x46       // 'F'

    let queue: DispatchQueue
    private var listener: NWListener?
    private var clients: [Int: Client] = [:]
    private var nextID = 1

    var onMessage: ((Client, UInt8, Data) -> Void)?
    var onClientsChanged: ((Int) -> Void)?
    var onFatalError: ((String) -> Void)?

    var clientCount: Int { clients.count }

    init(queue: DispatchQueue) {
        self.queue = queue
    }

    func start(port: UInt16) throws {
        let params = NWParameters.tcp
        params.allowLocalEndpointReuse = true
        params.requiredInterfaceType = .loopback      // só aceita ligações deste Mac

        guard let nwPort = NWEndpoint.Port(rawValue: port) else { return }
        let listener = try NWListener(using: params, on: nwPort)

        listener.newConnectionHandler = { [weak self] connection in
            self?.accept(connection)
        }
        listener.stateUpdateHandler = { [weak self] state in
            if case .failed(let error) = state {
                self?.onFatalError?("Socket: \(error)")
            }
        }
        listener.start(queue: queue)
        self.listener = listener
    }

    private func accept(_ connection: NWConnection) {
        let client = Client(id: nextID, connection: connection, server: self)
        nextID += 1
        clients[client.id] = client
        client.start(queue: queue)
        onClientsChanged?(clients.count)
    }

    fileprivate func remove(_ client: Client) {
        if clients.removeValue(forKey: client.id) != nil {
            client.connection.cancel()
            onClientsChanged?(clients.count)
        }
    }

    fileprivate func deliver(_ client: Client, type: UInt8, body: Data) {
        onMessage?(client, type, body)
    }

    func broadcast(type: UInt8, body: Data, droppable: Bool = false) {
        for client in clients.values {
            client.send(type: type, body: body, droppable: droppable)
        }
    }

    func broadcastJSON(_ object: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: object) else { return }
        broadcast(type: IPCServer.typeJSON, body: data)
    }

    //==========================================================================
    final class Client {
        let id: Int
        let connection: NWConnection
        private weak var server: IPCServer?
        private var buffer = Data()
        private var pendingSends = 0

        init(id: Int, connection: NWConnection, server: IPCServer) {
            self.id = id
            self.connection = connection
            self.server = server
        }

        func start(queue: DispatchQueue) {
            connection.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                switch state {
                case .failed, .cancelled:
                    self.server?.remove(self)
                default:
                    break
                }
            }
            connection.start(queue: queue)
            receive()
        }

        private func receive() {
            connection.receive(minimumIncompleteLength: 1, maximumLength: 1 << 20) { [weak self] data, _, isComplete, error in
                guard let self else { return }

                if let data, !data.isEmpty {
                    self.buffer.append(data)
                    self.parse()
                }

                if isComplete || error != nil {
                    self.server?.remove(self)
                    return
                }

                self.receive()
            }
        }

        private func parse() {
            while buffer.count >= 8 {
                let magic = buffer.uint32LE(at: 0)
                let size = Int(buffer.uint32LE(at: 4))

                guard magic == IPCServer.magic, size <= 64 << 20 else {
                    server?.remove(self)
                    return
                }

                guard buffer.count >= 8 + size else { return }

                let payload = buffer.subdata(in: 8 ..< (8 + size))
                buffer = buffer.subdata(in: (8 + size) ..< buffer.count)

                if let type = payload.first {
                    server?.deliver(self, type: type, body: payload.subdata(in: 1 ..< payload.count))
                }
            }
        }

        func send(type: UInt8, body: Data, droppable: Bool) {
            if droppable && pendingSends > 2 { return }   // cliente lento: salta frames

            var message = Data(capacity: 9 + body.count)
            message.appendUInt32LE(IPCServer.magic)
            message.appendUInt32LE(UInt32(1 + body.count))
            message.append(type)
            message.append(body)

            pendingSends += 1
            connection.send(content: message, completion: .contentProcessed { [weak self] _ in
                self?.pendingSends -= 1
            })
        }

        func sendJSON(_ object: [String: Any]) {
            guard let data = try? JSONSerialization.data(withJSONObject: object) else { return }
            send(type: IPCServer.typeJSON, body: data, droppable: false)
        }
    }
}

extension Data {
    func uint32LE(at offset: Int) -> UInt32 {
        let i = startIndex + offset
        return UInt32(self[i]) | (UInt32(self[i + 1]) << 8) | (UInt32(self[i + 2]) << 16) | (UInt32(self[i + 3]) << 24)
    }

    mutating func appendUInt32LE(_ value: UInt32) {
        append(UInt8(value & 0xff))
        append(UInt8((value >> 8) & 0xff))
        append(UInt8((value >> 16) & 0xff))
        append(UInt8((value >> 24) & 0xff))
    }
}
