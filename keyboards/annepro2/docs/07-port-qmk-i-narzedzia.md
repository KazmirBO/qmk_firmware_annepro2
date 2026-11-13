# Port QMK i narzędzia względem oryginału

Stan na 2026-10-27: fork `qmk_firmware_annepro2` (kod z commitu `6c03dfe813`, katalog `keyboards/annepro2/`; od `f3317b7ece` scalony z `qmk/qmk_firmware` `master` `180f394b48` z 2026-10-16) i lokalna gałąź `feature/ble-stock-parity` repozytorium `AnnePro2-Tools` (`b5c02bc`, niepublikowana). Opis zachowania oryginału jest w `03-mcu.md` i `05-ble.md`; tutaj jest tylko to, co port robi, czym różni się od oryginału i gdzie to jest w kodzie. Wszystkie twierdzenia o kodzie portu sprawdzono w kodzie i buildach z 2026-10-27; wyniki testów na klawiaturze są w `08-testy-na-sprzecie.md`. Uzupełnienie z 2026-11-13: §1, §3, §5 i §6 opisują też commit `3fad67e4b0` (na `6f3c9160a4`) z poprawkami z przeglądu kodu; obraz `c18:kazmir_bo` wgrano 2026-11-13 (§5). Commit `4a0086b5ae` przenosi ponadto upstreamowy PR #26573 (timer dla `wait_us()`, §6); jego obraz `c18:kazmir_bo` wgrano 2026-11-13 (§5).

## 1. Funkcje Bluetooth portu

