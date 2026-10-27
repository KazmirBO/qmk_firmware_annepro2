# Port QMK i narzędzia względem oryginału

Stan na 2026-10-27: fork `qmk_firmware_annepro2` (kod z commitu `6c03dfe813`, katalog `keyboards/annepro2/`) i lokalna gałąź `feature/ble-stock-parity` repozytorium `AnnePro2-Tools` (`b5c02bc`, niepublikowana). Opis zachowania oryginału jest w `03-mcu.md` i `05-ble.md`; tutaj jest tylko to, co port robi, czym różni się od oryginału i gdzie to jest w kodzie. Wszystkie twierdzenia o kodzie portu sprawdzono w kodzie i buildach z 2026-10-27; wyniki testów na klawiaturze są w `08-testy-na-sprzecie.md`.

## 1. Funkcje Bluetooth portu

| Funkcja | Oryginał | Port | Kod |
|---|---|---|---|
| Odbiór ramek od BLE | parser sprawdza bajty 0 i 1, ramki typu 0x12 i 0x10 (`02-protokol.md` §1) | parser po polu długości: typ 0x12 lub 0x10, `0x7D` w bajcie 7, długość 2-8; `20 07` i `20 0c` tylko w grupie 0x20; po odrzuconym nagłówku wznawia od następnego `0x7B`; niedokończoną ramkę porzuca po `AP2_BLE_RX_TIMEOUT_MS` (100 ms); bajtu adresowego (`0x35`) nie sprawdza | `annepro2.c`: `ble_rx_byte()`, `ble_handle_frame()` |
| Handshake | `02 01 02` i `01 03 02` co 0,5 s przez 5 prób, potem co 5 s, bez limitu | ta sama para przy starcie, potem 4 razy co 50 ms i dalej co 500 ms, najwyżej `AP2_BLE_HANDSHAKE_TRIES` (24) par, czyli około 9,7 s; odpowiedź `01 03 0x25` daje wersję 0x0205 | `ble_handshake_task()`; `annepro2_ble_is_ready()`, `annepro2_ble_version()` |
| Caps Lock hosta po BLE | `20 07` od BLE trafia do LED | `20 07` ustawia stan diody Caps Lock zwracany przez sterownik BLE | `ble_handle_frame()`, `ap2_ble_leds()` |
| Połączenie | `20 0c` od BLE gasi wskaźnik slotu | `20 0c` gasi miganie i zleca natychmiastową wysyłkę baterii | `ble_handle_frame()` |
| Klawisze slotów bez opcji | - | naciśnięcie wysyła `40 01 <slot>` (parowanie); drugie naciśnięcie tego samego slotu dodatkowo `40 04 <slot>` i przełącza raporty na BLE | `annepro2_ble_broadcast()` |
| Klawisze slotów z `AP2_BLE_SLOT_TAP_HOLD` | krótko `40 04`, przytrzymanie 5 s `40 01` | krótko `40 04`, przytrzymanie `AP2_BLE_SLOT_HOLD_MS` (domyślnie 500 ms; 5000 daje czas oryginału) `40 01`; w obu przypadkach raporty od razu idą do BLE, slot miga na niebiesko | `slot_event()`, `slot_tick()`, `annepro2_ble_pair()`, `annepro2_ble_connect()` |
| Przełącznik Bluetooth (`AP2_BT_SWITCH`) | PC10 z podciąganiem w dół jest jedynym źródłem wyboru wyjścia: stan wysoki USB, niski Bluetooth; odczyt bez odszumiania, wyjście ustawiane z poziomu pinu przy każdym odczycie | `AP2_BT_SWITCH_PIN` (domyślnie `C10`) z podciąganiem w dół; wyjście wybiera tylko zmiana stanu stabilna przez `AP2_BT_SWITCH_DEBOUNCE_MS` (50 ms), więc `KC_AP2_USB` i klawisze slotów działają pomiędzy zmianami; pierwszy odczyt jest stosowany w pierwszym skanie macierzy; On przełącza raporty na BLE bez ramki slotu, Off wraca do USB i gasi wskaźnik | `bt_switch_task()`, `annepro2_ble_use_driver()`, `annepro2_ble_disconnect()` |
| Bateria (`AP2_BLE_BATTERY`, tylko C18) | MCU mierzy baterię, ale nie wysyła jej do BLE, więc host widzi stałe 100 % (wartość początkową BLE) | ADC kanał 7 na PA7 skonfigurowany jak w oryginale (`HCLK/64`, wyzwalanie programowe, 12 bitów, średnia z 8 próbek bez skrajnych); ramka `01 05 <lo> <hi>` po handshake, po `20 0c` i co `AP2_BATTERY_INTERVAL_MS` (60 s); wysyłana jest największa z ostatnich `AP2_BATTERY_HISTORY` (5) wartości, bo BLE tylko obniża poziom | `annepro2_battery.c` |
| Numer seryjny BLE (`AP2_BLE_SERIAL`, tylko C18) | po każdym `01 03` od BLE wysyła `01 10` z 32 znakami hex UDID, ale bez bajtu `id`, więc BLE ramkę odrzuca i numer seryjny DIS zostaje domyślny | po każdym `01 03` od BLE wysyła `01 10 02` i 32 znaki hex (wielkie litery) UDID z rekordu oryginału we flash `0x3E08`; przy pustym rekordzie (same 0x00 albo 0xFF) nic nie wysyła; BLE trzyma wartość tylko w RAM | `annepro2_ble_send_serial()`, `AP2_DEVICE_RECORD_ADDR` w `c18/config.h` |
| Klawisze jasności po BLE | bity 6 i 7 raportu multimediów (usages 0x6F i 0x70) | `BRIGHTNESS_UP` 0x40, `BRIGHTNESS_DOWN` 0x80 | `CONSUMER2AP2()` w `annepro2_ble.c` |
| Powrót do USB | - | `KC_AP2_USB` przywraca poprzedni sterownik hosta (bez ramki do BLE) | `annepro2_ble_disconnect()` |
| Usunięcie parowań | ObinsKit `deleteBleHost` (`40 05`) | `KC_AP2_BT_UNPAIR` wysyła `40 05`, które usuwa bondy wszystkich czterech slotów | `annepro2_ble_unpair()` |
| Wejście w IAP | sygnatura `0xfab2` pod `0x20001ffc` i reset | LED w IAP, do BLE ramka `7b 10 51 10 03 00 00 7d 02 01 01`, sygnatura `0xfab2` pod `0x20001ffc`, reset | `bootloader_jump()` |

