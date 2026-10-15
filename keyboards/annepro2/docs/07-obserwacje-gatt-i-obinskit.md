# Obserwacje z żywej klawiatury (GATT) i z ObinsKit 1.2.11

Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma. Wszystko z odczytu tylko do odczytu (BlueZ przez D-Bus) oraz z statycznej lektury `app.asar` (bez uruchamiania aplikacji).

## 1. Odczyt GATT połączonej klawiatury (AnnePro2, firmware QMK)

- Report Map (0x2A4B, 149 B) jest **bajt w bajt identyczna** z obrazem BLE pod offsetem 0x6000. Dekodowanie statyczne zostało potwierdzone sprzętowo [O].
- Device Information [O]: producent `HEXCORE`, model `KB181`, numer seryjny `0123456789ABCDEF0123456789ABCDEF` (to ciąg z offsetu 0x1DC obrazu), wersja firmware `BLE-1.5.0` (obraz 0x6164), wersja sprzętu `1.2.0`, wersja oprogramowania `2.2.0` (obraz 0x200), System ID same zera, certyfikat IEEE 11073 `fe 00 'experimental'` (stąd napis `experimental` w obrazie 0x6158), PnP ID `01 0d00 e0f0 1001` (źródło Bluetooth SIG, VID 0x000D = Texas Instruments, PID 0xF0E0, wersja 0x0110).
- Numer seryjny jest domyślnym ciągiem z obrazu, bo QMK nie wysyła `01 10`. Fabryczny MCU wysyła 16 bajtów identyfikatora i prawdopodobnie ustawia nim numer seryjny lub podobny atrybut [I].
- GAP [O]: nazwa `AnnePro2`, appearance 0x0000 (w danych rozgłaszania jest 0x03C1), PPCP: minimum 100 ms, maksimum 200 ms, latency 0, limit czasu 10 s. To inne wartości niż żądanie aktualizacji parametrów w kodzie (11,25 ms, latency 15, 4 s); co host faktycznie zobaczy, pokaże dopiero `btmon`.
- Battery Service [O]: Battery Level (0x2A19) ma właściwości **read i notify** (jest CCCD; host ustawił 0x0001), Report Reference `04 01` (ID 4, input), Presentation Format `04 00 ad 27 01 11 31`. Odczyt dał 0x43 (67 %), wcześniej host pokazał 73 %. Kod BLE aktualizuje poziom w GATT tylko wtedy, gdy nowy jest mniejszy od obecnego, i wysyła wtedy powiadomienie (trzecia runda analizy), co tłumaczy spadek z 73 do 67 %; po naładowaniu poziom w GATT nie rośnie do restartu układu BLE [O/I].
- HID [O]: HID Information `11 01 00 01` (bcdHID 1.11, flaga remote wake), Protocol Mode `01`, Boot Keyboard Input (0x2A22) i Output (0x2A32), pięć raportów 0x2A4D wg Report Reference: ID 1 input i output, ID 2 input i output (kanał producenta, 18 B), ID 3 input (multimedia), oraz jeden raport feature (ID 0). Scan Parameters (0x1813): 0x2A4F i 0x2A31 (notify).
- Odczyt raportu wyjściowego ID 2 zwrócił 16 bajtów wyglądających na losowe. Nie zapisuję ich tutaj. Analiza kodu wskazuje, że callback odczytu zwraca bajty z XDATA od `0x0CF7`, a zapisy hosta idą do innego bufora (`0x0C60`), więc to odsłonięcie sąsiedniej pamięci, nie funkcja [I]. Czy są tam dane wrażliwe, nie ustalono; nie udostępniaj wyniku takiego odczytu. Podobnie `40 13` może zwracać śmieci ze stosu.

## 2. ObinsKit 1.2.11 (kod JavaScript z `app.asar`)

