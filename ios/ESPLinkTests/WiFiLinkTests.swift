import Foundation
import XCTest
@testable import ESPLink

private final class MockBoardProtocol: URLProtocol {
    static var handler: ((URLRequest) throws -> (Int, String))?

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        do {
            guard let handler = Self.handler, let url = request.url else { throw URLError(.badURL) }
            let (code, json) = try handler(request)
            let response = HTTPURLResponse(url: url, statusCode: code, httpVersion: "HTTP/1.1",
                                           headerFields: ["Content-Type": "application/json"])!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: Data(json.utf8))
            client?.urlProtocolDidFinishLoading(self)
        } catch { client?.urlProtocol(self, didFailWithError: error) }
    }
    override func stopLoading() {}
}

@MainActor
final class WiFiLinkTests: XCTestCase {
    private func link() -> WiFiLink {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [MockBoardProtocol.self]
        return WiFiLink(session: URLSession(configuration: config))
    }

    func testConnectAuthenticates() async throws {
        MockBoardProtocol.handler = { request in
            XCTAssertEqual(request.value(forHTTPHeaderField: "X-Device-Token"), "123456abcdef")
            XCTAssertEqual(request.url?.path, "/api/status")
            return (200, #"{"name":"ESP32-Test","protocolVersion":1,"busy":false,"led":false,"completedJob":0}"#)
        }
        let wifi = link()
        defer { wifi.disconnect() }
        try await wifi.connect(host: "192.168.4.1", token: "123456abcdef")
        XCTAssertTrue(wifi.isReady)
        XCTAssertEqual(wifi.deviceName, "ESP32-Test")
    }

    func testRejectsBadToken() async {
        MockBoardProtocol.handler = { _ in (401, #"{"reply":"error:auth"}"#) }
        let wifi = link()
        do {
            try await wifi.connect(host: "192.168.4.1", token: "123456abcdef")
            XCTFail("Expected authentication error")
        } catch {
            XCTAssertFalse(wifi.isReady)
            XCTAssertFalse(wifi.isConnecting)
        }
    }

    func testBlinkWaitsForCompletion() async throws {
        var completed = false
        var posts = 0
        MockBoardProtocol.handler = { request in
            if request.httpMethod == "POST" {
                posts += 1
                completed = true
                return (202, #"{"reply":"accepted:1","job":1}"#)
            }
            return (200, "{\"name\":\"ESP32-Test\",\"protocolVersion\":1,\"busy\":false,\"led\":false,\"completedJob\":\(completed ? 1 : 0)}")
        }
        let wifi = link()
        defer { wifi.disconnect() }
        try await wifi.connect(host: "192.168.4.1", token: "123456abcdef")
        var replies: [String] = []
        try await wifi.send("1") { replies.append($0) }
        XCTAssertEqual(replies, ["accepted:1", "done:1"])
        XCTAssertEqual(posts, 1)
    }

    func testMotorCommandsUseSingleRequests() async throws {
        var posts = 0
        MockBoardProtocol.handler = { request in
            if request.httpMethod == "POST" {
                posts += 1
                XCTAssertEqual(request.url?.path, "/api/command")
                if posts == 1 { return (202, #"{"reply":"ok:m1test"}"#) }
                if posts == 2 { return (202, #"{"reply":"ok:mtest"}"#) }
                if posts == 3 { return (409, #"{"reply":"error:m1-busy"}"#) }
                if posts == 4 { return (503, #"{"reply":"error:m1-unavailable"}"#) }
                return (200, #"{"reply":"ok:m1stop"}"#)
            }
            return (200, #"{"name":"ESP32-Test","protocolVersion":1,"busy":false,"led":false,"completedJob":0}"#)
        }
        let wifi = link()
        defer { wifi.disconnect() }
        try await wifi.connect(host: "192.168.4.1", token: "123456abcdef")
        var replies: [String] = []
        for command in ["m1test", "mtest", "mtest", "mtest", "m1stop"] {
            try await wifi.send(command) { replies.append($0) }
        }
        XCTAssertEqual(replies, ["ok:m1test", "ok:mtest", "error:m1-busy", "error:m1-unavailable", "ok:m1stop"])
        XCTAssertEqual(posts, 5)
    }

    func testBusyCommandIsNotRetried() async throws {
        var posts = 0
        MockBoardProtocol.handler = { request in
            if request.httpMethod == "POST" { posts += 1; return (409, #"{"reply":"error:busy"}"#) }
            return (200, #"{"name":"ESP32-Test","protocolVersion":1,"busy":false,"led":false,"completedJob":0}"#)
        }
        let wifi = link()
        defer { wifi.disconnect() }
        try await wifi.connect(host: "192.168.4.1", token: "123456abcdef")
        do {
            try await wifi.send("1") { _ in }
            XCTFail("Expected busy error")
        } catch { XCTAssertEqual(posts, 1) }
    }
}
