# Weryfikacja wcześniejszych ustaleń i nowe wyniki (runda Ghidry, 2026-10-26)

Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma. Adresy MCU i LED przy bazie 0x4000. Adresy BLE w zapisie `b:AAAA` (b to bank, 0 to obszar wspólny), jak w `04-uklad-ble-8051.md`.

## 1. Metoda tej rundy

- **Ghidra:** wersja 12.1.4 w trybie headless, z własnymi skryptami: import, mapa pamięci, eksport pseudokodu i listingów.
- **MCU i LED** zostały zaimportowane jako `ARM:LE:32:Cortex` (Thumb) z pełną mapą pamięci:
  - flash od 0x4000 i obszar bootloadera 0x0000-0x3FFF;
  - SRAM z danymi RW rozpakowanymi algorytmem z `05-metoda-i-dowody.md`;
  - peryferia 0x40000000 i rdzeń 0xE0000000;
  - 398 nazw rejestrów HT32F523x2 wygenerowanych z nagłówków ChibiOS-Contrib.
  - Wynik: MCU 644 funkcje (15,6 tys. linii pseudokodu), LED 291 funkcji. Wszystkie dekompilacje się powiodły.
- **BLE** zostało zaimportowane jako `8051:BE:16:default`: obszar wspólny pod `CODE:2000`, banki 1-4 jako nakładki (ang. _overlay_) pod 0x8000. Zostało rozwiązanych 764 przeskoków przez trampolinę `0:25E6`; to stuby z celem w banku 1-4, a 37 dalszych stubów z bankiem 0 nie jest objętych. Pseudokod 8051 jest mało czytelny, więc weryfikacja BLE opiera się na listingu z rozwiązanymi celami wywołań.
- **ObinsKit 1.2.11:** `app.asar` został odczytany własnym czytnikiem (bez uruchamiania), a moduł `HID_hidraw.node` deasemblowany `objdump`.
- **Sieć:** sprawdziłem instrukcję Anne Pro 2 (ManualsLib) i strony TI. Nie pobierałem żadnych binariów z sieci.

## 2. Status wcześniejszych twierdzeń

