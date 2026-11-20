package dev.esplink.controller;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Handler;
import android.os.Looper;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.Proxy;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;

final class WiFiLink {
    private final ConnectivityManager connectivity;
    private final LinkListener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final ExecutorService stopWorker = Executors.newSingleThreadExecutor();
    private final AtomicInteger generation = new AtomicInteger();
    private Future<?> operation;
    private boolean ready;
    private volatile HttpURLConnection activeRequest;
    private Network network;
    private URI baseUrl;
    private String token = "";

    WiFiLink(Context context, LinkListener listener) {
        connectivity = context.getSystemService(ConnectivityManager.class);
        this.listener = listener;
    }

    @SuppressWarnings("deprecation")
    void connect(String host, String deviceToken) {
        disconnect();
        URI url = CommandProtocol.baseUrl(host);
        Network wifi = null;
        for (Network candidate : connectivity.getAllNetworks()) {
            NetworkCapabilities capabilities = connectivity.getNetworkCapabilities(candidate);
            if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                wifi = candidate; break;
            }
        }
        if (wifi == null) throw new IllegalStateException("Сначала подключите телефон к Wi-Fi сети ESP32-Link-XXXX.");
        network = wifi;
        baseUrl = url;
        token = deviceToken;
        int id = generation.get();
        listener.onState("Проверяем плату по Wi-Fi…", false, true);
        operation = worker.submit(() -> {
            try {
                JSONObject status = request(id, "api/status", null);
                if (status.getInt("protocolVersion") != 1) throw new IllegalStateException("Несовместимая версия прошивки.");
                String name = status.getString("name");
                post(id, () -> {
                    ready = true;
                    listener.onState(name + " · Wi-Fi · " + url.getHost(), true, false);
                    listener.onReply("ready");
                    main.postDelayed(heartbeat, 3000);
                });
            } catch (Exception error) { report(id, error); }
        });
    }

    void send(String command) {
        if (!ready) throw new IllegalStateException("Сначала подключитесь к плате.");
        int id = generation.get();
        operation = worker.submit(() -> {
            try {
                // Never retry a POST: a lost response does not imply that the command did not execute.
                JSONObject result = request(id, "api/command", command);
                String reply = result.getString("reply");
                post(id, () -> listener.onReply(reply));
                if (!reply.equals("accepted:1")) return;
                long job = result.getLong("job");
                long deadline = System.nanoTime() + 8_000_000_000L;
                while (System.nanoTime() < deadline) {
                    Thread.sleep(300);
                    JSONObject status = request(id, "api/status", null);
                    if (status.getLong("completedJob") >= job) {
                        post(id, () -> listener.onReply("done:1")); return;
                    }
                }
                throw new IllegalStateException("Нет подтверждения завершения. Команда могла выполниться; автоматического повтора нет.");
            } catch (Exception error) { report(id, error); }
        });
    }

    interface RemoteReply { void finish(int code, JSONObject data); }

    void remote(String op, String body, RemoteReply reply) {
        if ("stop".equals(op)) { stopRemote(reply); return; }
        if (!ready) { reply.finish(0, new JSONObject()); return; }
        String path;
        String payload;
        switch (op) {
            case "status": path = "api/remote"; payload = null; break;
            case "arm": path = "api/remote/arm"; payload = "arm"; break;
            case "frame":
                if (!body.matches("[0-9]{1,10},[0-9]{1,6},-?[0-9]{1,3},-?[0-9]{1,3},[01]")) {
                    reply.finish(400, new JSONObject()); return;
                }
                path = "api/remote/frame"; payload = body; break;
            default: reply.finish(400, new JSONObject()); return;
        }
        int id = generation.get();
        worker.submit(() -> {
            try {
                JSONObject response = request(id, path, payload, true);
                int code = response.optInt("httpStatus", 200);
                post(id, () -> reply.finish(code, response));
            } catch (Exception error) { post(id, () -> reply.finish(0, new JSONObject())); }
        });
    }

    private void stopRemote(RemoteReply reply) {
        Network route = network;
        URI base = baseUrl;
        String credential = token;
        if (!ready || route == null || base == null) { reply.finish(0, new JSONObject()); return; }
        stopWorker.execute(() -> {
            HttpURLConnection connection = null;
            int status = 0;
            try {
                connection = (HttpURLConnection) route.openConnection(base.resolve("api/command").toURL(), Proxy.NO_PROXY);
                connection.setConnectTimeout(230);
                connection.setReadTimeout(230);
                connection.setInstanceFollowRedirects(false);
                connection.setUseCaches(false);
                connection.setRequestProperty("X-Device-Token", credential);
                connection.setRequestProperty("Content-Type", "text/plain; charset=utf-8");
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                byte[] command = "m1stop".getBytes(StandardCharsets.US_ASCII);
                connection.setFixedLengthStreamingMode(command.length);
                try (java.io.OutputStream output = connection.getOutputStream()) { output.write(command); }
                status = connection.getResponseCode();
            } catch (Exception ignored) { /* ESP32 times out even if explicit stop is lost. */ }
            finally { if (connection != null) connection.disconnect(); }
            int result = status;
            main.post(() -> reply.finish(result, new JSONObject()));
        });
    }

    private final Runnable heartbeat = new Runnable() {
        @Override public void run() {
            if (!ready) return;
            int id = generation.get();
            worker.submit(() -> {
                try {
                    request(id, "api/status", null);
                    post(id, () -> main.postDelayed(this, 3000));
                } catch (Exception error) { report(id, error); }
            });
        }
    };

    private JSONObject request(int id, String path, String body) throws Exception {
        return request(id, path, body, false);
    }

    private JSONObject request(int id, String path, String body, boolean remote) throws Exception {
        check(id);
        // Snapshot credentials before disconnect() can clear them; the generation gates every result.
        Network route = network;
        URI base = baseUrl;
        String credential = token;
        if (route == null || base == null) throw new InterruptedException();
        HttpURLConnection connection = (HttpURLConnection) route.openConnection(base.resolve(path).toURL(), Proxy.NO_PROXY);
        activeRequest = connection;
        try {
            check(id);
            connection.setConnectTimeout(remote ? 230 : 3500);
            connection.setReadTimeout(remote ? 230 : 4000);
            connection.setInstanceFollowRedirects(false);
            connection.setUseCaches(false);
            connection.setRequestProperty("X-Device-Token", credential);
            if (body != null) {
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", "text/plain; charset=utf-8");
                byte[] bytes = body.getBytes(StandardCharsets.US_ASCII);
                connection.setFixedLengthStreamingMode(bytes.length);
                try (java.io.OutputStream output = connection.getOutputStream()) { output.write(bytes); }
            }
            int code = connection.getResponseCode();
            if (remote && code != 200) return new JSONObject().put("httpStatus", code);
            boolean motorTest = "m1test".equals(body) || "mtest".equals(body);
            if (motorTest && (code == 409 || code == 503)) {
                return new JSONObject().put("reply", code == 409 ? "error:m1-busy" : "error:m1-unavailable");
            }
            if (code == 401) throw new IllegalStateException(CommandProtocol.explain("error:auth"));
            if (code == 409) throw new IllegalStateException(CommandProtocol.explain("error:busy"));
            if (code == 400 && (motorTest || "m1stop".equals(body))) {
                throw new IllegalStateException("Для этой команды нужна обновлённая прошивка ESP32.");
            }
            if (code == 400) throw new IllegalStateException(CommandProtocol.explain("error:command"));
            if (code < 200 || code >= 300) throw new IllegalStateException("HTTP " + code + ". Перенаправления запрещены.");
            try (InputStream input = connection.getInputStream(); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[1024];
                int length;
                while ((length = input.read(buffer)) != -1) {
                    if (output.size() + length > 8192) throw new IllegalStateException("Слишком большой ответ платы.");
                    output.write(buffer, 0, length);
                }
                check(id);
                return new JSONObject(output.toString(StandardCharsets.UTF_8.name()));
            }
        } finally {
            connection.disconnect();
            if (activeRequest == connection) activeRequest = null;
        }
    }

    private void check(int id) throws InterruptedException {
        if (Thread.currentThread().isInterrupted() || generation.get() != id) throw new InterruptedException();
    }

    private void post(int id, Runnable action) {
        main.post(() -> { if (generation.get() == id) action.run(); });
    }

    private void report(int id, Exception error) {
        if (error instanceof InterruptedException) return;
        post(id, () -> {
            String message = error.getMessage();
            disconnect();
            listener.onError((message == null ? "Связь с платой потеряна." : message)
                    + " Если ответ потерян, команда могла выполниться. Повтор — только вручную.");
        });
    }

    void disconnect() {
        generation.incrementAndGet();
        main.removeCallbacks(heartbeat);
        if (operation != null) operation.cancel(true);
        operation = null;
        HttpURLConnection current = activeRequest;
        if (current != null) current.disconnect();
        ready = false;
        network = null;
        baseUrl = null;
        token = "";
        listener.onState("Не подключено", false, false);
    }

    void close() { disconnect(); worker.shutdownNow(); stopWorker.shutdown(); }
}
