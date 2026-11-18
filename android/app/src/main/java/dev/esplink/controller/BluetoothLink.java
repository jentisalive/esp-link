package dev.esplink.controller;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

// Runtime permissions are requested by MainActivity before calling this class.
@SuppressLint("MissingPermission")
final class BluetoothLink {
    static final class Board {
        final BluetoothDevice device;
        final String name;
        final int rssi;
        Board(BluetoothDevice device, String name, int rssi) {
            this.device = device; this.name = name; this.rssi = rssi;
        }
    }

    private final Context context;
    private final LinkListener listener;
    private final Consumer<List<Board>> onBoards;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Board> boards = new LinkedHashMap<>();
    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private String token = "";
    private boolean ready;
    private boolean scanning;
    private int generation;
    private Runnable connectionTimeout;
    private final Runnable scanTimeout = this::stopScan;

    BluetoothLink(Context context, LinkListener listener, Consumer<List<Board>> onBoards) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        this.onBoards = onBoards;
    }

    boolean isScanning() { return scanning; }

    void scan() {
        disconnect();
        boards.clear();
        onBoards.accept(new ArrayList<>());
        BluetoothManager manager = context.getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            fail("Включите Bluetooth в настройках телефона."); return;
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) { fail("Bluetooth LE недоступен."); return; }
        int id = generation;
        scanCallback = new ScanCallback() {
            @Override public void onScanResult(int callbackType, ScanResult result) {
                main.post(() -> {
                    if (generation != id || !scanning) return;
                    ScanRecord record = result.getScanRecord();
                    String name = record == null ? null : record.getDeviceName();
                    if (name == null) name = result.getDevice().getName();
                    if (name == null) name = "ESP32 Link";
                    boards.put(result.getDevice().getAddress(), new Board(result.getDevice(), name, result.getRssi()));
                    List<Board> sorted = new ArrayList<>(boards.values());
                    sorted.sort((a, b) -> Integer.compare(b.rssi, a.rssi));
                    onBoards.accept(sorted);
                });
            }
            @Override public void onScanFailed(int errorCode) {
                main.post(() -> { if (generation == id) fail("Ошибка поиска BLE: " + errorCode + ". Подождите и повторите поиск."); });
            }
        };
        try {
            scanning = true;
            scanner.startScan(Collections.singletonList(new ScanFilter.Builder()
                            .setServiceUuid(new ParcelUuid(CommandProtocol.SERVICE)).build()),
                    new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback);
            listener.onState("Ищем платы поблизости…", false, false);
            main.postDelayed(scanTimeout, 10_000);
        } catch (SecurityException | IllegalStateException error) {
            fail("Не удалось начать поиск. Проверьте разрешения Bluetooth.");
        }
    }

    void stopScan() {
        main.removeCallbacks(scanTimeout);
        if (scanner != null && scanCallback != null) {
            try { scanner.stopScan(scanCallback); } catch (SecurityException | IllegalStateException ignored) { }
        }
        scanner = null;
        scanCallback = null;
        boolean wasScanning = scanning;
        scanning = false;
        if (wasScanning) listener.onState(boards.isEmpty() ? "Платы не найдены. Проверьте питание и прошивку."
                : "Выберите свою плату", false, false);
    }

    void connect(Board board, String deviceToken) {
        disconnect();
        token = deviceToken;
        listener.onState("Подключаемся к " + board.name + "…", false, true);
        try {
            gatt = board.device.connectGatt(context, false, callbacks, BluetoothDevice.TRANSPORT_LE);
            if (gatt == null) { fail("Не удалось создать соединение BLE."); return; }
            int id = generation;
            connectionTimeout = () -> {
                if (generation == id && !ready) fail("Нет ответа за 15 секунд. Проверьте питание, токен и прошивку.");
            };
            main.postDelayed(connectionTimeout, 15_000);
        } catch (SecurityException error) { fail("Разрешите доступ к устройствам поблизости."); }
    }

    void send(String command) {
        if (!ready) throw new IllegalStateException("Сначала подключитесь к ESP32.");
        write(command);
    }

    @SuppressWarnings("deprecation")
    private void write(String text) {
        if (gatt == null || rx == null) throw new IllegalStateException("Нет соединения BLE.");
        byte[] data = text.getBytes(StandardCharsets.US_ASCII);
        boolean accepted;
        if (Build.VERSION.SDK_INT >= 33) {
            accepted = gatt.writeCharacteristic(rx, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                    == BluetoothStatusCodes.SUCCESS;
        } else {
            rx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            rx.setValue(data);
            accepted = gatt.writeCharacteristic(rx);
        }
        if (!accepted) throw new IllegalStateException("BLE занят или запись отклонена. Автоматического повтора нет.");
    }

    void disconnect() {
        generation++;
        stopScan();
        if (connectionTimeout != null) main.removeCallbacks(connectionTimeout);
        connectionTimeout = null;
        BluetoothGatt old = gatt;
        gatt = null;
        rx = null;
        token = "";
        ready = false;
        if (old != null) {
            try { old.disconnect(); } catch (SecurityException ignored) { }
            try { old.close(); } catch (SecurityException ignored) { }
        }
        listener.onState("Не подключено", false, false);
    }

    private void fail(String message) {
        disconnect();
        listener.onState(message, false, false);
        listener.onError(message);
    }

    private void receive(BluetoothGatt source, byte[] value) {
        main.post(() -> {
            if (source != gatt) return;
            String reply = new String(value, StandardCharsets.US_ASCII);
            if (reply.equals("error:auth")) { fail(CommandProtocol.explain(reply)); return; }
            if (reply.equals("ready")) {
                if (connectionTimeout != null) main.removeCallbacks(connectionTimeout);
                connectionTimeout = null;
                token = "";
                ready = true;
                listener.onState("Подключено по Bluetooth LE", true, false);
            }
            listener.onReply(reply);
        });
    }

    private final BluetoothGattCallback callbacks = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt source, int status, int state) {
            main.post(() -> {
                if (source != gatt) return;
                if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) {
                    fail("Связь BLE прервана (код " + status + "). Подключитесь заново."); return;
                }
                if (state == BluetoothProfile.STATE_CONNECTED) {
                    listener.onState("Проверяем сервис и токен…", false, true);
                    if (!source.discoverServices()) fail("Не удалось запросить сервисы BLE.");
                }
            });
        }

        @Override @SuppressWarnings("deprecation")
        public void onServicesDiscovered(BluetoothGatt source, int status) {
            main.post(() -> {
                if (source != gatt) return;
                BluetoothGattService service = source.getService(CommandProtocol.SERVICE);
                if (status != BluetoothGatt.GATT_SUCCESS || service == null) {
                    fail("Сервис ESP Link не найден. Загрузите прошивку проекта, не тестовый скетч."); return;
                }
                rx = service.getCharacteristic(CommandProtocol.RX);
                BluetoothGattCharacteristic tx = service.getCharacteristic(CommandProtocol.TX);
                if (rx == null || tx == null || (rx.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE) == 0
                        || (tx.getProperties() & BluetoothGattCharacteristic.PROPERTY_NOTIFY) == 0) {
                    fail("Несовместимая прошивка BLE."); return;
                }
                BluetoothGattDescriptor cccd = tx.getDescriptor(CommandProtocol.CCCD);
                if (cccd == null || !source.setCharacteristicNotification(tx, true)) {
                    fail("Плата не разрешила уведомления BLE."); return;
                }
                boolean accepted;
                if (Build.VERSION.SDK_INT >= 33) {
                    accepted = source.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                            == BluetoothStatusCodes.SUCCESS;
                } else {
                    cccd.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                    accepted = source.writeDescriptor(cccd);
                }
                if (!accepted) fail("Не удалось включить уведомления BLE.");
            });
        }

        @Override public void onDescriptorWrite(BluetoothGatt source, BluetoothGattDescriptor descriptor, int status) {
            main.post(() -> {
                if (source != gatt || !descriptor.getUuid().equals(CommandProtocol.CCCD)) return;
                if (status != BluetoothGatt.GATT_SUCCESS) { fail("Не удалось подписаться на ответы платы."); return; }
                try { write("auth:" + token); } catch (RuntimeException error) { fail(error.getMessage()); }
            });
        }

        @Override public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic, byte[] value) {
            if (characteristic.getUuid().equals(CommandProtocol.TX)) receive(source, value.clone());
        }

        @Override @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt source, BluetoothGattCharacteristic characteristic) {
            if (characteristic.getUuid().equals(CommandProtocol.TX) && characteristic.getValue() != null) {
                receive(source, characteristic.getValue().clone());
            }
        }

        @Override public void onCharacteristicWrite(BluetoothGatt source, BluetoothGattCharacteristic characteristic, int status) {
            main.post(() -> {
                if (source == gatt && status != BluetoothGatt.GATT_SUCCESS) fail("Ошибка записи BLE: " + status + ". Повтор — только вручную.");
            });
        }
    };
}