| Twierdzenie (plik) | Status | Uwagi |
|---|---|---|
| Baza obrazów 0x4000, tablice scatter-load, rozpakowanie RW (01, 05) | potwierdzone | Format wpisu: `[offset handlera][źródło względne][długość<<1][cel]`. MCU 0x205 B -> 0x711 B, LED 0x100 B -> 6749 B. |
| Wersja MCU 2.36 nie wynika z obrazu (01) | **obalone** | `0x71ec` wpisuje do `0x20001187/88` wartość 0x24, 0x02, czyli 0x0224 = 2.36 [O]. |
| Format ramki, odbiornik sprawdza tylko bajty 0-1, fragmenty 54/8/256 B (02) | potwierdzone | Dodatkowo: `frame_reply` odpowiada tylko nadawcom 1, 2, 3 [O]. |
| Tablica 13 poleceń MCU (02) | potwierdzona | Role zaktualizowane w sekcji 3.2: `0x03` czyta pamięć SPI, a nie MCU; `0x30` to makra; `0x80` to pomiar baterii; `0x60` jest pusta. |
| `20 0c` od BLE: MCU tylko zapisuje bajt konfiguracji w `0x66e2` (02, 03) | **obalone** | `0x66e2` obsługuje `20 0d`. `20 0c` trafia do `0x66d6`: MCU wysyła do LED `20 0c 00`, czyli wyłącza wskaźnik slotu po połączeniu [O]. |
| Wskaźnik slotu sterują tylko klawisze, nie stan BLE (02) | **poprawione** | Połączenie (`20 0c` z BLE) gasi wskaźnik [O]. |
| Handshake: 5 prób co 50, potem co 500 (03) | potwierdzone, jednostka ustalona | Tick systemowy 10 ms, więc 0,5 s, potem 5 s [I mocne, sekcja 3.1]. |
| Przytrzymanie slotu ~500 ms (03) | **poprawione** | Licznik 500 zmniejszany o 10 co 10 ticków daje 5 s. Instrukcja Anne Pro 2: „FN2 + 1 (5 Seconds)” [O zewn.]. |
| PC10 to nieustalone wejście, PC10 nie istnieje w układzie 48-pinowym (03, 05) | **poprawione** | PC10 to jedyne źródło wyboru wyjścia; instrukcja opisuje przełącznik Bluetooth z tyłu klawiatury. Wniosek: PC10 = ten przełącznik [I mocne], potwierdzony testem na C18 (Off: USB, On: Bluetooth) [O]. Wątpliwość co do obudowy układu zostaje [N]. |
| PC0 to detekcja USB, niska pewność (03) | potwierdzone co do kodu | PC0: wejście bez podciągania, EXTI0, debounce 30 ticków, 1 = USB [O]. Konflikt z etykietą schematu zostaje [N]. |
| Sen: przyciemnianie 10 jednostek przed limitem w `0x836c` (03) | **poprawione** | `0x836c` uzbraja RTC limitem snu w sekundach; opis w sekcji 3.6. |
| PB6 to linia CS pamięci SPI (03) | **obalone dla C18** | CS pamięci SPI to PA3 (zgodne z QMK `c18/config.h`). PB6 to wyjście ustawiane na 1 przy starcie, rola [N]. |
| PB7 jako I2C to niezgodność ze schematem (03) | **wyjaśnione** | PB7/PB8 = I2C1 dla akcelerometru LIS3DH. Czy układ jest obsadzony na C18, nie wiadomo [N]; przy braku czujnika sonda `WHO_AM_I` wyłącza funkcję. |
| Bateria: wzór, progi, `20 0f` tylko do LED (03) | potwierdzone | ADC co 60 000 ticków (10 min). Procent idzie do LED tylko po `KC_BL_BATT`. |
| Zadania RTOS (03) | potwierdzone i doprecyzowane | To procesy Contiki OS (sekcja 3.1). |
| Tablica poleceń BLE i podpolecenia `0x40` (04) | potwierdzone | Wszystkie 15 gałęzi `1:FC24` pod tymi samymi adresami. |
| Poziom baterii w GATT tylko maleje (04) | potwierdzone | `3:D827`: `SUBB`/`JNC` przed zapisem do `0x165B` [O]. |
| Okres timera baterii BLE nieznany (04) | **ustalony** | 15 000 (stała 0x3A98 pod XDATA `0x82DA`, czytana przez okno flash), w stanie połączenia [I mocne: jednostka OSAL = ms]. |
| `0x0B5E` to reakcja na HID Control Point (04) | **poprawione** | `0x0B5E` to wskaźnik na strukturę callbacków `HidDev_Register` (XDATA `0x194D`, rejestrowaną w `1:EF15`). `evtCB` (pole +2) wołają Control Point (zdarzenie 0 suspend, 1 exit suspend) i Protocol Mode (2 boot, 3 report) [O]. Kod funkcji aplikacji nieustalony [N]. |
| Mapowanie poleceń ObinsKit (07) | potwierdzone z wyjątkami | Wyjątki w sekcji 6: grupa `0x60` i kilka podpoleceń nie istnieją w MCU 2.36. |

## 3. MCU 2.36: nowe ustalenia

### 3.1. System i czas

- **RTOS to Contiki OS** [O]: `process_alloc_event` (`0x6818`, numery od 0x8A), `process_start` (`0x6824`), `process_post` (`0x69dc`, kolejka 64 zdarzeń), `process_run` (`0x69b8`), `process_poll` (`0x6a6c`), etimery (`etimer_set` `0x8b58`, `etimer_stop` `0x8b78`). Zdarzenia systemowe: 0x81 INIT, 0x82 POLL, 0x83 EXIT, 0x88 TIMER. Procesy to protothready (numer linii w `*pt`).
- **Struktury procesów:** `etimer` 0x20000004, `I2c Device` 0x200002b4, `AT` 0x20000474, `Daemon` 0x20000500, `Matrix Scan` 0x20000540, `Macro` 0x20000564, `Misc` 0x2000057c, `Power Manager` 0x200005ac, `Tap` 0x200005f0, `Uart` 0x2000063c.
- **Zegar:** `SystemCoreClock` = 48 MHz. SysTick ze źródła STCLK, przeładowanie (48e6/8)/100 = 60000.
  - Przy STCLK = HCLK/8 tick wynosi 10 ms [I]. Dokumentacji Holtek nie znalazłem w sieci, ale potwierdza to zgodność z instrukcją (5 s przytrzymania slotu).
  - Licznik 360 000 ticków to 1 h, czyli godzinny licznik czasu pracy (`01 02`).
  - Licznik 60 000 ticków to 10 min, czyli pomiar baterii.
  - Do przeliczania użyj: wszystkie wartości etimerów w MCU razy 10 ms.

