# ObinsKit 1.2.11 i bootloader

Znaczniki i konwencje: `README.md` §2.

## 1. Źródło i metoda

- Archiwum `ObinsKit_1.2.11_x64.tar.gz` (SHA-256 `2a10aed5366c2667174525924da60e323b7e200e2aa9abc1c1a6335e792096cd`), z niego `resources/app.asar` (SHA-256 `e1fad52c6adbf27e104181266e48f579c607ee5afd02c6ed006503887acbed22`) i moduł natywny `node_modules/@hexcore/node-hid/out/Release/HID_hidraw.node` z wnętrza asar (ELF x86-64 z symbolami, SHA-256 `b48ab46cb46e543df9e9dfe93d8d1d4f438d3758fc3480850bc179b7ced0cf83`).
- Analiza statyczna: odczyt asar własnym czytnikiem, lektura kodu JavaScript (`out/backstage.js`, `out/async-commons.js`, `out/ui.js`) i lokalizacji (`resources/locales/en.yml`), deasemblacja modułu natywnego `objdump`. Aplikacji ani modułów nie uruchamiano.

## 2. Transport

- Węzły: `USB_HOST=1`, `BLE_HOST=2`, `MCU_MAIN=3`, `MCU_LED=4`, `MCU_BLE=5` [O].
- Moduł `HID_hidraw.node` (klasa `Liana`) buduje ramki w formacie z `02-protokol.md` §1. Po USB: typ `0x10`, nadawca 1, do 53 B danych w ramce, raport 64 B. Po BLE: typ `0x12`, nadawca 2, raport ID 2, do 8 B danych, raport 19 B (ID i 18 B). Grupa `0x02` zawsze ma typ `0x10` [O].
- Z deasemblacji `multi_package_create` wynika, że krótsze ramki nie są dopełniane do pełnej długości raportu [I].
- JavaScript wysyła polecenia wyłącznie przez nazwę (`writeTo`); `Liana::writeWithName` zamienia nazwę na adresata, `cmd` i `sub`, a 13 nazw ma osobne funkcje z adresatem z argumentu [O].

## 3. Nazwy poleceń

Kolumna „MCU 2.36” mówi, czy fabryczny odbiorca obsługuje polecenie (`02-protokol.md` §5). Wszystkie wiersze [O].

