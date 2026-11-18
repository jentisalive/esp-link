import Foundation

enum LinkError: LocalizedError {
    case message(String)

    var errorDescription: String? {
        switch self {
        case .message(let text): return text
        }
    }
}

enum CommandProtocol {
    static func command(_ value: String) throws -> String {
        let text = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty, text.utf8.count <= 20,
              text.utf8.allSatisfy({ (32...126).contains($0) }) else {
            throw LinkError.message("Команда: 1–20 печатных ASCII-символов. Например: 1, on, off, status, ping.")
        }
        return text
    }

    static func token(_ value: String) throws -> String {
        let text = value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        guard text.count == 12, text.utf8.allSatisfy({
            (48...57).contains($0) || (97...102).contains($0)
        }) else {
            throw LinkError.message("Токен — 12 hex-символов из USB Serial Monitor платы.")
        }
        return text
    }

    // Only local IPv4 addresses and Bonjour hostnames; never send the token to a public host.
    static func baseURL(host input: String) throws -> URL {
        let host = input.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        let parts = host.split(separator: ".", omittingEmptySubsequences: false)
        let numbers = parts.compactMap { part -> Int? in
            guard !part.isEmpty, part.utf8.allSatisfy({ (48...57).contains($0) }),
                  let n = Int(part), (0...255).contains(n), String(n) == String(part) else { return nil }
            return n
        }
        let localIP = parts.count == 4 && numbers.count == 4 && (
            numbers[0] == 10 ||
            (numbers[0] == 192 && numbers[1] == 168) ||
            (numbers[0] == 172 && (16...31).contains(numbers[1])) ||
            (numbers[0] == 169 && numbers[1] == 254)
        )
        let localName = host.count <= 253 && host.hasSuffix(".local") && parts.count >= 2 &&
            parts.allSatisfy { part in
                !part.isEmpty && part.count <= 63 && part.first != "-" && part.last != "-" &&
                part.utf8.allSatisfy { (97...122).contains($0) || (48...57).contains($0) || $0 == 45 }
            }
        guard localIP || localName else {
            throw LinkError.message("Введите локальный IPv4-адрес, например 192.168.4.1, или имя esp32-link.local. Без http:// и порта.")
        }
        var components = URLComponents()
        components.scheme = "http"
        components.host = host
        guard let url = components.url else { throw LinkError.message("Некорректный адрес.") }
        return url
    }

    static func explain(_ reply: String) -> String {
        switch reply {
        case "ready": return "Токен принят. Можно отправлять команды."
        case "accepted:1": return "Плата приняла команду: три мигания…"
        case "done:1": return "Готово — светодиод мигнул 3 раза."
        case "ok:on": return "Светодиод включён."
        case "ok:off": return "Светодиод выключен."
        case "pong": return "Pong — плата на связи."
        case "error:auth": return "Неверный токен устройства."
        case "error:busy": return "Плата ещё выполняет мигание. Подождите."
        case "error:command": return "Неизвестная команда. Доступны: 1, on, off, status, ping."
        default: return reply
        }
    }
}