### 3.2. Polecenia MCU (pełne podpolecenia)

| `cmd` `sub` | Działanie [O] | Nazwa w ObinsKit |
|---|---|---|
| `01 01` | 2 B z `0x20001146` (typ urządzenia z rekordu urządzenia, domyślnie 1, 5) | - |
| `01 02` | u32 czas pracy w godzinach | `readPowerOnTime` |
| `01 03` | od hosta: 6 B, trzy wersje u16; od LED: zapis wersji, stan LED = 2, wysyła konfigurację LED; od BLE: zapis wersji, stan BLE = 2, odsyła `01 10` (32 znaki hex UDID) | - |
| `01 05` | start ADC, odpowiedź: surowa średnia ADC (4 B) | `readBattery` |
| `01 06` | u32 z `0x20001191` | - |
| `01 08` | 2 B: flaga pamięci SPI (`0x200011ae`) i obecność czujnika (`0x200011af`) | - |
| `01 09` | 24 B stanu: liczniki i bajty konfiguracji | `readHardwareState` |
| `01 0a` | od LED z danymi 1: ping IAP do LED | - |
| `01 0b` | zeruje czas pracy, kasuje sektory SPI 0xA000 i 0xB000 | `resetPowerOnTime` |
| `01 0d` / `01 0e` | odczyt / zapis UDID. Zapis trafia razem z rekordem urządzenia na stronę flash 0x3E00 (ostatnia strona obszaru bootloadera) i jest chroniony kontrolą po stronie MCU | `readUDID` / `writeUDID` |
| `01 0f` | u16 limit snu w sekundach, zakres 10-3600, zapis konfiguracji | (`writeSleepTimeout` [I]) |
| `02 01` | od LED: stan i wersja LED; od BLE: stan BLE; od hosta USB z danymi 1 i tylko przy podłączonym USB: sygnatura `0xfab2` w `0x20001ffc` i reset do bootloadera. Host po BLE nie wejdzie w IAP | `writeIapMode` |
| `02 02` | odpowiedź 2 (aplikacja) | `readIapMode` |
| `03 01` | odczyt pamięci **SPI**: adres u32 + `data[5]`·0x10000, długość `data[4]` | - |
| `10 01` | zapis mapy klawiszy warstwy 0-3 do SPI 0x1000 + 0x100·warstwa (pierwszy fragment kasuje sektor) | - |
| `10 02` | odczyt mapy klawiszy warstwy | `uploadKbdLayout` |
| `10 03` / `10 05` / `10 06` / `10 07` | `0x2000115e` / `0x2000116b` / `0x2000115c` / `0x2000115f` (to ostatnie także do LED jako `20 09`) | `writeKbdMagicFn` / `writeKbdTap` / `writeKbdLayout` / `writeKbdCapslock` |
| `10 11` / `10 12` / `10 13` | tryb tap-key (2/3), tryb modyfikatora (4/5), czas 5-200 | `writeKbdTapkeyActMode` / `...ModifykeyActMode` / `...Timeout` |
| `12 01` | strumień zdarzeń klawiszy wł./wył. | - |
| `20 xx` | sekcja 3.3 | sekcja 6 |
| `30 03` | makra wł./wył. (`0x2000115d`) | `writeMacroFn` |
| `30 05` | zapis danych makr do SPI 0x10000 + offset | (`writeMacro` [I]) |
| `30 06` / `30 07` / `30 08` / `30 09` | nagłówek, lista aktywnych (3 B na wpis), mapa (14 B na wpis), treść makra | `readMacroConfig` / `readMacroActiveList` / `readMacroMap` / `readMacro` |
| `30 0a` | kasowanie makr (do 16 sektorów) | `clearMacro` |
| `40 xx` | pusta | - |
| `50 01`-`50 06` | czujnik: ack, stop kierownicy, odczyt `WHO_AM_I`, inicjalizacja, konfiguracja wykrywania stuknięć | - |
| `60 xx` | **brak** (wpis zerowy) | `writeVirtualMouse*`, `readVirtualMouseConfig` |
| `80 01` / `80 02` | jak `01 05` / pusta funkcja z ack | - |

