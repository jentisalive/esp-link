import Combine
import CoreBluetooth
import Foundation

struct NearbyBoard: Identifiable {
    let peripheral: CBPeripheral
    var name: String
    var rssi: Int
    var id: UUID { peripheral.identifier }
}

// CoreBluetooth callbacks and all callers run on the main queue.
final class BluetoothLink: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published private(set) var boards: [NearbyBoard] = []
    @Published private(set) var isScanning = false
    @Published private(set) var isConnecting = false
    @Published private(set) var isReady = false
    @Published private(set) var status = "Bluetooth LE · без сопряжения в настройках"
    @Published private(set) var deviceName = "ESP32"

    var onReply: ((String) -> Void)?
    var onError: ((String) -> Void)?
    private var central: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?
    private var tx: CBCharacteristic?
    private var token = ""
    private var wantsScan = false
    private var scanTimeout: DispatchWorkItem?
    private var connectionTimeout: DispatchWorkItem?

    func scan() {
        disconnect()
        boards = []
        wantsScan = true
        if central == nil {
            central = CBCentralManager(delegate: self, queue: .main)
        } else if central?.state == .poweredOn {
            startScan()
        } else {
            centralManagerDidUpdateState(central!)
        }
    }

    private func startScan() {
        wantsScan = false
        isScanning = true
        status = "Ищем платы поблизости…"
        central?.scanForPeripherals(withServices: [CBUUID(string: CommandProtocol.service)])
        let timeout = DispatchWorkItem { [weak self] in self?.stopScan() }
        scanTimeout = timeout
        DispatchQueue.main.asyncAfter(deadline: .now() + 10, execute: timeout)
    }

    func stopScan() {
        wantsScan = false
        scanTimeout?.cancel()
        scanTimeout = nil
        central?.stopScan()
        isScanning = false
        if !isConnecting && !isReady {
            status = boards.isEmpty ? "Платы не найдены. Проверьте питание и прошивку." : "Выберите свою плату"
        }
    }

    func connect(_ board: NearbyBoard, token: String) {
        disconnect()
        guard central?.state == .poweredOn else {
            onError?("Включите Bluetooth на iPhone.")
            return
        }
        self.token = token
        peripheral = board.peripheral
        deviceName = board.name
        board.peripheral.delegate = self
        isConnecting = true
        status = "Подключаемся к \(board.name)…"
        central?.connect(board.peripheral)
        let timeout = DispatchWorkItem { [weak self] in
            self?.fail("Плата не ответила за 15 секунд. Проверьте токен и попробуйте ещё раз.")
        }
        connectionTimeout = timeout
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: timeout)
    }

    func disconnect() {
        stopScan()
        connectionTimeout?.cancel()
        connectionTimeout = nil
        let old = peripheral
        peripheral = nil
        rx = nil
        tx = nil
        token = ""
        isReady = false
        isConnecting = false
        old?.delegate = nil
        if let old { central?.cancelPeripheralConnection(old) }
        status = "Не подключено"
    }

    func send(_ command: String) throws {
        guard isReady, let peripheral, let rx else {
            throw LinkError.message("Сначала подключитесь к плате.")
        }
        try write(command, peripheral: peripheral, characteristic: rx)
    }

    private func write(_ text: String, peripheral: CBPeripheral, characteristic: CBCharacteristic) throws {
        let data = Data(text.utf8)
        guard data.count <= peripheral.maximumWriteValueLength(for: .withResponse) else {
            throw LinkError.message("Команда слишком длинная для BLE.")
        }
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }

    private func fail(_ text: String) {
        disconnect()
        status = text
        onError?(text)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            if wantsScan { startScan() }
        case .unknown, .resetting:
            status = "Bluetooth запускается…"
        default:
            let message: String
            switch central.state {
            case .unauthorized: message = "Разрешите Bluetooth в Настройки → ESP Link."
            case .poweredOff: message = "Включите Bluetooth на iPhone."
            default: message = "Bluetooth LE недоступен. Нужен настоящий iPhone, не симулятор."
            }
            disconnect()
            status = message
            onError?(message)
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? "ESP32 Link"
        let board = NearbyBoard(peripheral: peripheral, name: name, rssi: RSSI.intValue)
        if let index = boards.firstIndex(where: { $0.id == board.id }) {
            boards[index] = board
        } else {
            boards.append(board)
        }
        boards.sort { $0.rssi > $1.rssi }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard self.peripheral === peripheral else { return }
        status = "Проверяем сервис и токен…"
        peripheral.discoverServices([CBUUID(string: CommandProtocol.service)])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        guard self.peripheral === peripheral else { return }
        fail(error?.localizedDescription ?? "Не удалось подключиться к ESP32.")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        guard self.peripheral === peripheral else { return }
        fail("Связь с платой потеряна. Подключитесь снова.")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil, let service = peripheral.services?.first(where: {
            $0.uuid == CBUUID(string: CommandProtocol.service)
        }) else { fail("На плате нет сервиса ESP Link. Установите прошивку из проекта."); return }
        peripheral.discoverCharacteristics([CBUUID(string: CommandProtocol.rx), CBUUID(string: CommandProtocol.tx)], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil else { fail("Не удалось прочитать характеристики BLE."); return }
        rx = service.characteristics?.first { $0.uuid == CBUUID(string: CommandProtocol.rx) }
        tx = service.characteristics?.first { $0.uuid == CBUUID(string: CommandProtocol.tx) }
        guard let rx, rx.properties.contains(.write), let tx, tx.properties.contains(.notify) else {
            fail("Несовместимая прошивка BLE."); return
        }
        peripheral.setNotifyValue(true, for: tx)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral, characteristic.uuid == CBUUID(string: CommandProtocol.tx) else { return }
        guard error == nil, characteristic.isNotifying, let rx else {
            fail("Плата не разрешила уведомления BLE."); return
        }
        do { try write("auth:\(token)", peripheral: peripheral, characteristic: rx) }
        catch { fail(error.localizedDescription) }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral, characteristic.uuid == CBUUID(string: CommandProtocol.tx) else { return }
        guard error == nil, let data = characteristic.value, let reply = String(data: data, encoding: .utf8) else {
            fail("Не удалось прочитать ответ платы."); return
        }
        if reply == "error:auth" { fail(CommandProtocol.explain(reply)); return }
        if reply == "ready" {
            connectionTimeout?.cancel()
            connectionTimeout = nil
            isReady = true
            isConnecting = false
            status = "Подключено по Bluetooth LE"
            token = ""
        }
        onReply?(reply)
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral else { return }
        if let error { fail("Ошибка записи BLE: \(error.localizedDescription)") }
    }

    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        guard self.peripheral === peripheral else { return }
        fail("Сервисы платы изменились. Подключитесь снова.")
    }
}
