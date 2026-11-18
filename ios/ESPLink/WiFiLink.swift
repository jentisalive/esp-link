import Combine
import Foundation

struct BoardStatus: Decodable {
    let name: String
    let protocolVersion: Int
    let busy: Bool
    let led: Bool
    let completedJob: UInt32
}

private struct CommandReceipt: Decodable {
    let reply: String
    let job: UInt32?
}

// Never forward the device token through HTTP redirects, including on a captive Wi-Fi portal.
private final class LocalHTTPPolicy: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }
}

@MainActor
final class WiFiLink: ObservableObject {
    @Published private(set) var isReady = false
    @Published private(set) var isConnecting = false
    @Published private(set) var status = "Подключите iPhone к Wi-Fi платы"
    @Published private(set) var deviceName = "ESP32"
    var onError: ((String) -> Void)?

    private var baseURL: URL?
    private var token = ""
    private var generation = UUID()
    private var monitor: Task<Void, Never>?
    private let session: URLSession

    init(session: URLSession? = nil) {
        if let session {
            self.session = session
            return
        }
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 4
        config.timeoutIntervalForResource = 6
        config.urlCache = nil
        config.httpShouldSetCookies = false
        self.session = URLSession(configuration: config, delegate: LocalHTTPPolicy(), delegateQueue: nil)
    }

    func connect(host: String, token: String) async throws {
        disconnect()
        let url = try CommandProtocol.baseURL(host: host)
        baseURL = url
        self.token = token
        isConnecting = true
        status = "Проверяем плату…"
        let id = generation
        do {
            let info = try await boardStatus()
            try Task.checkCancellation()
            guard generation == id else { throw CancellationError() }
            guard info.protocolVersion == 1 else { throw LinkError.message("Несовместимая версия прошивки.") }
            deviceName = info.name
            isConnecting = false
            isReady = true
            status = "Подключено по Wi-Fi · \(url.host ?? host)"
            monitor = Task { [weak self] in
                while !Task.isCancelled {
                    do {
                        try await Task.sleep(nanoseconds: 3_000_000_000)
                        guard let self, self.generation == id else { return }
                        _ = try await self.boardStatus()
                    } catch is CancellationError { return }
                    catch {
                        guard let self, self.generation == id, !Task.isCancelled else { return }
                        self.disconnect()
                        self.status = "Связь с Wi-Fi платы потеряна"
                        self.onError?(error.localizedDescription)
                        return
                    }
                }
            }
        } catch {
            if generation == id {
                disconnect()
                status = "Не удалось подключиться. Проверьте Wi-Fi, адрес и токен."
            }
            throw error
        }
    }

    func disconnect() {
        generation = UUID()
        monitor?.cancel()
        monitor = nil
        isReady = false
        isConnecting = false
        baseURL = nil
        token = ""
        status = "Не подключено"
    }

    // POSTs are never retried: a lost response does not mean the command was not executed.
    func send(_ command: String, onReply: @escaping (String) -> Void) async throws {
        guard isReady else { throw LinkError.message("Сначала подключитесь к плате.") }
        let id = generation
        let data = try await request(path: "api/command", method: "POST", body: command)
        try Task.checkCancellation()
        guard generation == id else { throw CancellationError() }
        let receipt = try JSONDecoder().decode(CommandReceipt.self, from: data)
        onReply(receipt.reply)
        guard receipt.reply == "accepted:1", let job = receipt.job else { return }
        let deadline = Date().addingTimeInterval(8)
        while Date() < deadline {
            try await Task.sleep(nanoseconds: 300_000_000)
            let info = try await boardStatus()
            try Task.checkCancellation()
            guard generation == id else { throw CancellationError() }
            if info.completedJob >= job { onReply("done:1"); return }
        }
        throw LinkError.message("Нет подтверждения завершения. Команда могла выполниться; автоматического повтора нет.")
    }

    private func boardStatus() async throws -> BoardStatus {
        let data = try await request(path: "api/status")
        return try JSONDecoder().decode(BoardStatus.self, from: data)
    }

    private func request(path: String, method: String = "GET", body: String? = nil) async throws -> Data {
        guard let baseURL else { throw LinkError.message("Нет подключения к плате.") }
        var request = URLRequest(url: baseURL.appendingPathComponent(path))
        request.httpMethod = method
        request.setValue(token, forHTTPHeaderField: "X-Device-Token")
        if let body {
            request.httpBody = Data(body.utf8)
            request.setValue("text/plain; charset=utf-8", forHTTPHeaderField: "Content-Type")
        }
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw LinkError.message("Некорректный ответ платы.") }
        guard http.url?.host == baseURL.host else {
            throw LinkError.message("Плата перенаправила запрос на другой адрес. Подключение отменено.")
        }
        guard (200...299).contains(http.statusCode) else {
            if http.statusCode == 401 { throw LinkError.message("Неверный токен устройства.") }
            if http.statusCode == 409 { throw LinkError.message(CommandProtocol.explain("error:busy")) }
            if http.statusCode == 400 { throw LinkError.message(CommandProtocol.explain("error:command")) }
            throw LinkError.message("Ошибка HTTP \(http.statusCode). Команда могла выполниться; повторите только вручную.")
        }
        return data
    }
}