| Nazwa | Adresat | `cmd sub` | Obsługa |
|---|---|---|---|
| `readDeviceType` | MCU | `01 01` | tak (odpowiedź: model, typ) |
| `readPowerOnTime` | MCU | `01 02` | tak |
| `readFirmwareVersion` | z argumentu (ObinsKit podaje MCU) | `01 03` | tak (trzy wersje u16) |
| `readMcuUUID` | MCU | `01 04` | nie; ObinsKit 1.2.11 go nie wywołuje |
| `readBattery` | MCU | `01 05` | tak; ObinsKit przelicza surowy u16 wzorem `raw * 2 * 3,1 / 4095` |
| `readHardwareState` | MCU | `01 09` | tak |
| `resetPowerOnTime` | MCU | `01 0b` | tak |
| `writeBacklightAuto` | MCU | `01 0c` | nie; nie jest wywoływane |
| `readUDID` / `writeUDID` | MCU | `01 0d` / `01 0e` | tak |
| `writeSleepTimeout` | MCU | `01 0f` | tak; u16 LE w sekundach, suwak 0-109 daje 10-3600 s |
| `readBatteryPercentage` | MCU | `01 11` | nie; ObinsKit używa go tylko dla modelu Anne Pro 2 Discovery |
| `getCombinationMode` | MCU | `01 f1` | nie |
| `writeIapMode` | z argumentu | `02 01` | tak (`02 01 01` od komputera po USB) |
| `readIapMode` | z argumentu | `02 02` | tak (odpowiedź 2 w aplikacji, 1 w bootloaderze) |
| `readIapFwVersion` | MCU | `02 03` | bootloader |
| `writeIapMemory` | z argumentu | `02 31` | bootloader |
| `writeEraseMemory` | z argumentu | `02 43` | bootloader |
| `downloadKbdLayout` / `uploadKbdLayout` | MCU | `10 01` / `10 02` | tak (zapis / odczyt mapy warstwy) |
| `writeKbdMagicFn`, `writeKbdTap`, `writeKbdLayout`, `writeKbdCapslock` | MCU | `10 03`, `10 05`, `10 06`, `10 07` | tak |
| `writeKbdTapkeyActMode`, `writeKbdTapkeyModifykeyActMode`, `writeKbdTapkeyTimeout` | MCU | `10 11`, `10 12`, `10 13` | tak |
| `downloadLight` | MCU | `20 02` | tak |
| `previewLight` | LED | `20 03` | tak |
| `writeLightList` | MCU | `20 04` | tak |
| `writeLightHoldOn` | MCU | `20 0d` | tak; przełącznik „Auto Sleep”, warunek odliczania do snu (`03-mcu.md` §7) |
| `writeAudioVisualize` | LED | `20 11` | nie sprawdzono [N] |
| `writeIndicatorBrightness` | MCU | `20 14` | nie |
| `writeBacklightBrightness` | MCU | `20 15` | tak |
| `readLedIndicatorLocation` / `writeLedIndicatorLocation` | MCU | `20 18` / `20 19` | nie |
| `readLedIndicatorColor` / `writeLedIndicatorColor` | MCU | `20 1a` / `20 1b` | tak |
| `writeCapslockLedColor` / `readCapslockLedColor` | MCU | `20 1c` / `20 1d` | nie |
| `readLedPowerMode` / `writeLedPowerMode` | MCU | `20 1e` / `20 1f` | tak |
| `writeAudioVisualConfig`, `writeAudioVisualData` | LED | `21 01`, `21 02` | nie (pusta procedura `0x21` w LED) |
| `writeMacro` | MCU | `30 01` | nie |
| `writeMacroFn` | MCU | `30 03` | tak |
| `resetMacro` | MCU | `30 04` | nie |
| `downloadMacro` | MCU | `30 05` | tak |
| `readMacroConfig`, `readMacroActiveList`, `readMacroMap`, `readMacro` | MCU | `30 06`-`30 09` | tak |
| `clearMacro` | MCU | `30 0a` | tak |
| `deleteBleHost` | BLE | `40 05` | tak; przycisk „Clear” i przywracanie ustawień, usuwa bondy wszystkich slotów |
| `readBleMac` | BLE | `40 30` | tak (6 B); ObinsKit 1.2.11 go nie wywołuje |
| `writeVirtualMouseCursorSpeed`, `...WheelSpeed`, `...AcpanSpeed`, `readVirtualMouseConfig` | MCU | `60 01`, `60 02`, `60 03`, `60 05` | nie (brak grupy `0x60` w MCU 2.36) |
| `writeOledImg`, `writeOledScroll`, `writeOledDateTime`, `writeOledChar`, `writeOledMonitor` | MCU innego modelu (z wyświetlaczem) | `70 01`, `70 05`, `70 07`, `70 08`, `70 09` | nie dotyczy |

- Bez nazwy w ObinsKit są polecenia MCU `01 06`, `01 08`, `01 0a`, `03 01`, `12 01` oraz grupy `0x50` i `0x80` [O].
- Żadne polecenie ObinsKit nie każe MCU wysłać `01 05` do BLE; do BLE trafiają tylko `deleteBleHost`, `readBleMac` i polecenia z adresatem z argumentu [O].

## 4. Aktualizacja firmware

Przebieg z `out/backstage.js` [O]:

