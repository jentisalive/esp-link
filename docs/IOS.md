# Сборка и установка iOS

## Вариант A: Windows + unsigned IPA

1. Загрузите проект в GitHub и дождитесь успешного **Build and test**.
2. Скачайте артефакт `ESPLink-iOS-unsigned`, распакуйте.
3. Подпишите `ESPLink-unsigned.ipa` и установите на свой iPhone через выбранный инструмент, например **Sideloadly** на Windows или **AltStore**. Скачивайте эти программы только с официальных сайтов; это сторонние продукты, проект ими не управляет.
4. Выполните инструкции инструмента по доверию профилю и включению **Developer Mode**, если iOS этого потребует.
5. При первом поиске BLE разрешите Bluetooth; при работе по Wi-Fi — локальную сеть.

С личным бесплатным Apple ID подпись обычно действует 7 дней. Для обновления подписи понадобится повторное подключение/обновление через выбранный инструмент. Не вводите Apple ID в приложение ESP Link и не присылайте пароль разработчику.

`ESPLink-simulator.zip` предназначен только для iOS Simulator на Mac, **не для телефона**. В нём можно смотреть интерфейс; BLE на симуляторе недоступен.

## Вариант B: Mac + Xcode

Установите Xcode с iOS SDK и XcodeGen:

```sh
brew install xcodegen
cd ios
xcodegen generate
open ESPLink.xcodeproj
```

В target **ESPLink → Signing & Capabilities** выберите свою Team и уникальный Bundle Identifier, например `com.yourname.esplink`. Подключите iPhone и нажмите Run. Генерируемый `.xcodeproj` не хранится в Git: долгосрочные изменения вносите в `project.yml`.

Тесты в Xcode: Product → Test. Или:

```sh
xcodebuild test -project ESPLink.xcodeproj -scheme ESPLink \
  -destination 'platform=iOS Simulator,name=ИМЯ_УСТАНОВЛЕННОГО_IPHONE' \
  CODE_SIGNING_ALLOWED=NO
```

## Вариант C: подпись в GitHub Actions

Workflow **Signed iOS IPA (manual)** требует сертификата Apple с приватным ключом и соответствующего provisioning profile. Обычно для управляемых профилей и Ad Hoc/TestFlight нужен платный Apple Developer Program. Он не обходит правила Apple и не создаёт аккаунт/сертификаты автоматически.

В GitHub → Settings → Secrets and variables → Actions добавьте:

| Secret | Содержимое |
| --- | --- |
| `IOS_CERTIFICATE_BASE64` | Base64 сертификата `.p12` с приватным ключом |
| `IOS_CERTIFICATE_PASSWORD` | Пароль `.p12` (может быть пустым для сертификата без пароля) |
| `IOS_PROFILE_BASE64` | Base64 `.mobileprovision` |

Base64 на Windows:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('C:\path\certificate.p12')) | Set-Clipboard
```

Повторите для provisioning profile. Base64 — **не шифрование**. Не вставляйте результат в код, issue, лог или чат.

Запустите workflow с `bundle_id`, в точности совпадающим с **explicit App ID** профиля. Скрипт проверяет срок, App ID и совпадение сертификата. Нужны подходящие по сроку сертификат и профиль. Сам profile может содержать UDID зарегистрированных устройств; храните его как секрет.

- Development profile → export `debugging`;
- Ad Hoc profile → `release-testing`, установка только на устройства с UDID в профиле;
- App Store profile → `app-store-connect`, **не для прямой установки**: потребуется загрузка в App Store Connect/TestFlight, настройка приложения и требования Apple;
- Enterprise-профили в этом примере не поддерживаются.

Артефакт `ESPLink-iOS-signed` хранится 7 дней. Workflow **не загружает приложение в TestFlight/App Store**. Это прототип: перед публикацией в App Store также нужны финальная иконка приложения, оформление, сведения о приватности и проверка на реальном устройстве.

Приватные ключи импортируются в временный keychain runner и удаляются после выполнения. Выполняйте workflow только из доверенной ветки: изменённый workflow с доступом к Secrets может их украсть.

## Примечание о локальном HTTP

ESP32 обслуживает обычный HTTP на локальном IP. В `Info.plist` разрешён HTTP (`NSAllowsArbitraryLoads`); код принимает только частные IPv4-адреса или имена `.local` и запрещает перенаправления, чтобы не пересылать токен на страницу авторизации Wi-Fi. Для публичного сервиса это разрешение не подходит: нужен TLS и другая модель аутентификации.