- Numeracja węzłów jest taka sama jak w analizie: `USB_HOST=1`, `BLE_HOST=2`, `MCU_MAIN=3`, `MCU_LED=4`, `MCU_BLE=5` [O].
- Aktualizacja: ObinsKit **odczytuje adresy bazowe z bootloadera** poleceniem `readIapFwVersion` (jedno wywołanie bez adresata) i używa fragmentów odpowiedzi: bajty 2-5 to baza MCU, bajty 12-15 baza LED, bajty 22-25 baza BLE (little-endian). Potem `writeEraseMemory` na tym adresie i zapis kolejnymi fragmentami: 48 B dla MCU i LED, 32 B dla BLE [O]. Stały adres 0x4000 w `AnnePro2-Tools` dotyczy więc tylko MCU i LED, a baza BLE nie jest znana bez odczytu z bootloadera [I].
- Przebieg: wejście w IAP w kolejności BLE (`writeIapMode(5, 1)`), po 60 ms LED, po 60 ms MCU; kontrola trybu `readIapMode` (2 oznacza, że węzeł nadal jest w aplikacji, więc to błąd); zapis MCU, LED i BLE; na końcu `writeIapMode(MCU_MAIN, 2)` [O].
- Pliki firmware pochodzą z `https://releases.obins.net/<model>/firmware/<wersja>/<plik>` z kontrolą SHA-256 [O].
- Po BLE ObinsKit używa tych samych ramek w raportach o długości 19 B (ID raportu plus 18 B), po USB 64 B [O].
- Kody klawiszy: `KC_BLE_WL_1` do `KC_BLE_WL_4` to 200-203 (cztery sloty BLE), `KC_BLE_WL_ON` to 204, `KC_BL_BATT` to 246 (0xF6), `KC_FN_WHEEL` to 207 [O].
- `KC_BL_BATT` w fabrycznym MCU przełącza wskaźnik baterii na diodach: uruchamia pomiar ADC, a po nim MCU wysyła do LED `20 0f <procent>` (0 gasi wskaźnik) [O/I]. Do QMK nie dodano jeszcze odpowiednika.
- Natywny moduł `@hexcore/node-hid` (`HID_hidraw.node`, funkcja `Liana::writeWithName`) mapuje nazwy poleceń na (adresat, grupa, podpolecenie). Odczytane z kodu [O]:

| Nazwa w ObinsKit | Adresat | `cmd` `sub` | Uwaga |
|---|---|---|---|
| `readIapMode` / `writeIapMode` | MCU | `02 02` / `02 01` | tryb: 1 to IAP, 2 to aplikacja |
| `readIapFwVersion` | MCU | `02 03` | adresy bazowe MCU, LED i BLE (patrz wyżej) |
| `readPowerOnTime` | MCU | `01 02` | |
| `readMcuUUID` | MCU | `01 04` | |
| `readBattery` | MCU | `01 05` | surowy odczyt baterii |
| `readHardwareState` | MCU | `01 09` | |
| `resetPowerOnTime` | MCU | `01 0b` | |
| `writeBacklightAuto` | MCU | `01 0c` | |
| `readUDID` / `writeUDID` | MCU | `01 0d` / `01 0e` | identyfikator urządzenia (16 B w MCU, ten sam, który MCU odsyła do BLE jako `01 10`) |
| `readBatteryPercentage` | MCU | `01 11` | |
| `uploadKbdLayout`, `writeKbdMagicFn`, `writeKbdTap`, `writeKbdLayout`, `writeKbdCapslock`, `writeKbdTapkey*` | MCU | `10 02`, `10 03`, `10 05`, `10 06`, `10 07`, `10 11`-`10 13` | konfiguracja klawiatury |
| `writeLightList`, `writeLightHoldOn`, wskaźniki, kolor Caps Lock, tryb zasilania LED, jasność | MCU | `20 04`, `20 0d`, `20 14`, `20 15`, `20 18`-`20 1f` | konfiguracja podświetlenia (`20 15` to jasność podświetlenia) |
| `writeAudioVisualConfig`, `writeAudioVisualData` | LED | `21 01`, `21 02` | wizualizacja dźwięku |
| makra (`writeMacroFn`, `resetMacro`, `readMacro*`, `clearMacro`) | MCU | `30 03` do `30 0a` | |
| **`deleteBleHost`** | **BLE** | **`40 05`** | usunięcie parowań; potwierdza, że `40 05` kasuje wszystkie bondy |
| **`readBleMac`** | **BLE** | **`40 30`** | adres BD układu BLE (6 B) |
| mysz wirtualna (`writeVirtualMouse*`, `readVirtualMouseConfig`) | MCU | `60 01`-`60 03`, `60 05` | grupa `0x60` to mysz wirtualna; ramka `60 04` z MCU trafia do BLE, który zamienia ją na raport HID ID 4 (input, 8 B) |
| OLED (`writeOled*`) | MCU | `70 xx` | modele z wyświetlaczem |