Podpolecenia, których MCU 2.36 nie ma, choć ObinsKit je zna: `01 04` (`readMcuUUID`), `01 0c` (`writeBacklightAuto`), `01 11` (`readBatteryPercentage`), `30 04` (`resetMacro`), cała grupa `0x60`. Ramka bez obsługi zostaje bez odpowiedzi.

### 3.3. Polecenia MCU do LED i z LED

| Ramka | Kierunek | Znaczenie [O] |
|---|---|---|
| `20 01` | do LED | przełącz podświetlenie; LED odpowiada 2 B (stan, tryb) |
| `20 03 <x> <tryb> [dane]` | do LED | wybór efektu (1 = kolor stały RGB, 2 = kolory per klawisz, 3-160 = efekty i dane); LED odsyła do MCU `20 03 ff <tryb>` |
| `20 05` | lokalnie w MCU | następny efekt (`KC_BL_COLOR`, `KC_BL_TOGGLE`) |
| `20 06 <F3/F4>` | do LED | jasność +/-; LED odsyła `20 06 <jasność>` |
| `20 07 <0/1>` | do LED | Caps Lock (także z BLE) |
| `20 08 <5 × u16>` | do LED | bitmapa wciśniętych klawiszy dla efektów reaktywnych, wysyłana tylko przy trybach 5, 7, 10, 11, 12, 15, 22, 25 |
| `20 09 <b>` | do LED | ustawienie z `writeKbdCapslock` |
| `20 0b <slot> <1/0>` | do LED | wskaźnik slotu BLE (parowanie / łączenie) |
| `20 0c 00` | do LED | wyłączenie wskaźnika slotu (po `20 0c` z BLE) |
| `20 0f <pct>` | do LED | wskaźnik baterii; drugie wysłanie gasi |
| `20 12 01` | do LED | wskaźnik trybu kierownicy |
| `20 15 <b>` | oba | jasność podświetlenia |
| `20 16 <x> <y> <z>` | do LED | dane akcelerometru (starsze bajty osi) |
| `20 1b <R G B>` / `20 1f <b>` | do LED | kolor wskaźnika / tryb zasilania LED |

### 3.4. Kody klawiszy w MCU (nazwy z ObinsKit)

