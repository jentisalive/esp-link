package dev.esplink.controller;

import org.junit.Test;
import static org.junit.Assert.*;

public class CommandProtocolTest {
    @Test public void commandsAreTrimmed() {
        for (String command : new String[]{"1", "on", "off", "status", "ping"}) {
            assertEquals(command, CommandProtocol.command(" " + command + "\n"));
        }
    }
    @Test public void commandLimitMatchesFirmware() {
        assertEquals(20, CommandProtocol.command("12345678901234567890").length());
    }
    @Test public void invalidCommandsAreRejected() {
        for (String value : new String[]{"", "  ", "мигни", "on\noff", "123456789012345678901"}) {
            assertThrows(value, IllegalArgumentException.class, () -> CommandProtocol.command(value));
        }
    }
    @Test public void tokensAreNormalized() {
        assertEquals("abcdef123456", CommandProtocol.token(" ABCDEF123456\n"));
        assertEquals("zetacontrol7", CommandProtocol.token("ZetaControl7"));
        for (int length : new int[]{8, 13, 64}) {
            assertEquals(length, CommandProtocol.token("Z".repeat(length)).length());
        }
    }
    @Test public void invalidTokensAreRejected() {
        for (String value : new String[]{"", "1234567", "ZetaControl!", "ЖetaControl7", "a".repeat(65)}) {
            assertThrows(IllegalArgumentException.class, () -> CommandProtocol.token(value));
        }
    }
    @Test public void privateHostsAreAccepted() {
        for (String host : new String[]{"192.168.4.1", "10.0.0.1", "172.16.0.1", "172.31.255.254", "169.254.1.2", "esp32-link.local"}) {
            assertEquals(host, CommandProtocol.baseUrl(host).getHost());
            assertEquals("http", CommandProtocol.baseUrl(host).getScheme());
        }
    }
    @Test public void publicOrMalformedHostsAreRejected() {
        for (String host : new String[]{"8.8.8.8", "example.com", "localhost", "127.0.0.1", "172.32.0.1", "192.168.4.999",
                "http://192.168.4.1", "192.168.4.1:80", "192.168.4.1/path", "user@esp.local", "-esp.local", "esp..local", "192.168.004.1", ""}) {
            assertThrows(host, IllegalArgumentException.class, () -> CommandProtocol.baseUrl(host));
        }
    }
    @Test public void responseExplanationMatchesFirmware() {
        assertTrue(CommandProtocol.explain("done:1").contains("3"));
        assertEquals("status:off", CommandProtocol.explain("status:off"));
    }
}