- Nie znalazłem w ObinsKit polecenia, które wymusza wysłanie `01 05` do BLE. `readBattery` i `readBatteryPercentage` pytają MCU (`01 05`, `01 11`).

## 3. Niedokończone

- Podpolecenia, które w tabeli ObinsKit mają niejednoznaczny adresat lub podpolecenie (np. `readDeviceType`, `readFirmwareVersion`, `writeSleepTimeout`, `writeIapMemory`, `writeEraseMemory`): kod wywołuje osobne funkcje, nie prześledzone.
- Trzecia runda analizy obrazu BLE została ukończona; wyniki są w `04-uklad-ble-8051.md`, sekcja 8.
- Nagranie `btmon` (parametry połączenia, dystrybucja kluczy). Odczyt `readIapFwVersion` w trybie IAP został wykonany (sekcja 5).

## 4. Nagranie btmon (parametry połączenia, dystrybucja kluczy)

Cel: sprawdzić, jakie parametry połączenia host faktycznie dostaje (PPCP w GATT: 100-200 ms, latency 0, timeout 10 s; kod BLE żąda 11,25 ms, latency 15, 4 s) oraz jak wygląda parowanie (Just Works, rozdzielane klucze). `btmon` wymaga uprawnień roota, więc uruchom go Ty:

1. W jednym terminalu: `sudo btmon -w /tmp/annepro2.btsnoop`
2. W drugim: odłącz i podłącz klawiaturę po BLE (klawisz slotu, krótkie naciśnięcie). Do sprawdzenia parowania użyj slotu, którego jeszcze nie używasz na tym komputerze (najpierw `bluetoothctl remove <adres>` dla starego wpisu tego slotu na hoście, a na klawiaturze długie przytrzymanie slotu i parowanie od nowa).
3. Zatrzymaj `btmon` (Ctrl+C) i podaj mi ścieżkę pliku. Analizę zrobię offline.

Uwaga: parowanie na slocie zastępuje bond tego slotu. Parowanie od nowa nie kasuje pozostałych slotów, ale klawisz unpair (`40 05`) kasuje wszystkie bondy, więc go nie używaj.

## 5. Odczyt bootloadera na prawdziwym urządzeniu (`annepro2_tools --info`)

Polecenie `--info` pochodzi z lokalnej, niepublikowanej wersji `AnnePro2-Tools` (tylko odczyt: wysyła `02 02` i `02 03` do bootloadera). Wykonane po przełączeniu klawiatury w tryb IAP (VID 0x04d9, PID 0x8009, "USB-HID IAP"), tylko odczyt, przed wgraniem firmware [O].

- Tryb (`02 02`) dla MCU, LED i BLE: `0x01` we wszystkich trzech, czyli wszystkie trzy procesory były w trybie IAP.
- Odpowiedź na `02 03` (`IapGetFwVersion`), pierwsze 32 bajty ładunku (bajty po `cmd` i `sub`):

```
7b 10 | 00 40 00 00  00 fe 01 00  01 00 | 00 40 00 00  00 fe 01 00  00 00 | 00 40 00 00  00 fe 01 00  00 00
```

- Układ jest zgodny z założeniem z kodu ObinsKit (bajty 2-5, 12-15 i 22-25): trzy rekordy po 10 bajtów, od przesunięcia 2: baza (u32 LE), rozmiar (u32 LE) i dwa bajty flag. Każdy rekord: baza `0x00004000`, rozmiar `0x0001fe00` (130 560 B). Flagi: `01 00` dla MCU, `00 00` dla LED i BLE. Dwa pierwsze bajty ładunku (`7b 10`) nie mają wyjaśnienia [N].
- Wniosek: bootloader zgłasza bazę **0x4000 także dla BLE**, więc domyślna baza `AnnePro2-Tools` (0x4000) zgadza się z odczytem dla wszystkich trzech procesorów. Zawartość obrazu BLE (`ap2_c18_0205_ble.bin`) kończy się na offsecie 0x1F480 (128 128 B do ostatniego niepustego bajtu), więc mieści się w zgłoszonym rozmiarze 0x1fe00. Samo to jednak nie wyjaśnia, jak adresy bootloadera BLE odpowiadają fizycznemu adresowi flash układu CC253x (z analizy obrazu wynikało 0x2000); flashowanie BLE nie było testowane [N].