| Zakres | ObinsKit | Działanie w MCU 2.36 [O] |
|---|---|---|
| 0x04-0xA4 | klawisze HID | raport klawiatury |
| 0xA5-0xA7 | `KC_SYSTEM_POWER/SLEEP/WAKE` | sterowanie systemem |
| 0xA8-0xB5 | multimedia, jasność ekranu, poczta, kalkulator | raport consumer (14 bitów) |
| 0xB6-0xB9 | `KC_MY_COMPUTER`, `KC_WWW_*` | w tej wersji kółko i przesuw myszy wirtualnej (rozbieżność z nazwami ObinsKit) |
| 0xBA-0xBD | `KC_WWW_*` | klawisze wyzwalające makra (tablica wiązań makr) |
| 0xC0-0xC6 | `KC_FN0`-`KC_FN6` | warstwy |
| 0xC8-0xCB | `KC_BLE_WL_1`-`4` | sloty BLE: krótko łącz, 5 s paruj |
| 0xCC | `KC_BLE_WL_ON` | brak obsługi |
| 0xCF | `KC_FN_WHEEL`, w lokalizacji „Game wheel” | tryb kierownicy (sekcja 3.5) |
| 0xD0, 0xD1 | `KC_FN16`, `KC_FN17` | gamepad: osie X i Y rosną od 0x32 do 0xFF podczas trzymania (pedały) |
| 0xD2-0xDF | `KC_FN18`-`KC_FN31` | gamepad: przyciski 1-14 |
| 0xF0, 0xF1 | `KC_BL_TOGGLE`, `KC_BL_COLOR` | `20 01` / `20 05` |
| 0xF2, 0xF5 | `KC_BL_LEVEL`, `KC_BL_RATE` | brak obsługi |
| 0xF3, 0xF4 | `KC_BL_INCREASE`, `KC_BL_DECREASE` | jasność |
| 0xF6 | `KC_BL_BATT` | wskaźnik baterii |
| 0xF8-0xFE | `KC_MS_BTN1-3`, `KC_MS_XY_*` | mysz wirtualna |
| 0xBF, 0xF7 | `KC_BOOTLOADER`, `KC_QUICKNUM` | brak obsługi |

### 3.5. Akcelerometr: „Game wheel” i efekt poziomicy

- **Czujnik:** LIS3DH na I2C1 (PB7/PB8), adres 0x19, `WHO_AM_I` = 0x33. Inicjalizacja: ODR 100 Hz, ±2 g, osie XYZ. Opcjonalnie konfiguracja wykrywania stuknięć (`0x38` = 0x2A, `0x3B` = 120, `0x3C` = 200), ale wyniku stuknięć nic nie odczytuje [O].
- **Tryb kierownicy (`KC_FN_WHEEL`)** [O, nazwa z ObinsKit]:
  - Włącza się tylko przy wyjściu USB i podłączonym USB.
  - Co 10 ms liczy kąty przechyłu (`atan2`, stopnie) i wysyła je jako oś Ry raportu gamepada USB (interfejs 3, EP 0x86).
  - Klawisze 0xD0-0xDF pełnią rolę pedałów i przycisków.
  - Poza USB proces wysyła do BLE `50 07`, które BLE 2.05 ignoruje, więc kierownica działa tylko po USB.
- **Efekt podświetlenia nr 26 (0x1a):**
  - Gdy LED zgłasza ten tryb (`20 03 ff 1a`), MCU co 100 ms wysyła `20 16 x y z`.
  - LED (`0x7db0`) zamienia oś Y na kolumnę 0-13 i ją podświetla, jak poziomica.
  - ObinsKit 1.2.11 nie ma tego efektu na liście [O].

### 3.6. Zasilanie, sen, piny

- **Odliczanie bezczynności:** `0x6cfe`, wołane z SysTick, gdy nie ma zdarzeń. Licznik rośnie tylko przy spełnieniu wszystkich warunków:
  - brak USB;
  - flaga `0x20001163` = 1, ustawiana `20 0d`, w ObinsKit `writeLightHoldOn`;
  - LED i BLE w stanie 2;
  - wyłączony wskaźnik baterii.
- **Sen:**
  - Po 0,9 s bezczynności MCU uzbraja RTC limitem snu w sekundach (`01 0f`, 10-3600).
  - Dopasowanie RTC wyzwala uśpienie (DeepSleep, `0x6e74`). Źródła wybudzenia to EXTI na wierszach macierzy, PC0, PC10 i PA6.
  - Po wybudzeniu MCU wysyła do BLE `11 01`.
- **Piny (konfiguracja `0xb1b8`):**
  - Macierz: wiersze PB5, PB4, PB3, PB2, PD1 (pull-down) oraz kolumny PC4, PC5, PD0, PB15, PC11, PA15, PC12, PC13, PA8, PA10, PA11, PA14, PD2, PD3. Zgodne z QMK.
  - PA6 to wejście z podciąganiem w górę, służy jako źródło wybudzenia [N rola].
  - PC3 jest ustawiany na 1 przed snem i na 0 po wybudzeniu [N rola].
  - PA9, PC8, PC9, PB12, PB13, PB14 i PC2 są ustawione jako nieużywane wejścia.
