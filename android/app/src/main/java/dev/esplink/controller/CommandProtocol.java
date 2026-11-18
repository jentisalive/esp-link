package dev.esplink.controller;

import java.net.URI;
import java.util.Locale;
import java.util.UUID;

public final class CommandProtocol {
    public static final UUID SERVICE = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static final UUID RX = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static final UUID TX = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    public static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    private CommandProtocol() { }

    public static String token(String input) {
        String token = input.trim().toLowerCase(Locale.ROOT);
        if (!token.matches("[0-9a-f]{12}")) {
            throw new IllegalArgumentException("Токен — 12 hex-символов из USB Serial Monitor платы.");
        }
        return token;
    }

    public static String command(String input) {
        String command = input.trim();
        if (!command.matches("[\\x20-\\x7e]{1,20}") || command.startsWith("auth:")) {
            throw new IllegalArgumentException("Команда: 1–20 печатных ASCII-символов. Например: 1, on, off, status, ping.");
        }
        return command;
    }

    public static URI baseUrl(String input) {
        String host = input.trim().toLowerCase(Locale.ROOT);
        String[] parts = host.split("\\.", -1);
        boolean localIP = false;
        if (parts.length == 4) {
            int[] numbers = new int[4];
            boolean valid = true;
            for (int i = 0; i < 4; i++) {
                if (!parts[i].matches("0|[1-9][0-9]{0,2}")) { valid = false; break; }
                numbers[i] = Integer.parseInt(parts[i]);
                if (numbers[i] > 255) { valid = false; break; }
            }
            localIP = valid && (numbers[0] == 10 || (numbers[0] == 192 && numbers[1] == 168)
                    || (numbers[0] == 172 && numbers[1] >= 16 && numbers[1] <= 31)
                    || (numbers[0] == 169 && numbers[1] == 254));
        }
        boolean localName = host.length() <= 253 && parts.length >= 2 && host.endsWith(".local");
        if (localName) {
            for (String part : parts) {
                if (!part.matches("[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")) { localName = false; break; }
            }
        }
        if (!localIP && !localName) {
            throw new IllegalArgumentException("Введите локальный адрес, например 192.168.4.1. Без http:// и порта.");
        }
        return URI.create("http://" + host + "/");
    }

    public static String explain(String reply) {
        switch (reply) {
            case "ready": return "Токен принят. Можно отправлять команды.";
            case "accepted:1": return "Плата приняла команду: три мигания…";
            case "done:1": return "Готово — светодиод мигнул 3 раза.";
            case "ok:on": return "Светодиод включён.";
            case "ok:off": return "Светодиод выключен.";
            case "pong": return "Pong — плата на связи.";
            case "error:auth": return "Неверный токен устройства.";
            case "error:busy": return "Плата ещё мигает. Подождите.";
            case "error:command": return "Неизвестная команда. Доступны: 1, on, off, status, ping.";
            default: return reply;
        }
    }
}