| Funkcja | Oryginał | Port | Kod |
|---|---|---|---|
| Odbiór ramek od BLE | parser sprawdza bajty 0 i 1, ramki typu 0x12 i 0x10 (`02-protokol.md` §1) | parser po polu długości: typ 0x12 lub 0x10, `0x7D` w bajcie 7, długość 2-8; `20 07` i `20 0c` tylko w grupie 0x20; po odrzuconym nagłówku wznawia od następnego `0x7B`; niedokończoną ramkę porzuca po `AP2_BLE_RX_TIMEOUT_MS` (100 ms); bajtu adresowego (`0x35`) nie sprawdza | `annepro2.c`: `ble_rx_byte()`, `ble_handle_frame()` |
| Handshake | `02 01 02` i `01 03 02` co 0,5 s przez 5 prób, potem co 5 s, bez limitu | ta sama para przy starcie, potem 4 razy co 50 ms i dalej co 500 ms, `AP2_BLE_HANDSHAKE_TRIES` (24) par, potem co `AP2_BLE_HANDSHAKE_SLOW_MS` (5 s) bez limitu, jak w oryginale, aż układ BLE odpowie; odpowiedź `01 03 0x25` daje wersję 0x0205 | `ble_handshake_task()`; `annepro2_ble_is_ready()`, `annepro2_ble_version()` |
| Caps Lock hosta po BLE | `20 07` od BLE trafia do LED | `20 07` ustawia stan diody Caps Lock zwracany przez sterownik BLE | `ble_handle_frame()`, `ap2_ble_leds()` |
| Połączenie | `20 0c` od BLE gasi wskaźnik slotu | `20 0c` gasi miganie i zleca natychmiastową wysyłkę baterii | `ble_handle_frame()` |
| Klawisze slotów bez opcji | - | naciśnięcie wysyła `40 01 <slot>` (parowanie); drugie naciśnięcie tego samego slotu dodatkowo `40 04 <slot>` i przełącza raporty na BLE | `annepro2_ble_broadcast()` |
| Klawisze slotów z `AP2_BLE_SLOT_TAP_HOLD` | krótko `40 04`, przytrzymanie 5 s `40 01` | krótko `40 04`, przytrzymanie `AP2_BLE_SLOT_HOLD_MS` (domyślnie 500 ms; 5000 daje czas oryginału) `40 01`. Tap od razu przełącza raporty na BLE. Przytrzymanie zostawia je tam, gdzie były (host USB działa dalej), a po połączeniu nowego hosta (`20 0c`) przenosi je na BLE tylko wtedy, gdy żaden host USB nie jest w użyciu. Slot miga na niebiesko, a miganie jest anulowane na tym samym klawiszu (do 2026-11-13 anulowanie wskazywało klawisz 0 i gasiło kolor warstwy na Esc, bo układ LED po zakończeniu migania czyści maskę wskazanego klawisza) | `slot_event()`, `slot_tick()`, `annepro2_ble_pair()`, `annepro2_ble_connect()` |
| Przełącznik Bluetooth (`AP2_BT_SWITCH`) | PC10 z podciąganiem w dół jest jedynym źródłem wyboru wyjścia: stan wysoki USB, niski Bluetooth; odczyt bez odszumiania, wyjście ustawiane z poziomu pinu przy każdym odczycie | `AP2_BT_SWITCH_PIN` (domyślnie `C10`) z podciąganiem w dół; wyjście wybiera tylko zmiana stanu stabilna przez `AP2_BT_SWITCH_DEBOUNCE_MS` (50 ms), więc `KC_AP2_USB` i klawisze slotów działają pomiędzy zmianami; pierwszy odczyt jest stosowany w pierwszym skanie macierzy; On przełącza raporty na BLE bez ramki slotu, Off wraca do USB i gasi wskaźnik | `bt_switch_task()`, `annepro2_ble_use_driver()`, `annepro2_ble_disconnect()` |
| Bateria (`AP2_BLE_BATTERY`, tylko C18) | MCU mierzy baterię, ale nie wysyła jej do BLE, więc host widzi stałe 100 % (wartość początkową BLE) | ADC kanał 7 na PA7 skonfigurowany jak w oryginale (`HCLK/64`, wyzwalanie programowe, 12 bitów, średnia z 8 próbek bez skrajnych); ramka `01 05 <lo> <hi>` po handshake, po `20 0c` i co `AP2_BATTERY_INTERVAL_MS` (60 s); wysyłana jest największa z ostatnich `AP2_BATTERY_HISTORY` (5) wartości, bo BLE tylko obniża poziom | `annepro2_battery.c` |
| Numer seryjny BLE (`AP2_BLE_SERIAL`, tylko C18) | po każdym `01 03` od BLE wysyła `01 10` z 32 znakami hex UDID, ale bez bajtu `id`, więc BLE ramkę odrzuca i numer seryjny DIS zostaje domyślny | po każdym `01 03` od BLE wysyła `01 10 02` i 32 znaki hex (wielkie litery) UDID z rekordu oryginału we flash `0x3E08`; przy pustym rekordzie (same 0x00 albo 0xFF) nic nie wysyła; BLE trzyma wartość tylko w RAM | `annepro2_ble_send_serial()`, `AP2_DEVICE_RECORD_ADDR` w `c18/config.h` |
| Klawisze jasności po BLE | bity 6 i 7 raportu multimediów (usages 0x6F i 0x70) | `BRIGHTNESS_UP` 0x40, `BRIGHTNESS_DOWN` 0x80 | `CONSUMER2AP2()` w `annepro2_ble.c` |
| Powrót do USB | - | `KC_AP2_USB` przywraca poprzedni sterownik hosta (bez ramki do BLE) | `annepro2_ble_disconnect()` |
| Usunięcie parowań | ObinsKit `deleteBleHost` (`40 05`) | `KC_AP2_BT_UNPAIR` po przytrzymaniu `AP2_BLE_UNPAIR_HOLD_MS` (3 s; 0 = od razu) wraca na sterownik USB, a potem wysyła `40 05` i `40 02`. Samo `40 05` przy połączonym hoście tylko zapisuje żądanie, a bondy wszystkich czterech slotów znikają dopiero po zakończeniu łącza (`05-ble.md` §8), więc `40 02` kończy je od razu; bez połączenia `40 02` wyłącza rozgłaszanie. Powrót na USB idzie pierwszy, bo wysyłany przy nim pusty raport sam uruchamia rozgłaszanie w układzie BLE (`05-ble.md` §6) | `unpair_now()`, `annepro2_ble_unpair()` |
| Limity czasu połączenia | brak: układ BLE nie zgłasza końca rozgłaszania (5 s przy łączeniu, 60 s przy parowaniu) ani rozłączenia (`05-ble.md` §5 i `05-ble.md` §7) | tap slotu, który przełączył raporty z hosta USB na BLE, wraca do USB, jeśli po `AP2_BLE_CONNECT_TIMEOUT_MS` (6 s; 0 = wyłączone) nie przyjdzie `20 0c`; flaga parowania wygasa po `AP2_BLE_PAIR_TIMEOUT_MS` (60 s) [I]. Ponowiony tap przed połączeniem hosta zachowuje powrót na USB i liczy czas od nowa, a przytrzymanie slotu (parowanie) w trakcie takiej próby od razu oddaje raporty hostowi USB (do 2026-11-13 oba przypadki zostawiały raporty na BLE bez limitu) | `annepro2_ble_connect()`, `annepro2_ble_pair()`, `annepro2_ble_task()` |
| Wejście w IAP | sygnatura `0xfab2` pod `0x20001ffc` i reset | LED w IAP, do BLE ramka `7b 10 51 10 03 00 00 7d 02 01 01`, sygnatura `0xfab2` pod `0x20001ffc`, reset | `bootloader_jump()` |

