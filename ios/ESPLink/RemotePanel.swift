import SwiftUI
import WebKit

struct RemotePanel: UIViewRepresentable {
    let link: WiFiLink
    let active: Bool
    let onBack: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(link: link, onBack: onBack) }

    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.userContentController.add(context.coordinator, name: "remote")
        let web = WKWebView(frame: .zero, configuration: config)
        web.isOpaque = false
        web.scrollView.bounces = false
        web.navigationDelegate = context.coordinator
        if let file = Bundle.main.url(forResource: "remote", withExtension: "html") {
            context.coordinator.file = file
            web.loadFileURL(file, allowingReadAccessTo: file.deletingLastPathComponent())
        }
        return web
    }

    func updateUIView(_ web: WKWebView, context: Context) {
        let coordinator = context.coordinator
        if coordinator.active == active { return }
        coordinator.active = active
        if !active { coordinator.stop() }
        web.evaluateJavaScript(active ? "window.espResume?.()" : "window.espSuspend?.()", completionHandler: nil)
    }

    static func dismantleUIView(_ web: WKWebView, coordinator: Coordinator) {
        coordinator.active = false
        coordinator.stop()
        web.configuration.userContentController.removeScriptMessageHandler(forName: "remote")
        web.stopLoading()
    }

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
        let link: WiFiLink
        let onBack: () -> Void
        var file: URL?
        var active = true
        private var actuation: Task<Void, Never>?

        init(link: WiFiLink, onBack: @escaping () -> Void) {
            self.link = link
            self.onBack = onBack
        }

        func stop() {
            link.stopRemote()
        }

        func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                     decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            decisionHandler(action.request.url == file ? .allow : .cancel)
        }

        func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
            guard message.frameInfo.isMainFrame, message.frameInfo.request.url == file,
                  let input = message.body as? [String: Any], let id = input["id"] as? Int, id > 0,
                  let op = input["op"] as? String, let body = input["body"] as? String else { return }
            if op == "back" { onBack(); return }
            if op == "stop" {
                weak var web = message.webView
                link.stopRemote { code in
                    web?.evaluateJavaScript("window.espReply(\(id),{code:\(code)});", completionHandler: nil)
                }
                return
            }
            guard active else { return }
            let prior = op == "status" ? nil : actuation
            let task = Task { [weak web = message.webView, weak self] in
                await prior?.value
                guard let self, self.active, !Task.isCancelled else { return }
                let (code, data) = await self.perform(op, body: body)
                // Use the Objective-C callback API, not the Swift WebKit overlay that
                // failed to load on the iOS 18.5 simulator.
                let arguments: [Any] = [id, ["code": code, "data": data] as [String: Any]]
                guard let json = try? JSONSerialization.data(withJSONObject: arguments) else { return }
                let scriptData = String(decoding: json, as: UTF8.self)
                    .replacingOccurrences(of: "\u{2028}", with: "\\u2028")
                    .replacingOccurrences(of: "\u{2029}", with: "\\u2029")
                web?.evaluateJavaScript("window.espReply.apply(null, \(scriptData));", completionHandler: nil)
            }
            if op != "status" { actuation = task }
        }

        private func perform(_ op: String, body: String) async -> (Int, [String: Any]) {
            let key = "remote-panel-layout-v1"
            if op == "layout" { return (200, ["layout": UserDefaults.standard.string(forKey: key) ?? ""]) }
            if op == "save" {
                guard body.utf8.count <= 160 else { return (400, [:]) }
                UserDefaults.standard.set(body, forKey: key)
                return (200, [:])
            }
            return await link.remote(op, body: body)
        }
    }
}
