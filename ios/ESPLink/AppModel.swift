import Combine
import Foundation

enum Transport: String, CaseIterable, Identifiable {
    case bluetooth = "Bluetooth"
    case wifi = "Wi-Fi"
    var id: String { rawValue }
}

struct LogEntry: Identifiable {
    let id = UUID()
    let date = Date()
    let text: String
    let isError: Bool
}

@MainActor
final class AppModel: ObservableObject {
    let bluetooth = BluetoothLink()
    let wifi = WiFiLink()
    @Published var transport = Transport.bluetooth
    @Published var token = TokenStore.load()
    @Published var host = UserDefaults.standard.string(forKey: "boardHost") ?? "192.168.4.1"
    @Published var command = "1"
    @Published private(set) var logs: [LogEntry] = []
    @Published private(set) var isSending = false
    @Published var alert: String?
    private var observations = Set<AnyCancellable>()
    private var operation: Task<Void, Never>?
    private var replyTimeout: Task<Void, Never>?
    private var pendingCommand: String?

    var isReady: Bool { transport == .bluetooth ? bluetooth.isReady : wifi.isReady }
    var isConnecting: Bool { transport == .bluetooth ? bluetooth.isConnecting : wifi.isConnecting }
    var status: String { transport == .bluetooth ? bluetooth.status : wifi.status }
    var deviceName: String { transport == .bluetooth ? bluetooth.deviceName : wifi.deviceName }

    init() {
        bluetooth.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &observations)
        wifi.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &observations)
        bluetooth.onReply = { [weak self] reply in self?.receiveBLE(reply) }
        bluetooth.onError = { [weak self] text in
            self?.finishSending()
            self?.report(text)
        }
        wifi.onError = { [weak self] text in
            self?.operation?.cancel()
            self?.finishSending()
            self?.report(text)
        }
    }

    func switchTransport() { disconnect() }

    func connect(board: NearbyBoard? = nil) {
        do {
            let validToken = try CommandProtocol.token(token)
            if transport == .wifi { _ = try CommandProtocol.baseURL(host: host) }
            try TokenStore.save(validToken)
            token = validToken
            if transport == .bluetooth {
                guard let board else { return }
                bluetooth.connect(board, token: validToken)
            } else {
                UserDefaults.standard.set(host, forKey: "boardHost")
                operation?.cancel()
                operation = Task {
                    do {
                        try await wifi.connect(host: host, token: validToken)
                        try Task.checkCancellation()
                        log("Wi-Fi: \(wifi.deviceName) · токен принят")
                    } catch is CancellationError { }
                    catch { if !Task.isCancelled { report(error.localizedDescription) } }
                }
            }
        } catch { report(error.localizedDescription) }
    }

    func disconnect() {
        operation?.cancel()
        operation = nil
        finishSending()
        bluetooth.disconnect()
        wifi.disconnect()
    }

    func send(_ preset: String? = nil) {
        guard !isSending else { return }
        do {
            let text = try CommandProtocol.command(preset ?? command)
            guard isReady else { throw LinkError.message("Сначала подключитесь к ESP32.") }
            isSending = true
            pendingCommand = text
            log("→ \(text)")
            if transport == .bluetooth {
                try bluetooth.send(text)
                replyTimeout = Task { [weak self] in
                    do { try await Task.sleep(nanoseconds: 8_000_000_000) }
                    catch { return }
                    guard let self else { return }
                    self.finishSending()
                    self.report("Нет подтверждения от платы. Команда могла выполниться; автоматического повтора нет.")
                }
            } else {
                operation = Task {
                    defer { if !Task.isCancelled { finishSending() } }
                    do {
                        try await wifi.send(text) { [weak self] reply in
                            self?.log("← \(CommandProtocol.explain(reply))")
                        }
                    } catch is CancellationError { }
                    catch {
                        if !Task.isCancelled {
                            report("\(error.localizedDescription) Если ответ потерян, команда могла выполниться. Повтор — только вручную.")
                        }
                    }
                }
            }
        } catch {
            finishSending()
            report(error.localizedDescription)
        }
    }

    private func receiveBLE(_ reply: String) {
        log("← \(CommandProtocol.explain(reply))", isError: reply.hasPrefix("error:"))
        guard isSending else { return }
        if reply.hasPrefix("error:") || (pendingCommand == "1" ? reply == "done:1" : reply != "accepted:1") {
            finishSending()
        }
    }

    private func finishSending() {
        replyTimeout?.cancel()
        replyTimeout = nil
        pendingCommand = nil
        isSending = false
    }

    private func report(_ text: String) {
        log(text, isError: true)
        alert = text
    }

    private func log(_ text: String, isError: Bool = false) {
        logs.insert(LogEntry(text: text, isError: isError), at: 0)
        if logs.count > 100 { logs.removeLast(logs.count - 100) }
    }

    func clearLog() { logs = [] }
}