Ramki do BLE mają nagłówek `7b 12 53 00 <len> 00 00 7d`; `40 01` i `40 04` niosą jeden bajt danych (numer slotu 0-3), zgodnie z polem długości. Ramki grupy `0x02` (budząca `02 01 02` i `02 01 01` przed wejściem w IAP) mają, jak w oryginale, typ `0x10` i bajt 3 równy `0x10`: `7b 10 53 10 03 00 00 7d 02 01 02`. Aplikacja BLE ignoruje ramkę budzącą; ma ona znaczenie tylko dla bootloadera BLE [I].

### 1.1. Korekta poziomu 0 % baterii

Układ BLE pokazuje 0 % poniżej 3,4 V (`05-ble.md` §9), a ogniwo działa dłużej. Port przelicza odczyt w domenie napięcia widzianego przez BLE (`U = raw * 2 * 3,1 / 4095`): zakres od `AP2_BATTERY_EMPTY_MV` (domyślnie 3200 mV) do 3,6 V jest rozciągany liniowo na 3,4-3,6 V, a poniżej progu wynik wypada tuż pod 3,4 V. Powyżej 3,6 V odczyt przechodzi bez zmian, a wartość 3400 lub więcej wyłącza korektę. Skalę odczytu można poprawić przez `AP2_BATTERY_SCALE_NUM/DEN` (domyślnie 1:1).

Skutek u hosta przy wartościach domyślnych (`analiza/py/bateria_korekta.py`, z interpolacją liniową, której używa BLE): 0 % poniżej 3,2 V, 10 % od 3,2 V, dalej liniowo do 20 % przy 3,4 V i 30 % przy 3,6 V, od 3,6 V tak jak bez korekty. Bez korekty host pokazuje 0 % aż do 3,4 V. Te same wartości trafiają do hosta i do wskaźnika `KC_AP2_BATT`, który liczy procent wzorem napięcia i krzywą układu BLE, z interpolacją; dla 4095 z 4096 możliwych odczytów daje dokładnie ten procent co BLE, a dla jednego (3,72 V) o 1 % więcej, bo BLE liczy na liczbach zmiennoprzecinkowych (test kodu skompilowanego gcc względem `analiza/py/bateria_korekta.py`). Do wersji `6b71633911` wskaźnik używał schodków fabrycznego MCU i w 278 odczytach zapalał o jeden klawisz mniej, niż wynikało z procentu u hosta. Zgodności z napięciem zmierzonym multimetrem nie sprawdzono.

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