- **Pamięć SPI NOR (SPI1, CS PA3):**
  - Sterownik zna 15 identyfikatorów JEDEC (Winbond, Macronix, SST, GigaDevice, Boya).
  - Mapa: konfiguracja 0x0000; mapy klawiszy 0x1000-0x13FF; dziennik czasu pracy 0xA000/0xB000; makra od 0x10000; efekty użytkownika (adresy z `0x75a8`).
  - Bez rozpoznanej pamięci flaga `0x200011ae` = 0 i zapisy konfiguracji są odrzucane.

### 3.7. USB

- **Device:** VID 0x04D9, PID 0xA293, bcdDevice 0x0100. Stringi: „OBINS”, „OBINS\nAnnePro2” (ze znakiem nowej linii), numer seryjny wypełniany w RAM.
- **Konfiguracja:** remote wakeup, 500 mA, cztery interfejsy HID:
  - **IF0:** klawiatura boot + NKRO (37 B, EP 0x82, 1 ms) z LED na EP 0x01.
  - **IF1:** kanał producenta 64 B (EP 0x03/0x84) dla ObinsKit.
  - **IF2:** consumer (ID 1), klawiatura 6KRO (ID 2), mysz (ID 3, osie 16-bit, kółko, przesuw); EP 0x85.
  - **IF3:** gamepad (Rx, Ry, Rz, X, Y, 16 przycisków; EP 0x86).

## 4. LED 2.33: nowe ustalenia

- **System:** Contiki z procesami „Led Hardware Matrix Process”, „Led Animate Process”, „Keyboard Misc Process” i „AT process”.
- **Tablica poleceń:** 8 wpisów. Grupa `0x21` (wizualizacja dźwięku w ObinsKit) ma pustą procedurę, więc nie działa.
- **Odpowiedź na ping:** `02 01 02` od MCU dostaje odpowiedź `21 02 02 21` (stan 2, wersja 0x0221). `02 03` zwraca emulację odpowiedzi bootloadera (trzy rekordy, bajt 0x40).
- **Efekty:** numery 3-27 (dyspozytor `0x8240`). Nazwy z ObinsKit:

| Nr | Nazwa | Nr | Nazwa |
|---|---|---|---|
| 1/2 | statyczny | 15 | Pulse (reaktywny) |
| 3 | Breathing | 16 | Colorful |
| 4 | Gradual breathing | 17 | Wave |
| 5 | Poptang (reaktywny) | 18 | Black Hole |
| 6 | Rainbow | 19 | Vertical Rainbow |
| 7 | Breathing per key (reaktywny) | 20 | pusta funkcja |
| 10 | Breathing trigger (reaktywny) | 21 | Fountain |
| 11 | Rainbow trigger (reaktywny) | 22 | **brak w ObinsKit**, reaktywny według MCU |
| 12 | Ripple (reaktywny) | 23 | Flame |
| 13 | Starry Sky | 24 | Firework |
| 14 | Comet | 25 | Star Fall (reaktywny) |
| | | 26 | **brak w ObinsKit**, poziomica z akcelerometru |
| | | 27 | **brak w ObinsKit** |

## 5. BLE 2.05: nowe ustalenia

- **Okres sprawdzania baterii** w stanie połączenia: 15 s (sekcja 2). Obok tej stałej leży stała zmiennoprzecinkowa 3,1, ta sama co we wzorze napięcia.
- **Callbacki HID:** `0x0B5E` to wskaźnik `HidDev_Register` na callbacki aplikacji (sekcja 2).
- **Wariant układu:** obraz nie dotyka rejestrów USB ani I2C układu, więc CC2540 czy CC2541 nadal nie rozstrzygnięto [N]. Oba mają ten sam układ wyprowadzeń, jeśli USB i I2C nie są używane (TI).
- **Bez zmian:** pozostałe niewiadome z `04-uklad-ble-8051.md`, sekcja 9 (HCI 0xFC20, zawartość XDATA `0x0CF8`, bootloader).

