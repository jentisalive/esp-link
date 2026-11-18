import SwiftUI

private enum Palette {
    static let background = Color(red: 0.035, green: 0.055, blue: 0.075)
    static let card = Color(red: 0.07, green: 0.095, blue: 0.12)
    static let mint = Color(red: 0.47, green: 0.96, blue: 0.74)
    static let muted = Color(red: 0.61, green: 0.68, blue: 0.73)
}

struct ContentView: View {
    @StateObject private var model = AppModel()
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                header
                connectionCard
                commandCard
                consoleCard
                Text("uPesy ESP32 Wroom DevKit · GPIO 2\nРаботает локально. Облако и аккаунт не нужны.")
                    .font(.caption).foregroundStyle(Palette.muted)
                    .frame(maxWidth: .infinity).multilineTextAlignment(.center)
            }
            .padding(20)
            .frame(maxWidth: 680)
            .frame(maxWidth: .infinity)
        }
        .background(Palette.background.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .tint(Palette.mint)
        .onChange(of: model.transport) { _ in model.switchTransport() }
        .onChange(of: scenePhase) { phase in
            // This controller intentionally does not run commands in the background.
            if phase == .background { model.disconnect() }
        }
        .alert("ESP Link", isPresented: Binding(
            get: { model.alert != nil },
            set: { if !$0 { model.alert = nil } }
        )) {
            Button("Понятно", role: .cancel) { model.alert = nil }
        } message: { Text(model.alert ?? "") }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Label("DEVICE CONTROL", systemImage: "cpu")
                    .font(.system(size: 11, weight: .semibold, design: .monospaced))
                    .tracking(2).foregroundStyle(Palette.mint)
                Spacer()
                Text("LOCAL / 01").font(.system(size: 10, design: .monospaced)).foregroundStyle(Palette.muted)
            }
            HStack(alignment: .firstTextBaseline, spacing: 0) {
                Text("ESP ").foregroundStyle(.white)
                Text("Link").foregroundStyle(Palette.mint)
            }
            .font(.system(size: 48, weight: .bold, design: .rounded))
            Text("Твоя плата. Твои команды.")
                .font(.subheadline).foregroundStyle(Palette.muted)
        }.padding(.vertical, 8)
    }

    private var connectionCard: some View {
        VStack(alignment: .leading, spacing: 18) {
            sectionTitle("01", "Подключение", icon: "antenna.radiowaves.left.and.right")
            Picker("Способ подключения", selection: $model.transport) {
                ForEach(Transport.allCases) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented)

            HStack(alignment: .top, spacing: 10) {
                Circle().fill(model.isReady ? Palette.mint : Color.orange)
                    .frame(width: 8, height: 8).padding(.top, 5)
                VStack(alignment: .leading, spacing: 4) {
                    if model.isReady { Text(model.deviceName).font(.headline) }
                    Text(model.status).font(.caption).foregroundStyle(Palette.muted)
                }
                Spacer(minLength: 0)
                if model.isConnecting { ProgressView().controlSize(.small) }
            }
            if model.isReady || model.isConnecting {
                Button("Отключиться", role: .destructive) { model.disconnect() }
                    .font(.subheadline)
            } else {
                VStack(alignment: .leading, spacing: 7) {
                    Text("ТОКЕН УСТРОЙСТВА").font(.system(size: 10, weight: .medium, design: .monospaced)).foregroundStyle(Palette.muted)
                    SecureField("12 символов из Serial Monitor", text: $model.token)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                        .font(.system(.body, design: .monospaced))
                        .padding(12).background(Palette.background, in: RoundedRectangle(cornerRadius: 10))
                        .accessibilityLabel("Токен ESP32")
                }
                if model.transport == .bluetooth {
                    bluetoothControls
                } else {
                    wifiControls
                }
            }
        }.cardStyle()
    }

    private var bluetoothControls: some View {
        VStack(alignment: .leading, spacing: 12) {
            Button {
                if model.bluetooth.isScanning { model.bluetooth.stopScan() }
                else { model.bluetooth.scan() }
            } label: {
                HStack {
                    if model.bluetooth.isScanning { ProgressView().controlSize(.small) }
                    Image(systemName: "dot.radiowaves.left.and.right")
                    Text(model.bluetooth.isScanning ? "Остановить поиск" : "Найти ESP32")
                }.frame(maxWidth: .infinity).padding(.vertical, 5)
            }.buttonStyle(.borderedProminent).foregroundStyle(Palette.background)
            ForEach(model.bluetooth.boards) { board in
                Button { model.connect(board: board) } label: {
                    HStack(spacing: 12) {
                        Image(systemName: "cpu").font(.title2).foregroundStyle(Palette.mint)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(board.name).font(.subheadline.weight(.semibold)).foregroundStyle(.white)
                            Text("\(board.rssi) dBm · нажмите для подключения").font(.caption2).foregroundStyle(Palette.muted)
                        }
                        Spacer()
                        Image(systemName: "chevron.right").font(.caption).foregroundStyle(Palette.muted)
                    }.padding(12).background(Palette.background, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain)
            }
            Text("Сопряжение через настройки iPhone не нужно. Плата появится здесь после загрузки прошивки ESP Link.")
                .font(.caption).foregroundStyle(Palette.muted)
        }
    }

    private var wifiControls: some View {
        VStack(alignment: .leading, spacing: 12) {
            TextField("192.168.4.1", text: $model.host)
                .textInputAutocapitalization(.never).autocorrectionDisabled()
                .keyboardType(.URL).font(.system(.body, design: .monospaced))
                .padding(12).background(Palette.background, in: RoundedRectangle(cornerRadius: 10))
                .accessibilityLabel("Локальный адрес ESP32")
            Button { model.connect() } label: {
                Label("Подключиться к плате", systemImage: "wifi")
                    .frame(maxWidth: .infinity).padding(.vertical, 5)
            }.buttonStyle(.borderedProminent).foregroundStyle(Palette.background)
            Text("1. В Настройки → Wi-Fi выберите ESP32-Link-XXXX.\n2. Пароль сети совпадает с токеном из USB Serial Monitor.\n3. Вернитесь сюда и подключитесь. Интернет не требуется.")
                .font(.caption).foregroundStyle(Palette.muted).lineSpacing(4)
        }
    }

    private var commandCard: some View {
        VStack(alignment: .leading, spacing: 18) {
            sectionTitle("02", "Команды", icon: "terminal")
            Button { model.send("1") } label: {
                HStack(spacing: 14) {
                    Image(systemName: "lightbulb.max.fill").font(.system(size: 29))
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Мигнуть 3 раза").font(.headline)
                        Text("Команда 1 · встроенный LED").font(.caption).opacity(0.7)
                    }
                    Spacer()
                    if model.isSending { ProgressView().tint(Palette.background) }
                    else { Image(systemName: "arrow.up.right").font(.title3) }
                }.padding(18).foregroundStyle(Palette.background)
                    .background(Palette.mint, in: RoundedRectangle(cornerRadius: 16))
            }.buttonStyle(.plain)
            HStack(spacing: 10) {
                quickButton("Включить", command: "on", icon: "sun.max")
                quickButton("Выключить", command: "off", icon: "moon")
                quickButton("Проверка", command: "ping", icon: "waveform.path")
            }
            HStack(spacing: 12) {
                TextField("Команда, например 1", text: $model.command)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .font(.system(.body, design: .monospaced))
                    .submitLabel(.send).onSubmit { model.send() }
                Button { model.send() } label: {
                    Image(systemName: "arrow.up").font(.headline).frame(width: 38, height: 38)
                }.buttonStyle(.borderedProminent).foregroundStyle(Palette.background)
                    .accessibilityLabel("Отправить команду")
            }.padding(10).background(Palette.background, in: RoundedRectangle(cornerRadius: 12))
            Text(model.isReady ? "1 = три мигания · on · off · status · ping" : "Подключите плату, чтобы отправлять команды")
                .font(.caption).foregroundStyle(Palette.muted)
        }
        .disabled(!model.isReady || model.isSending)
        .opacity(model.isReady ? 1 : 0.55)
        .cardStyle()
    }

    private var consoleCard: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                sectionTitle("03", "Журнал", icon: "text.alignleft")
                Spacer()
                Button("Очистить") { model.clearLog() }.font(.caption).foregroundStyle(Palette.muted)
            }
            if model.logs.isEmpty {
                Text("Здесь появятся команды и ответы платы.\nНикаких автоматических повторов отправки.")
                    .font(.caption).foregroundStyle(Palette.muted).lineSpacing(5).padding(.vertical, 12)
            } else {
                LazyVStack(alignment: .leading, spacing: 12) {
                    ForEach(model.logs) { entry in
                        HStack(alignment: .top, spacing: 10) {
                            Text(entry.date, style: .time).foregroundStyle(Palette.muted)
                            Text(entry.text).foregroundStyle(entry.isError ? Color.orange : Palette.mint)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }.font(.system(size: 11, design: .monospaced))
                    }
                }.textSelection(.enabled)
            }
        }.cardStyle()
    }

    private func quickButton(_ title: String, command: String, icon: String) -> some View {
        Button { model.send(command) } label: {
            VStack(spacing: 8) {
                Image(systemName: icon).font(.title3)
                Text(title).font(.system(size: 10, weight: .medium))
            }.foregroundStyle(.white).frame(maxWidth: .infinity).padding(.vertical, 14)
                .background(Palette.background, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(.plain)
    }

    private func sectionTitle(_ number: String, _ title: String, icon: String) -> some View {
        HStack(spacing: 9) {
            Text(number).font(.system(size: 10, weight: .medium, design: .monospaced)).foregroundStyle(Palette.mint)
            Text(title).font(.headline)
            Image(systemName: icon).font(.caption).foregroundStyle(Palette.muted)
        }
    }
}

private extension View {
    func cardStyle() -> some View {
        padding(18)
            .background(Palette.card, in: RoundedRectangle(cornerRadius: 22))
            .overlay(RoundedRectangle(cornerRadius: 22).strokeBorder(.white.opacity(0.06)))
    }
}