## 3. Keymapa `kazmir_bo`

- Włączone opcje: `AP2_BLE_SLOT_TAP_HOLD` (z domyślnym czasem 500 ms) oraz, tylko na C18, `AP2_BT_SWITCH` (gdy płytka definiuje `AP2_HAS_BT_SWITCH`), `AP2_BLE_BATTERY` (`AP2_HAS_BATTERY_ADC`) i `AP2_BLE_SERIAL` (`AP2_DEVICE_RECORD_ADDR`). Na C15 przełącznika nie ma (48-pinowy MCU nie ma PC10), a z włączoną opcją keymapa przekraczała tam pamięć RAM o 12 B.
- Warstwa `FN2`: sloty BLE na `1`-`4`, `KC_AP2_USB` na `6`, `KC_AP2_BATT` na `8`. `KC_AP2_BT_UNPAIR` na `5`; działa dopiero po przytrzymaniu 3 s, bo kasuje bondy wszystkich slotów (ponowne parowanie slotu zastępuje tylko jego bond).
- Warstwa `FN1`: jasność ekranu na `M` (w dół) i `,` (w górę).
- Caps Lock przytrzymany działa jako `FN1` po 250 ms (`CAPS_FN1_TAPPING_TERM`); pozostałe klawisze mają `TAPPING_TERM` 100 ms.
- Debounce: `sym_defer_pk`, 20 ms na obu zboczach (`a7eb48fa3b`, `7c6dd49603`), z powodu drgań przełącznika W. Ustawiono go, gdy macierz była skanowana około 200 razy na sekundę (§6, PR #26573); czy po przyspieszeniu skanu wystarczy krótszy czas, nie sprawdzano.
- `MATRIX_IO_DELAY` ma domyślne 30 µs z QMK. Wcześniejsze `1` nic nie zmieniało, dopóki `wait_us()` usypiał wątek na cały tick 1 ms; z timerem dawałoby kolumnom 1 µs na powrót do stanu wysokiego.
- Na C15 keymapa nie włącza efektu Pixel Flow (`KEYBOARD_annepro2_c15` w `config.h`): po `6f3c9160a4` brakowało tam 36 B RAM, a efekt trzyma 183 B stanu. C18 ma komplet efektów.

## 4. AnnePro2-Tools (lokalna gałąź `feature/ble-stock-parity`)

- Kontrola obrazu przed zapisem (`src/image.rs`): dla `main` i `led` początkowy SP musi wskazywać SRAM (0x20000000 do 0x20004000), wektor reset musi mieć bit Thumb i wskazywać wnętrze obrazu przy podanej `--base` (to wyłapuje też złą bazę); dla `ble` obraz musi zaczynać się od `LJMP` (0x02) i mieć najwyżej 0x26000 B. `--force` pomija kontrolę.
- `--info`: tylko odczyt z bootloadera. Wysyła `02 02` (tryb) do MCU, LED i BLE oraz `02 03` (bazy i rozmiary) do MCU, wypisuje surową odpowiedź i bazy. Plik firmware jest wtedy niepotrzebny.
- Zapis: fragmenty po 48 B dla MCU i LED, po 32 B dla BLE; domyślny cel `main`, domyślna baza 0x4000.
- `cargo test --release`: 11 testów (7 kontroli obrazu, 4 parsera odpowiedzi i ramek).
- Commity: `60175d8` (kontrola obrazu), `006d480` (`--info`), `b5c02bc` (wspólny `build_frame()`, czytelne błędy `--info`).

## 5. Buildy

Buildy z `4a0086b5ae` z 2026-11-13 (przeniesiony PR #26573, §6; drzewo zawiera `upstream/master` `3329239097`; zbudowane z drzewa roboczego o tej samej treści źródeł, przed commitem), bez ostrzeżeń kodu, po `make clean`:

| Cel | Rozmiar | SHA-256 |
|---|---|---|
| `c18:kazmir_bo` | 33 216 B | `1f5ce8bb453721a4c86dca4714ef3e92d78ab3fbb244887625478eaccc27c752` |
| `c15:kazmir_bo` | 31 248 B | `4a2a037290ecbb29f28a07460522f0bd9c59bc337b9d4b2283436a4b88f752ce` |
| `c18:default` | 35 288 B | `cf4308727620654a9aa9ae4206c588d7eabbaddeee9806f5e2be28e78cfdc56f` |
| `c15:default` | 35 288 B | `88877d8aae139ef7548f459e23b63ea1e87f2ab461a421071dd52fbe478b52d5` |
| `c18:default` z `-DAP2_BT_SWITCH` | 35 436 B | `ac4c681328850cff6e4eaa6bb2415c6af41cff9eb2706152f6b9f4794e96c93d` |
| `c18:default` z `-DAP2_BLE_SERIAL` | 35 396 B | `2a1d17e6978e7c6ad70d1518ef0defe367dc06eb4d2a41ab3c024e2cc72beda8` |

Warianty z flagami budowano jako `make annepro2/c18:default EXTRAFLAGS=-DAP2_BT_SWITCH` (analogicznie `-DAP2_BLE_SERIAL`). `c15:default` z `-DAP2_BLE_SERIAL` kończy się zamierzonym błędem `#error` (rekord urządzenia znany tylko dla C18). Budują się też przemianowane keymapy `c18:default_full_caps`, `c18:default_layer_indicators` i `c18:iso_default`. Na C15 keymapie `default` zostaje 16 B wolnego RAM-u, a `kazmir_bo` 132 B. Obraz `c18:kazmir_bo` wgrano na klawiaturę 2026-11-13 (`annepro2_tools --boot`, 33 216 B zapisane, "Flash complete"), po czym zgłosiła się po USB jako `ac20:8009` „Anne Pro 2 C18 (QMK)”. Wgranie nie jest jeszcze testem (`08-testy-na-sprzecie.md` §2).

Poprzedni zestaw, z `3fad67e4b0` (zbudowany przed commitem z drzewa o tej samej treści źródeł, a `c18:kazmir_bo` ponownie po commicie, z tą samą sumą): `c18:kazmir_bo` 32 992 B `4b820d9082bd1ff0f4f453204f709935ec4672266487e690c804e89f88a099e7`, `c15:kazmir_bo` 31 048 B `a5eae9d2feb247eec60f92cbbb7b2aa838eede6efbd5dac57e14fd47ec709b4a`, `c18:default` 35 044 B `b85e77235ba470edbe7c9e89657a20a3b026f6da38a954d15b67ebd595f7c378`, `c15:default` 35 044 B `6ea9cc4b111ba00cc753053fd8e665dbf1fd650bd7986247d6194d7f7866a735`. Obraz `c18:kazmir_bo` z tego zestawu był wgrany na klawiaturę 2026-11-13 (32 992 B zapisane, "Flash complete"), do czasu wgrania obrazu z bieżącego zestawu tego samego dnia.

Scalenia upstreamu zmieniają obrazy także bez zmian w porcie. Sam `6f3c9160a4` (bez poprawek z 2026-11-13) daje `c18:kazmir_bo` 32 840 B, `960d8913bc9c577044ce34957d76205e2ba0a214df2602cd844c8dc55b2ae2b8`, a `c15:kazmir_bo` się na nim nie linkuje (36 B ponad RAM). Obrazy keymap `default` zmalały o około 4,4 kB, najpewniej dlatego, że upstream usunął z `info.json` efekty `typing_heatmap` i `digital_rain` [I].

Poprzedni zestaw, z `c0a2cfcccb` z 2026-11-06 (drzewo z `upstream/master` `f5db962110`): `c18:kazmir_bo` 32 840 B `a0f129390e91f270846867be0f4744a83ddcf09189171073b0014bf0333e1e17`, `c15:kazmir_bo` 31 252 B `f4e2a7d61cb7bcab1eea607033929c5201085a6ab0c51f2e2f014281a4f77a11`, `c18:default` 39 436 B `21a4baa7e2c8ee0ebdc197dc0f10af20e332982b2674ed4aedeb7aba2448132d`, `c15:default` 39 436 B `71b300b99ec9c0f5a42d56f702bfe950f41ca1d7bf66868da14b0d251b4181d1`. Obraz `c18:kazmir_bo` z tego zestawu wgrano na klawiaturę 2026-11-06 (`annepro2_tools --boot`, 32 840 B zapisane, "Flash complete").

## 6. Historia zmian w forku

| Commit | Data | Zmiana |
|---|---|---|
| `8b1bc7b9be` | 2026-10-14 | parser ramek po długości, handshake z BLE, opcja tap/hold slotów |
| `354db088eb` | 2026-10-14 | bateria do BLE (`AP2_BLE_BATTERY`, C18) |
| `537df32397` | 2026-10-15 | `KC_AP2_BATT` |
| `4aaa862386` | 2026-10-15 | keymapa `kazmir_bo` |
| `5bf72bff35` | 2026-10-15 | logika baterii w osobnym module, nazwane offsety i bity, `#error` na C15, opis opcji w `readme.md` |
| `099f766757` | 2026-10-15 | klawisze jasności na `FN1` w `kazmir_bo` |
| `18355aa8ba` | 2026-10-15 | korekta 0 % baterii (`AP2_BATTERY_EMPTY_MV`) |
| `49467b01d5` | 2026-10-15 | ta dokumentacja w `keyboards/annepro2/docs/` |
| `188ee6b527` | 2026-10-16 | opcje keymapy `kazmir_bo` działające naprawdę, `KC_AP2_BT_UNPAIR` odpięty |
| `4c87841d68` | 2026-10-16 | parser wznawia od następnego `0x7B` i ma limit czasu; przytrzymanie slotu przełącza na BLE; `40 01`/`40 04` z jednym bajtem danych |
| `237b276a55` | 2026-10-16 | wysyłka największej z ostatnich wartości baterii |
| `fdebec4dbd` | 2026-10-23 | 250 ms dla Caps Lock/`FN1` w `kazmir_bo` |
| `313ea7353f` | 2026-10-26 | `AP2_BT_SWITCH`, poprawione opisy czasów oryginału |
| `2e37abad5e` | 2026-10-26 | dokumentacja po rundzie Ghidry |
| `6b71633911` | 2026-10-26 | `AP2_BT_SWITCH` włączone w `kazmir_bo` |
| `f698d45e48` | 2026-10-27 | dokumentacja przebudowana po pełnej weryfikacji |
| `6c03dfe813` | 2026-10-27 | wskaźnik baterii z krzywą BLE, `AP2_BLE_SERIAL`, ramka budząca jak w oryginale, `AP2_BT_SWITCH` w `kazmir_bo` tylko na C18, komentarze zgodne z weryfikacją |
| `f3317b7ece` | 2026-10-27 | scalenie `qmk/qmk_firmware` `master` (`180f394b48`); jedyna zmiana upstream od bazy `7a1bbf37c5` to plik blokady generatora dokumentacji |
| `6a53fe3a45` | 2026-10-24 | utwardzenie łącza BLE, Bluetooth działa przy uśpionym hoście USB (gałąź PR #2, w `master` od 2026-11-01) |
| `01e763797a` | 2026-10-24 | filtr pierwszego poziomu baterii i krzywa układu BLE (PR #2) |
| `48d6b81715` | 2026-10-24 | pozostałe martwe opcje keymapy `kazmir_bo` (PR #2) |
| `56d39ba7b1` | 2026-10-24 | opis parowania, baterii i uśpienia USB w `readme.md` (PR #2) |
| `55bb2a154b` | 2026-11-01 | poprawki builda i przeglądu zmiany uśpienia USB (PR #2) |
| `f459a43171` | 2026-11-01 | UART układu LED nie blokuje skanu macierzy (PR #2) |
| `c972dc008a` | 2026-11-01 | poprawki po czwartym przeglądzie (PR #2, scalony jako `d4c4b842d4`) |
| `b3ebcc6e1e` | 2026-11-01 | poprawki po przeglądzie `master` (PR #3, `c1d9a582db`) |
| `f649e362a1` | 2026-11-02 | poprawki po przeglądzie C18 (PR #4, `134176b7df`) |
| `a7eb48fa3b` | 2026-11-02 | debounce `kazmir_bo` na obu zboczach, 25 ms |
| `81cf9f0ccf` | 2026-11-02 | scalenie `upstream/master` (`f5db962110`) |
| `7c6dd49603` | 2026-11-03 | debounce `kazmir_bo` 20 ms |
| `d0c9fab01d` | 2026-11-03 | dokumentacja: mapa fabryczna, matryca i wskaźniki LED, sen, CRC obrazu BLE |
| `c0a2cfcccb` | 2026-11-06 | limity czasu łączenia i parowania, unpair po przytrzymaniu, pingi układu BLE bez końca |
| `0b45b60286` | 2026-11-06 | dokumentacja limitów i unpair, zmiana nazwy keymapy z `Kazmir_BO` na `kazmir_bo` |
| `e83d5ef8a2` | 2026-11-08 | scalenie `upstream/master` (`d194c077ed`) |
| `97d3db7e65` | 2026-11-08 | rozmiary i sumy buildów `c0a2cfcccb` w §5 |
| `6f3c9160a4` | 2026-11-12 | scalenie `upstream/master` (`3329239097`); `ec8a1b6549` powiększa bufor USB EP0 z 2 do 64 B i usuwa z `info.json` efekty `typing_heatmap` i `digital_rain`; `c15:kazmir_bo` przestaje się linkować |
| `3fad67e4b0` | 2026-11-13 | unpair kończy też łącze (`40 02` po `40 05`) i najpierw wraca na USB; anulowanie migania na właściwym klawiszu; powrót na USB zachowany po ponowionym tapie i przy parowaniu w trakcie próby łączenia; wskaźnik baterii zostawia rezerwę kolejki UART LED; `kazmir_bo` na C15 bez efektu Pixel Flow |
| `4a0086b5ae` | 2026-11-13 | przeniesiony upstreamowy PR #26573 (`qmk/qmk_firmware`, autor Fede-26, otwarty, niescalony): `WAIT_US_TIMER GPTD_BFTM0` w `c15/config.h` i `c18/config.h`, `HAL_USE_GPT` w `halconf.h`, `HT32_GPT_USE_BFTM0` w `mcuconf.h`, poprawione nagłówki licencji keymap `default` i `iso_default`, keymapy `default-full-caps` i `default-layer-indicators` przemianowane na `default_full_caps` i `default_layer_indicators`. Na Cortex-M0 `wait_us()` bez timera to `chThdSleepMicroseconds()`, które przy takcie 1 kHz trwa cały tick, a macierz czeka raz na wiersz, więc skan 5 wierszy trwał około 5 ms [O z kodu]; PR podaje 200 Hz przed zmianą i 3300 Hz po niej [zewn., niemierzone tutaj]. Ponad PR: `kazmir_bo` bez `MATRIX_IO_DELAY 1` (§3). Zmiany nagłówka w `ap2_led.h` nie przenoszono, bo plik w forku ma go już poprawnego |

Wiersze od `6a53fe3a45` do `97d3db7e65` dopisano 2026-11-13 na podstawie tytułów commitów (`git log`), bez ponownego czytania ich treści.

Kolejna synchronizacja z upstream: `git fetch upstream` i `git merge upstream/master` (remote `upstream` wskazuje `https://github.com/qmk/qmk_firmware.git`).

Archiwalne patche z pierwszej wersji zmian (2026-10-14) i kopie plików sprzed nich są w `propozycje/`; ich treść jest dziś w commitach powyżej.
