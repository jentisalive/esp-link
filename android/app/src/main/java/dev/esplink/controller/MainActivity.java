package dev.esplink.controller;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int BACKGROUND = Color.rgb(9, 14, 19);
    private static final int CARD = Color.rgb(18, 24, 31);
    private static final int MINT = Color.rgb(120, 245, 189);
    private static final int MUTED = Color.rgb(155, 173, 186);
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<String> logs = new ArrayList<>();
    private final List<Button> commandButtons = new ArrayList<>();
    private WiFiLink wifi;
    private boolean ready;
    private boolean connecting;
    private boolean sending;
    private String pendingCommand;
    private TextView stateView;
    private TextView logView;
    private Button connectButton;
    private Button disconnectButton;
    private EditText tokenField;
    private EditText hostField;
    private EditText commandField;
    private final Runnable replyTimeout = () -> {
        finishSending();
        showError("Нет подтверждения от платы. Команда могла выполниться; автоматического повтора нет.");
    };

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        wifi = new WiFiLink(this, listener());
        buildInterface();
        render();
    }

    private LinkListener listener() {
        return new LinkListener() {
            @Override public void onState(String message, boolean connected, boolean busyConnecting) {
                if (stateView == null) return;
                ready = connected;
                connecting = busyConnecting;
                if (!ready && !connecting) finishSending();
                stateView.setText((connected ? "●  " : "○  ") + message);
                stateView.setTextColor(connected ? MINT : MUTED);
                render();
            }

            @Override public void onReply(String reply) {
                log("← " + CommandProtocol.explain(reply));
                if (sending && (reply.startsWith("error:") || ("1".equals(pendingCommand)
                        ? reply.equals("done:1") : !reply.equals("accepted:1")))) finishSending();
            }

            @Override public void onError(String message) {
                finishSending();
                showError(message);
            }
        };
    }

    private void buildInterface() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.setFillViewport(true);
        LinearLayout page = column();
        page.setPadding(dp(20), dp(20), dp(20), dp(28));
        scroll.addView(page);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            scroll.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                android.graphics.Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, keyboard.bottom));
                return insets;
            });
        }
        TextView eyebrow = text("DEVICE CONTROL                          LOCAL / 02", 10, MINT);
        eyebrow.setTypeface(Typeface.MONOSPACE);
        add(page, eyebrow, 0);
        TextView title = text("ESP Link", 46, MINT);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        add(page, title, 8);
        add(page, text("Твоя плата. Твои команды.", 15, MUTED), 0);

        LinearLayout connection = card(page, "01  Подключение");
        add(connection, text("Только Wi-Fi · локальное управление", 14, MINT), 14);
        stateView = text("○  Не подключено", 13, MUTED);
        add(connection, stateView, 14);
        tokenField = input("Токен из Serial Monitor", true);
        tokenField.setSaveEnabled(false);
        tokenField.setText(TokenStore.load(this));
        add(connection, tokenField, 14);
        hostField = input("192.168.4.1", false);
        hostField.setText(getPreferences(MODE_PRIVATE).getString("host", "192.168.4.1"));
        add(connection, hostField, 12);
        connectButton = button("Подключиться по Wi-Fi", this::connectWifi, true);
        add(connection, connectButton, 12);
        disconnectButton = button("Отключиться", this::disconnect, false);
        add(connection, disconnectButton, 12);
        TextView wifiHelp = text("В настройках телефона подключитесь к ESP32-Link-XXXX. Введите Wi-Fi password из USB Serial Monitor — это НЕ токен. "
                + "Затем вернитесь сюда и введите Device token. Адрес: 192.168.4.1. "
                + "Пароль сети меняется по USB через change_wifi_password.bat.", 12, MUTED);
        add(connection, wifiHelp, 12);
        Button settings = button("Открыть настройки Wi-Fi", () -> startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS)), false);
        add(connection, settings, 10);

        LinearLayout commands = card(page, "02  Команды");
        Button blink = button("☀  Мигнуть 3 раза  ↗", () -> send("1"), true);
        blink.setTextSize(19);
        blink.setMinHeight(dp(76));
        commandButtons.add(blink);
        add(commands, blink, 16);
        LinearLayout quick = row();
        for (String[] preset : new String[][]{{"Включить", "on"}, {"Выключить", "off"}, {"Проверка", "ping"}}) {
            Button command = button(preset[0], () -> send(preset[1]), false);
            command.setTextSize(11);
            commandButtons.add(command);
            weighted(quick, command);
        }
        add(commands, quick, 10);
        LinearLayout motor = row();
        Button motorTest = button("Тест мотора 1", () -> send("m1test"), false);
        Button motorStop = button("Стоп мотора 1", () -> send("m1stop"), false);
        commandButtons.add(motorTest);
        commandButtons.add(motorStop);
        weighted(motor, motorTest);
        weighted(motor, motorStop);
        add(commands, motor, 10);
        add(commands, text("Один цикл · 2 с вперёд, 1 с пауза, 2 с назад. Мотор 2 не используется.", 11, MUTED), 6);
        commandField = input("Команда, например 1", false);
        commandField.setText("1");
        commandField.setImeOptions(EditorInfo.IME_ACTION_SEND);
        commandField.setOnEditorActionListener((view, action, event) -> {
            if (action == EditorInfo.IME_ACTION_SEND) { send(commandField.getText().toString()); return true; }
            return false;
        });
        add(commands, commandField, 12);
        Button send = button("Отправить команду ↑", () -> send(commandField.getText().toString()), false);
        commandButtons.add(send);
        add(commands, send, 8);
        add(commands, text("1 = три мигания · on · off · status · ping", 11, MUTED), 10);

        LinearLayout console = card(page, "03  Журнал");
        logView = text("Здесь появятся команды и ответы платы.", 12, MINT);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setTextIsSelectable(true);
        add(console, logView, 14);
        add(console, button("Очистить журнал", () -> { logs.clear(); logView.setText("Журнал очищен."); }, false), 12);
        TextView footer = text("uPesy ESP32 Wroom DevKit · GPIO 2\nAndroid 8+ · Локально, без облака", 12, MUTED);
        footer.setGravity(Gravity.CENTER);
        add(page, footer, 24);
    }

    private void render() {
        if (connectButton == null || commandField == null) return;
        boolean idle = !ready && !connecting;
        tokenField.setVisibility(idle ? View.VISIBLE : View.GONE);
        hostField.setVisibility(idle ? View.VISIBLE : View.GONE);
        connectButton.setVisibility(idle ? View.VISIBLE : View.GONE);
        disconnectButton.setVisibility(ready || connecting ? View.VISIBLE : View.GONE);
        for (Button button : commandButtons) {
            button.setEnabled(ready && !sending);
            button.setAlpha(ready && !sending ? 1f : 0.4f);
        }
        commandField.setEnabled(ready && !sending);
    }

    private String saveToken() throws Exception {
        String token = CommandProtocol.token(tokenField.getText().toString());
        TokenStore.save(this, token);
        return token;
    }

    private void connectWifi() {
        try {
            String host = hostField.getText().toString();
            CommandProtocol.baseUrl(host);
            String token = saveToken();
            getPreferences(MODE_PRIVATE).edit().putString("host", host).apply();
            wifi.connect(host, token);
        } catch (Exception error) { showError(error.getMessage()); }
    }

    private void send(String input) {
        if (sending) return;
        try {
            if (!ready) throw new IllegalStateException("Сначала подключитесь к ESP32.");
            String command = CommandProtocol.command(input);
            sending = true;
            pendingCommand = command;
            log("→ " + command);
            render();
            wifi.send(command);
            main.postDelayed(replyTimeout, 18_000);
        } catch (RuntimeException error) { finishSending(); showError(error.getMessage()); }
    }

    private void finishSending() {
        main.removeCallbacks(replyTimeout);
        sending = false;
        pendingCommand = null;
        render();
    }

    private void disconnect() {
        finishSending();
        wifi.disconnect();
    }

    private void showError(String message) {
        String text = message == null ? "Не удалось выполнить действие." : message;
        log("! " + text);
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    private void log(String message) {
        String time = new SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(new Date());
        logs.add(0, time + "  " + message);
        if (logs.size() > 100) logs.remove(logs.size() - 1);
        if (logView != null) logView.setText(String.join("\n\n", logs));
    }

    @Override protected void onStop() {
        super.onStop();
        disconnect();
    }

    @Override protected void onDestroy() {
        main.removeCallbacksAndMessages(null);
        wifi.close();
        super.onDestroy();
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }
    private LinearLayout row() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        return layout;
    }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setLineSpacing(dp(3), 1f);
        return view;
    }
    private GradientDrawable background(int color, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(radius));
        return background;
    }
    private Button button(String title, Runnable action, boolean prominent) {
        Button button = new Button(this);
        button.setText(title);
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setTextColor(prominent ? BACKGROUND : Color.WHITE);
        button.setBackground(background(prominent ? MINT : BACKGROUND, 12));
        button.setPadding(dp(10), dp(8), dp(10), dp(8));
        button.setMinHeight(dp(48));
        button.setOnClickListener(view -> action.run());
        return button;
    }
    private EditText input(String hint, boolean password) {
        EditText field = new EditText(this);
        field.setHint(hint);
        field.setTextColor(Color.WHITE);
        field.setHintTextColor(MUTED);
        field.setTextSize(14);
        field.setTypeface(Typeface.MONOSPACE);
        field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT | (password ? InputType.TYPE_TEXT_VARIATION_PASSWORD
                : InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS));
        field.setPadding(dp(12), dp(12), dp(12), dp(12));
        field.setBackground(background(BACKGROUND, 10));
        field.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        return field;
    }
    private LinearLayout card(LinearLayout page, String title) {
        LinearLayout card = column();
        card.setPadding(dp(18), dp(18), dp(18), dp(18));
        card.setBackground(background(CARD, 22));
        TextView heading = text(title, 18, Color.WHITE);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        add(card, heading, 0);
        add(page, card, 22);
        return card;
    }
    private void add(LinearLayout parent, View child, int top) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(top);
        parent.addView(child, params);
    }
    private void weighted(LinearLayout parent, View child) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, -2, 1);
        params.setMargins(dp(3), 0, dp(3), 0);
        parent.addView(child, params);
    }
}