Ramki do BLE mają nagłówek `7b 12 53 00 <len> 00 00 7d`; `40 01` i `40 04` niosą jeden bajt danych (numer slotu 0-3), zgodnie z polem długości. Ramki grupy `0x02` (budząca `02 01 02` i `02 01 01` przed wejściem w IAP) mają, jak w oryginale, typ `0x10` i bajt 3 równy `0x10`: `7b 10 53 10 03 00 00 7d 02 01 02`. Aplikacja BLE ignoruje ramkę budzącą; ma ona znaczenie tylko dla bootloadera BLE [I].

### 1.1. Korekta poziomu 0 % baterii

Układ BLE pokazuje 0 % poniżej 3,4 V (`05-ble.md` §9), a ogniwo działa dłużej. Port przelicza odczyt w domenie napięcia widzianego przez BLE (`U = raw * 2 * 3,1 / 4095`): zakres od `AP2_BATTERY_EMPTY_MV` (domyślnie 3200 mV) do 3,6 V jest rozciągany liniowo na 3,4-3,6 V, a poniżej progu wynik wypada tuż pod 3,4 V. Powyżej 3,6 V odczyt przechodzi bez zmian, a wartość 3400 lub więcej wyłącza korektę. Skalę odczytu można poprawić przez `AP2_BATTERY_SCALE_NUM/DEN` (domyślnie 1:1).

Skutek u hosta przy wartościach domyślnych (symulacja kodu portu z krzywą BLE, która interpoluje liniowo): 0 % poniżej 3,2 V, 10 % od 3,2 V, dalej liniowo do 20 % przy 3,4 V i 30 % przy 3,6 V, od 3,6 V tak jak bez korekty. Bez korekty host pokazuje 0 % aż do 3,4 V. Te same wartości trafiają do hosta i do wskaźnika `KC_AP2_BATT`, który liczy procent wzorem napięcia i krzywą układu BLE, z interpolacją; dla 4095 z 4096 możliwych odczytów daje dokładnie ten procent co BLE, a dla jednego (3,72 V) o 1 % więcej, bo BLE liczy na liczbach zmiennoprzecinkowych (test kodu skompilowanego gcc względem `analiza/py/bateria_korekta.py`). Do wersji `6b71633911` wskaźnik używał schodków fabrycznego MCU i w 278 odczytach zapalał o jeden klawisz mniej, niż wynikało z procentu u hosta. Zgodności z napięciem zmierzonym multimetrem nie sprawdzono.

