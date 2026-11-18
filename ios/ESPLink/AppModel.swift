import Combine
import Foundation

struct LogEntry: Identifiable {
    let id = UUID()
    let date = Date()
    let text: String
    let isError: Bool
}

@MainActor
final class AppModel: ObservableObject {
    let wifi = WiFiLink()
    @Published var token = TokenStore.load()
    @Published var host = UserDefaults.standard.string(forKey: "boardHost") ?? "192.168.4.1"
    @Published var command = "1"
    @Published private(set) var logs: [LogEntry] = []
    @Published private(set) var isSending = false
    @Published var alert: String?
    private var observations = Set<AnyCancellable>()
    private var operation: Task<Void, Never>?

    var isReady: Bool { wifi.isReady }
    var isConnecting: Bool { wifi.isConnecting }
    var status: String { wifi.status }
    var deviceName: String { wifi.deviceName }

    init() {
        wifi.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &observations)
        wifi.onError = { [weak self] text in
            self?.operation?.cancel()
            self?.isSending = false
            self?.report(text)
        }
    }

    func connect() {
        do {
            let validToken = try CommandProtocol.token(token)
            _ = try CommandProtocol.baseURL(host: host)
            try TokenStore.save(validToken)
            token = validToken
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
        } catch { report(error.localizedDescription) }
    }

    func disconnect() {
        operation?.cancel()
        operation = nil
        isSending = false
        wifi.disconnect()
    }

    func send(_ preset: String? = nil) {
        guard !isSending else { return }
        do {
            let text = try CommandProtocol.command(preset ?? command)
            guard isReady else { throw LinkError.message("Сначала подключитесь к ESP32.") }
            isSending = true
            log("→ \(text)")
            operation = Task {
                defer { if !Task.isCancelled { isSending = false } }
                do {
                    try await wifi.send(text) { [weak self] reply in
                        self?.log("← \(CommandProtocol.explain(reply))", isError: reply.hasPrefix("error:"))
                    }
                } catch is CancellationError { }
                catch {
                    if !Task.isCancelled {
                        report("\(error.localizedDescription) Если ответ потерян, команда могла выполниться. Повтор — только вручную.")
                    }
                }
            }
        } catch {
            isSending = false
            report(error.localizedDescription)
        }
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