1. Pobranie obrazów z `https://releases.obins.net/<model>/firmware/<wersja>/<plik>` (plik `key-<wersja>.bin`, `led-<wersja>.bin`, `ble-<wersja>.bin` albo nazwa z listy wydań) i kontrola SHA-256. Przy braku pola `sha` kontroli nie ma, a przy niezgodności obraz jest pomijany bez błędu i aplikacja i tak kończy komunikatem „Update Firmware Success” [O, z lektury kodu].
2. Wejście w IAP: `writeIapMode(BLE, 1)`, po 60 ms `writeIapMode(LED, 1)`, po kolejnych 60 ms `writeIapMode(MCU, 1)`; błędy tych wywołań są tylko logowane.
3. Kontrola `readIapMode` dla MCU, LED i BLE: odpowiedź 2 (węzeł nadal w aplikacji) albo błąd odczytu przerywają aktualizację („iap-timeout”).
4. Dla każdego obrazu: `readIapFwVersion` (adres bazowy z bajtów 2-5 dla MCU, 12-15 dla LED, 22-25 dla BLE, little-endian), `writeEraseMemory` (`02 43`) pod tym adresem i zapis `writeIapMemory` (`02 31`) fragmentami po 48 B dla MCU i LED oraz 32 B dla BLE; ostatni fragment jest dopełniany zerami. Odpowiedź 1 oznacza błąd. Kolejność: MCU, LED, BLE.
5. `writeIapMode(MCU, 2)`, czyli powrót do aplikacji.

ObinsKit nie ma stałego adresu bazowego: bierze go za każdym razem z bootloadera.

## 5. Nazwy w interfejsie

- Procesory: „Application Processor”, „Light Processor”, „Bluetooth Processor” (`en.yml`); wersje pochodzą z `readFirmwareVersion` (`01-architektura.md` §1) [O].
- Kody klawiszy (enum `KC`): `KC_BLE_WL_1`-`4` = 200-203, `KC_BLE_WL_ON` = 204 (w lokalizacji błędnie nazwany jak slot 4), `KC_FN_WHEEL` = 207 („Game wheel”), `KC_BL_BATT` = 246 („Battery level”); nazwy klawiszy HID kończą się na 0xA1 (`KC_EXSEL`). Pełna tabela z działaniem w MCU: `03-mcu.md` §6 [O].
- Efekty: mapa numerów na nazwy i podział na grupy Auto i Trigger; numerów 20, 22, 26 i 27 ObinsKit nie zna (`04-led.md` §4) [O].

## 6. Bootloader

- Kod bootloaderów nie występuje w żadnym obrazie. MCU wchodzi w IAP po `02 01 01` od komputera po USB (sygnatura `0xfab2` pod `0x20001ffc` i reset); BLE po `02 01 01` resetuje się watchdogiem [O]. Port QMK opisuje też wejście przez przytrzymanie Esc przy podłączaniu.
- W trybie IAP C18 zgłasza się jako urządzenie USB VID 0x04D9, PID 0x8009 („USB-HID IAP”) [O, urządzenie]; dla C15 OpenAnnePro podaje PID 0x8008 [zewn.].
- Odczyt `annepro2_tools --info` (lokalnie zmodyfikowane AnnePro2-Tools, tylko odczyt) na C18 przed wgraniem firmware [O, urządzenie]:
  - `02 02` dla MCU, LED i BLE: 0x01, czyli wszystkie trzy w trybie IAP;
  - `02 03`, pierwsze 32 B danych: `7b 10 | 00 40 00 00 00 fe 01 00 01 00 | 00 40 00 00 00 fe 01 00 00 00 | 00 40 00 00 00 fe 01 00 00 00`;
  - trzy rekordy po 10 B od offsetu 2 (MCU, LED, BLE): baza u32 LE 0x4000, rozmiar u32 LE 0x1fe00 (130 560 B), dwa bajty flag (`01 00` dla MCU, `00 00` dla LED i BLE). Układ zgadza się z tym, jak czyta go ObinsKit (§4). Znaczenie dwóch pierwszych bajtów (`7b 10`) i flag jest nieznane [N].
- Aplikacja LED odpowiada na `02 03` własną emulacją (24 B, trzy rekordy po 8 B z 0x40 w bajcie 5), inną niż odpowiedź bootloadera [O].
- Baza 0x4000 zgadza się z obrazami MCU i LED. Dla BLE obraz zaczyna się od fizycznego adresu 0x2000, więc odwzorowanie bazy z bootloadera na flash CC254x nie jest znane, a wgrywania BLE nie testowano [N]. AnnePro2-Tools używa domyślnie bazy 0x4000 dla wszystkich trzech celów, zgodnie z odczytem [O].