## 6. ObinsKit 1.2.11: weryfikacja

- **Mapowanie poleceń:** odczytane z `Liana::writeWithName` (moduł `HID_hidraw.node`) i zgodne z kodem MCU dla grup `0x10`, `0x20`, `0x30` i `0x40`. Poprawki do `07-obserwacje-gatt-i-obinskit.md`:
  - `uploadKbdLayout` = `10 02`, czyli odczyt mapy z klawiatury. Zapis mapy to `10 01`.
  - `writeLightHoldOn` = `20 0d` ustawia flagę `0x20001163`, od której zależy odliczanie do snu.
  - Grupa `0x60` (mysz wirtualna) nie ma procedury w MCU 2.36; prędkości myszy są stałymi w RW (`0x2000012c`-`0x2000012e`).
  - Brak w MCU 2.36: `20 14` (`writeIndicatorBrightness`), `20 1c`/`20 1d` (kolor Caps Lock) oraz polecenia z sekcji 3.2.
- **Kody klawiszy i efekty:** sekcje 3.4 i 4.

## 7. Wnioski dla portu QMK w tym forku

- **Przytrzymanie slotu:** fabrycznie trwa 5 s. `AP2_BLE_SLOT_TAP_HOLD` ma domyślnie 500 ms, co jest świadomym odejściem od oryginału; `AP2_BLE_SLOT_HOLD_MS` 5000 daje czas fabryczny. Opisy w `readme.md` i w keymapie `Kazmir_BO` zostały poprawione (wcześniej podawały 500 ms jako zachowanie oryginału).
- **Wskaźnik slotu:** fabryczny MCU gasi go po `20 0c` z BLE; port robi to samo (`ap2_led_cancel_blink()` w obsłudze `BLE_STS_ID_CONNECT`).
- **Handshake:** port pinguje co 50 ms, a potem co 500 ms, czyli dziesięć razy częściej niż oryginał. To tylko przyspiesza odebranie wersji; komentarz w `annepro2.c` opisuje teraz czasy oryginału poprawnie.
- **Przełącznik Bluetooth:** nowa opcja `AP2_BT_SWITCH` (domyślnie wyłączona) odtwarza zachowanie oryginału. Odczytuje PC10 z podciąganiem w dół: stan wysoki to USB, niski to Bluetooth. Wyjście wybiera tylko zmiana stanu przełącznika, stabilna przez 50 ms. Sprawdzone na jednej klawiaturze C18: przełącznik w pozycji Off daje USB, On Bluetooth. Opcja jest włączona w keymapie `Kazmir_BO`; domyślnie pozostaje wyłączona, bo na płytce bez przełącznika pin czyta stan niski i klawiatura pisałaby przez Bluetooth.
- **Bateria:** BLE sprawdza poziom co 15 s w stanie połączenia, a poziom w GATT tylko maleje. Częstsza niż co 15 s wysyłka `01 05` z QMK niczego nie przyspieszy u hosta.

## 8. Niewiadome po tej rundzie

1. **Czujnik:** czy LIS3DH jest obsadzony na płytkach C18 (schemat opisuje PB7/PB8 jako `LED_RESET`/`LED_WKUP`).
2. **Piny:** role PA6, PC3, PB6, PC1 (AF6) oraz obudowa układu MCU (PC10 działa na C18, więc układ ma ten pin).
3. **Ping LED:** zmiana typu ramki pingu LED (0x12/0x10) przy każdym wysłaniu.
4. **Rekord urządzenia:** znaczenie pól rekordu pod 0x3E00 poza PID 0xA293.
5. **BLE:** funkcja `evtCB`, HCI 0xFC20, bootloader i mapowanie adresu 0x4000 na flash CC254x.
6. **Zegar:** potwierdzenie STCLK = HCLK/8 w dokumentacji Holtek.

Źródła zewnętrzne: [instrukcja Anne Pro 2 (ManualsLib)](https://www.manualslib.tech/guide/10407/obinslab-anne-pro-2-keyboard-manual.html), [TI CC2541](https://www.ti.com/product/CC2541).
