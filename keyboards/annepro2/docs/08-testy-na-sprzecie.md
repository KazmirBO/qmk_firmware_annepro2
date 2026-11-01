# Testy na sprzęcie

Testy wykonał użytkownik na jednej klawiaturze Anne Pro 2 C18 z hostem po BLE. Źródłem są jego relacje w trakcie pracy i zrzuty ekranu z hosta; nie są to pomiary ani nagrania ruchu. Kolumna „Wersja” podaje commit forka `qmk_firmware_annepro2`, z którego pochodził wgrany obraz; obraz dla przełącznika Bluetooth ma ten sam SHA-256 co build `6b71633911`, a w pozostałych wierszach commit przypisano na podstawie opisu wersji w relacji („po parserze”, „po refaktoryzacji” itd.).

## 1. Wyniki

| Funkcja | Wersja | Wynik |
|---|---|---|
| Parowanie i łączenie po BLE (`40 01`, `40 04`) | `8b1bc7b9be` (parser i handshake) | działa |
| Caps Lock przez BLE, gaszenie migania po połączeniu, tap/hold slotów, przełączanie USB i BLE, multimedia, stabilność | `8b1bc7b9be` | bez zgłoszonej niezgodności; punkty nie były raportowane osobno |
| Poziom baterii u hosta (`01 05`, ADC na PA7) | `354db088eb` | host pokazał 73 %, później odczyt GATT dał 67 % |
| Poziom baterii po ponownym starcie BLE | `5bf72bff35` | 90 % |
| `KC_AP2_BATT` (`Fn2` + `8`) | `5bf72bff35` | wskaźnik na diodach działa |
| Klawisze jasności przez BLE (`Fn1` + `M`, `Fn1` + `,`) | `099f766757` | działają |
| Wgranie przez `annepro2_tools --boot` i start | `5bf72bff35` | klawiatura działa |
| `annepro2_tools --info` w trybie IAP (tylko odczyt) | Tools `006d480` | tryb 1 (IAP) dla MCU, LED i BLE; trzy rekordy z bazą 0x4000 i rozmiarem 0x1fe00 (`06-obinskit-i-bootloader.md`) |
| Wgranie `18355aa8ba` (korekta 0 % baterii) | `18355aa8ba`, 35 808 B | `Flash complete`, klawiatura wstała, po BLE bateria 92 % |
| Przełącznik Bluetooth (`AP2_BT_SWITCH` w `Kazmir_BO`), 2026-10-26 | `6b71633911`, 31 500 B | Off daje USB, On daje Bluetooth, zgodnie z analizą oryginału |
| Odczyt GATT połączonej klawiatury (BlueZ przez D-Bus, tylko odczyt) | firmware QMK | wyniki w `05-ble.md` |

Co to potwierdza z analizy oryginału: format ramek, polecenia `40 01`, `40 04`, `10 04`, `10 08` i zdarzenia `20 07`, `20 0c` (funkcjonalnie, nie bajt po bajcie); format `01 05` (surowy odczyt ADC, 16 bitów, little-endian) i konfigurację ADC (kanał 7, `HCLK/64`); bity jasności w raporcie multimediów (0x40, 0x80); kierunek przełącznika Bluetooth na PC10; układ odpowiedzi bootloadera na `02 03`.

## 2. Czego nie sprawdzono

- Wersji `6c03dfe813` (2026-10-27) jeszcze nie wgrywano: ramki budzącej BLE w formacie oryginału, numeru seryjnego DIS z UDID (`AP2_BLE_SERIAL`) i wskaźnika baterii z krzywą BLE. Procedura w §3.1.
- Korekty 0 % baterii (`AP2_BATTERY_EMPTY_MV`) przy niskim napięciu.
- Zgodności procentu baterii z napięciem zmierzonym multimetrem (napięcie odniesienia 3,1 V i dzielnik 2:1 pochodzą z analizy firmware).
- Wersji układu BLE odebranej przez port: `annepro2_ble_version()` nie jest nigdzie wyświetlana.
- Parametrów połączenia i dystrybucji kluczy (nagranie `btmon`).
- Ruchu UART bajt po bajcie.
- Wgrywania obrazu BLE.

## 3. Procedury

### 3.1. Po wgraniu nowego obrazu

1. Caps Lock przez BLE (dioda) i gaszenie migania slotu po połączeniu.
2. Klawisze slotów: krótkie naciśnięcie łączy, przytrzymanie (`AP2_BLE_SLOT_HOLD_MS`) paruje.
3. Przełącznik Bluetooth, jeśli włączony: Off daje USB, On Bluetooth, powrót na Off gasi wskaźnik slotu; start z przełącznikiem na On pisze od razu przez Bluetooth.
4. Multimedia i jasność po BLE, `KC_AP2_BATT`, poziom baterii u hosta; liczba zapalonych klawiszy wskaźnika ma odpowiadać procentowi u hosta (jeden klawisz na 10 %, zaokrąglenie do najbliższego).
5. Z `AP2_BLE_SERIAL`: Device Information (Serial Number) po ponownym połączeniu pokazuje 32 znaki hex UDID zamiast `0123456789ABCDEF0123456789ABCDEF`; host może trzymać stare dane GATT w pamięci podręcznej, wtedy pomaga ponowne sparowanie.

### 3.2. Nagranie btmon

Cel: parametry połączenia, które host faktycznie dostaje (PPCP w GATT kontra żądanie aktualizacji z kodu BLE, `05-ble.md`), oraz przebieg parowania (Just Works, rozdzielane klucze). `btmon` wymaga uprawnień roota.

1. W jednym terminalu: `sudo btmon -w /tmp/annepro2.btsnoop`.
2. W drugim: odłącz i podłącz klawiaturę po BLE (krótkie naciśnięcie slotu). Do sprawdzenia parowania użyj slotu, którego nie używasz na tym komputerze: najpierw `bluetoothctl remove <adres>` dla starego wpisu, potem przytrzymanie slotu i parowanie od nowa.
3. Zatrzymaj `btmon` (Ctrl+C) i zachowaj plik do analizy.

Parowanie na slocie zastępuje bond tego slotu i nie rusza pozostałych. `40 05` (`KC_AP2_BT_UNPAIR`, `deleteBleHost` w ObinsKit) kasuje bondy wszystkich slotów, więc go nie używaj.

### 3.3. Podsłuch UART

Najtańsze potwierdzenie ramek bajt po bajcie: analizator stanów logicznych na liniach MCU-BLE (PA4/PA5, USART1) albo MCU-LED (PB0/PB1, USART0), 115200 8N1, i porównanie z `02-protokol.md`. Alternatywą dla poleceń konfiguracji jest przechwycenie ruchu USB HID ObinsKit.