### 1.2. Wskaźnik baterii `KC_AP2_BATT`

Odpowiednik `KC_BL_BATT` z oryginału. Mierzy baterię i pokazuje poziom (liczony jak u hosta, §1.1) na rzędzie liczb: klawisze 1-0, po jednym na 10 % (zapala się `(procent + 5) / 10` klawiszy), zielony od 50 %, żółty od 20 %, czerwony poniżej, pozostałe ciemnoszare. Znika po `AP2_BATTERY_SHOW_MS` (3 s) albo po ponownym naciśnięciu. Korzysta z klawiszy `sticky`, więc działa także przy wyłączonym podświetleniu. Bez `AP2_BLE_BATTERY` kod jest pusty. Po 3 nieudanych pomiarach kolejne są pomijane przez 10 minut, żeby martwy ADC nie spowalniał skanowania macierzy. `AP2_BLE_BATTERY` na C15 kończy budowę błędem `#error` (ADC znany tylko dla C18).

## 2. Czego port nie robi

| Funkcja oryginału | Powód |
|---|---|
| `11 01` | oryginał wysyła ją dopiero po upływie limitu snu (wybudzenie przez RTC); w BLE odsuwa o 500 ms zwolnienie blokady snu; port nie ma odpowiednika tego snu |
| `50 07` (tryb kierownicy poza USB) | BLE 2.05 ignoruje tę ramkę; port nie obsługuje akcelerometru |
| `60 04` (mysz wirtualna) | BLE 2.05 w praktyce ją ignoruje (kod konwersji na raport ID 4 jest nieosiągalny); port nie ma myszy wirtualnej |
| `40 02` (zerwanie połączenia) | oryginał nie wysyła jej z klawiszy |
| Kanał producenta (raport HID ID 2) | służy konfiguracji z ObinsKit po BLE; QMK ma własne mechanizmy |
| Wgrywanie obrazu BLE | bootloader zgłasza dla BLE bazę 0x4000, ale nie wiadomo, jak odpowiada ona adresowi fizycznemu flash CC254x (`06-obinskit-i-bootloader.md` §6); nie testowano |

## 3. Keymapa `Kazmir_BO`

- Włączone opcje: `AP2_BLE_SLOT_TAP_HOLD` (z domyślnym czasem 500 ms) oraz, tylko na C18, `AP2_BT_SWITCH` (gdy płytka definiuje `AP2_HAS_BT_SWITCH`), `AP2_BLE_BATTERY` (`AP2_HAS_BATTERY_ADC`) i `AP2_BLE_SERIAL` (`AP2_DEVICE_RECORD_ADDR`). Na C15 przełącznika nie ma (48-pinowy MCU nie ma PC10), a z włączoną opcją keymapa przekraczała tam pamięć RAM o 12 B.
- Warstwa `FN2`: sloty BLE na `1`-`4`, `KC_AP2_USB` na `6`, `KC_AP2_BATT` na `8`. `KC_AP2_BT_UNPAIR` jest celowo nieprzypisany, bo kasuje bondy wszystkich slotów.
- Warstwa `FN1`: jasność ekranu na `M` (w dół) i `,` (w górę).
- Caps Lock przytrzymany działa jako `FN1` po 250 ms (`CAPS_FN1_TAPPING_TERM`); pozostałe klawisze mają `TAPPING_TERM` 100 ms.

## 4. AnnePro2-Tools (lokalna gałąź `feature/ble-stock-parity`)

- Kontrola obrazu przed zapisem (`src/image.rs`): dla `main` i `led` początkowy SP musi wskazywać SRAM (0x20000000 do 0x20004000), wektor reset musi mieć bit Thumb i wskazywać wnętrze obrazu przy podanej `--base` (to wyłapuje też złą bazę); dla `ble` obraz musi zaczynać się od `LJMP` (0x02) i mieć najwyżej 0x26000 B. `--force` pomija kontrolę.
- `--info`: tylko odczyt z bootloadera. Wysyła `02 02` (tryb) do MCU, LED i BLE oraz `02 03` (bazy i rozmiary) do MCU, wypisuje surową odpowiedź i bazy. Plik firmware jest wtedy niepotrzebny.
- Zapis: fragmenty po 48 B dla MCU i LED, po 32 B dla BLE; domyślny cel `main`, domyślna baza 0x4000.
- `cargo test --release`: 11 testów (7 kontroli obrazu, 4 parsera odpowiedzi i ramek).
- Commity: `60175d8` (kontrola obrazu), `006d480` (`--info`), `b5c02bc` (wspólny `build_frame()`, czytelne błędy `--info`).

