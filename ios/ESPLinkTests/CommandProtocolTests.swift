import XCTest
@testable import ESPLink

final class CommandProtocolTests: XCTestCase {
    func testCommands() throws {
        for text in ["1", "on", "off", "status", "ping"] {
            XCTAssertEqual(try CommandProtocol.command(" \(text)\n"), text)
        }
        XCTAssertEqual(try CommandProtocol.command(String(repeating: "x", count: 20)).utf8.count, 20)
    }

    func testInvalidCommands() {
        for text in ["", "   ", "мигни", "on\noff", String(repeating: "x", count: 21)] {
            XCTAssertThrowsError(try CommandProtocol.command(text), text)
        }
    }

    func testTokenValidation() throws {
        let token = try CommandProtocol.token(" ABCDEF012345\n")
        XCTAssertEqual(token, "abcdef012345")
        for text in ["", "123456", "zzzzzzzzzzzz", "123456789abcd"] {
            XCTAssertThrowsError(try CommandProtocol.token(text))
        }
    }

    func testLocalHosts() throws {
        for host in ["192.168.4.1", "10.0.0.1", "172.16.0.1", "172.31.255.254", "169.254.1.2", "esp32-link.local"] {
            XCTAssertEqual(try CommandProtocol.baseURL(host: host).scheme, "http")
            XCTAssertEqual(try CommandProtocol.baseURL(host: host).host, host)
        }
    }

    func testRejectPublicOrMalformedHosts() {
        for host in ["8.8.8.8", "example.com", "localhost", "127.0.0.1", "172.32.0.1", "192.168.4.999",
                     "http://192.168.4.1", "192.168.4.1:80", "192.168.4.1/path", "user@esp.local",
                     "-esp.local", "esp..local", "192.168.004.1", "esp.local?x=1", ""] {
            XCTAssertThrowsError(try CommandProtocol.baseURL(host: host), host)
        }
    }

    func testWireStatusDecoding() throws {
        let json = #"{"name":"ESP32-Link-1234","protocolVersion":1,"busy":false,"led":false,"completedJob":1}"#
        let status = try JSONDecoder().decode(BoardStatus.self, from: Data(json.utf8))
        XCTAssertEqual(status.protocolVersion, 1)
        XCTAssertEqual(status.completedJob, 1)
        XCTAssertFalse(status.busy)
    }
}