## 5. Buildy

Buildy z `6c03dfe813` z 2026-10-27, bez ostrzeżeń kodu:

| Cel | Rozmiar | SHA-256 |
|---|---|---|
| `c18:Kazmir_BO` | 31 652 B | `305978fabbef0ed42807dc278b1a1919c50e6f899f0f33bf15e101dee46db21c` |
| `c15:Kazmir_BO` | 30 392 B | `4ab60c47a8b6863fbfcaffbb219262a557cd1a20de84325f46180a7af8696137` |
| `c18:default` | 37 612 B | `1cbf621c98e703339ca7d765d91c19d6bfa484833ace5692541998a2fd7df3cb` |
| `c15:default` | 37 612 B | `6c6e983a5db11bee234738b45d1bde4591e9b3c84d7fb669fd113d796a563746` |
| `c18:default` z `-DAP2_BT_SWITCH` | 37 804 B | `30e7f9764b68f76609fbfc9e2d42c2a216f36e90ec0bd9a4d7fbe29a49ad452b` |
| `c18:default` z `-DAP2_BLE_SERIAL` | 37 740 B | `14792de6d772b28bb5e5f0145550d3968fc02fadd1e41464f9f64c19c21e799c` |

`c15:default` z `-DAP2_BLE_SERIAL` kończy się zamierzonym błędem `#error` (rekord urządzenia znany tylko dla C18).

Build `c18:Kazmir_BO` jest powtarzalny: kolejne buildy z tego samego commitu dają bajt w bajt ten sam plik (sprawdzone m.in. dla obrazu wgranego 2026-10-26, `08-testy-na-sprzecie.md`).

## 6. Historia zmian w forku

| Commit | Data | Zmiana |
|---|---|---|
| `8b1bc7b9be` | 2026-10-14 | parser ramek po długości, handshake z BLE, opcja tap/hold slotów |
| `354db088eb` | 2026-10-14 | bateria do BLE (`AP2_BLE_BATTERY`, C18) |
| `537df32397` | 2026-10-15 | `KC_AP2_BATT` |
| `4aaa862386` | 2026-10-15 | keymapa `Kazmir_BO` |
| `5bf72bff35` | 2026-10-15 | logika baterii w osobnym module, nazwane offsety i bity, `#error` na C15, opis opcji w `readme.md` |
| `099f766757` | 2026-10-15 | klawisze jasności na `FN1` w `Kazmir_BO` |
| `18355aa8ba` | 2026-10-15 | korekta 0 % baterii (`AP2_BATTERY_EMPTY_MV`) |
| `49467b01d5` | 2026-10-15 | ta dokumentacja w `keyboards/annepro2/docs/` |
| `188ee6b527` | 2026-10-16 | opcje keymapy `Kazmir_BO` działające naprawdę, `KC_AP2_BT_UNPAIR` odpięty |
| `4c87841d68` | 2026-10-16 | parser wznawia od następnego `0x7B` i ma limit czasu; przytrzymanie slotu przełącza na BLE; `40 01`/`40 04` z jednym bajtem danych |
| `237b276a55` | 2026-10-16 | wysyłka największej z ostatnich wartości baterii |
| `fdebec4dbd` | 2026-10-23 | 250 ms dla Caps Lock/`FN1` w `Kazmir_BO` |
| `313ea7353f` | 2026-10-26 | `AP2_BT_SWITCH`, poprawione opisy czasów oryginału |
| `2e37abad5e` | 2026-10-26 | dokumentacja po rundzie Ghidry |
| `6b71633911` | 2026-10-26 | `AP2_BT_SWITCH` włączone w `Kazmir_BO` |
| `f698d45e48` | 2026-10-27 | dokumentacja przebudowana po pełnej weryfikacji |
| `6c03dfe813` | 2026-10-27 | wskaźnik baterii z krzywą BLE, `AP2_BLE_SERIAL`, ramka budząca jak w oryginale, `AP2_BT_SWITCH` w `Kazmir_BO` tylko na C18, komentarze zgodne z weryfikacją |
