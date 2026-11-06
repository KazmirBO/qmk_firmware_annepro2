# Niewiadome

Stan na 2026-11-03: lista z weryfikacji z 2026-10-27 (`09-metoda-i-weryfikacja.md` §6) po uzupełnieniu z 2026-11-03 (`09-metoda-i-weryfikacja.md` §8). Przy każdym punkcie: co by go rozstrzygnęło.

## 1. Płytka i piny MCU

| Pytanie | Co rozstrzygnie |
|---|---|
| Czy akcelerometr LIS3DH jest obsadzony na płytkach C18 (`03-mcu.md` §11) | oględziny płytki albo odpowiedź `01 08` (bajt obecności czujnika) z ObinsKit lub własnego narzędzia |
| Czy PA4 przełączany na GPIO w stanie wysokim przed długim snem budzi układ BLE i który pin MCU steruje wejściem P0.1 układu BLE (`03-mcu.md` §7, `05-ble.md` §10) | schemat C18 albo podsłuch PA4 przy przejściu w długi sen i wybudzeniu |
| Czy PC3 w MCU jest połączony z PB12 układu LED (linia uśpienia, `04-led.md` §8); kod obu stron do siebie pasuje [I] | schemat C18 albo pomiar PC3 przy wyłączonym podświetleniu i uśpieniu |
| Role PA6 (tylko źródło wybudzenia), PB6 (wyjście 1 przy starcie) i PC1 (funkcja 6) | schemat C18 albo pomiar |
| Schemat płytki C18 (LQFP64): znany schemat rysuje wariant 48-pinowy (`01-architektura.md` §7) | schemat producenta albo odrysowanie płytki |
| Mapowanie funkcji 6 i 5 AFIO na USART0/USART1 i SPI1 jest zgodne z QMK, ale nie z kartą katalogową | tabela funkcji alternatywnych w podręczniku HT32F52342/52352 |

## 2. MCU 2.36

| Pytanie | Co rozstrzygnie |
|---|---|
| Czy SysTick naprawdę liczy z STCLK = `CK_AHB`/8 (tick 10 ms) i czy ADCDIV = 6 to podział przez 64 | User Manual Holtek HT32F52342/52352 (rejestr SysTick i CKCU) |
| Kiedy aktywne są warstwy 3 (strzałki w prawym dolnym rogu) i 4 (przyciski gamepada) mapy klawiszy (`03-mcu.md` §6.1) | analiza wyboru warstwy w procesie macierzy |
| Czy przy mapie z SPI warstwa 4 (SPI 0x2000, której kod nigdy nie zapisuje) ma treść z fabryki | odczyt SPI przez `03 01` |
| Bootloader MCU (0x0000-0x3FFF), rozmiar strony flash i to, czy 0x3E00 to ostatnia strona obszaru bootloadera | zrzut flash przez debuger albo karta katalogowa (rozmiar strony) |
| Znaczenie bajtów +0, +1 i u16 +4 (0x3311) rekordu urządzenia (`03-mcu.md` §12) | kod bootloadera albo ObinsKit |
| Cel przełączania typu ramki pingu LED (`0x12`/`0x10`) | kod bootloadera LED albo podsłuch USART0 przy starcie |

## 3. LED 2.33

| Pytanie | Co rozstrzygnie |
|---|---|
| Zegar BFTM1, a więc rzeczywisty okres szczeliny matrycy (20 µs przy 48 MHz, `04-led.md` §7) | User Manual HT32F52342 albo pomiar oscyloskopem na pinie kolumny |
| Rola flagi `0x2000345d` ustawianej przez „Led Hardware Matrix Process” | dalsza analiza odczytów tej flagi |

## 4. BLE 2.05

| Pytanie | Co rozstrzygnie |
|---|---|
| CC2540 czy CC2541 | odczyt CHIPID (`X:624A`) z urządzenia albo porównanie `1:9D1E`-`1:9D40` z bibliotekami TI BLE dla obu układów |
| Bootloader BLE, protokół IAP i odwzorowanie bazy 0x4000 z bootloadera na flash od fizycznego 0x2000; czy wgrywanie obrazu BLE przez AnnePro2-Tools jest bezpieczne; czy bootloader, jak bootloader szeregowy TI, sprawdza CRC pod plik+0x90 i zapisuje jego kopię pod plik+0x92, nadpisując dwa bajty danych inicjujących (`05-ble.md` §2) | zrzut flash przez debuger CC254x albo nagranie aktualizacji z ObinsKit |
| Znaczenie stałej pod plik+0x619A (jako u16: 27 000, 44 000, 50 000, 54 000) i rekordów strony informacyjnej od `X:7848` | dalsza analiza `1:9F40`-`1:A0B0` albo odczyt strony informacyjnej z urządzenia |
| Czy 15 B ujawnianych przy odczycie raportu wyjściowego ID 2 może zawierać materiał kluczowy (`05-ble.md` §6) | prześledzenie bufora odpowiedzi ATT albo kontrolowany test na urządzeniu |
| Napięcie odniesienia i dzielnik w przeliczeniu `01 05` (3,1 V i 2:1 pochodzą z kodu, nie z pomiaru) | pomiar multimetrem przy znanym odczycie ADC |
| Dystrybucja kluczy, rozmiar klucza i parametry połączenia przyjęte przez hosta | nagranie `btmon` (`08-testy-na-sprzecie.md` §3.2) |
| Pełna logika wejść P0.0 i P0.1 (`4:8F30`, `4:8EC7`, tryb zależny od `X:0846`) | dalsza analiza |
| Czy kod `60 04` jest naprawdę martwy; wniosek opiera się na braku stałych wywołań uzbrajających zdarzenie 0x0008 | szukanie wywołań pośrednich albo test: `60 04` wysłane do BLE i obserwacja raportów u hosta |

## 5. ObinsKit i bootloader

| Pytanie | Co rozstrzygnie |
|---|---|
| Co wysyła wątek `sendReadDeviceTypeV1` uruchamiany przez `readDeviceType` po USB | dalsza analiza `HID_hidraw.node` |
| Czy krótkie ramki zapisu są dopełniane do pełnej długości raportu | przechwycenie ruchu USB HID ObinsKit |
| Znaczenie dwóch pierwszych bajtów (`7b 10`) i flag w odpowiedzi bootloadera na `02 03` | kod bootloadera albo nagranie aktualizacji |
| Zachowanie ObinsKit przy niezgodnej sumie SHA-256 pobranego obrazu (z lektury kodu: pominięcie obrazu i komunikat sukcesu) | uruchomienie w kontrolowanym środowisku |

## 6. Port QMK

| Pytanie | Co rozstrzygnie |
|---|---|
| Zgodność procentu baterii z napięciem ogniwa i działanie korekty `AP2_BATTERY_EMPTY_MV` przy niskim napięciu | pomiar multimetrem, w tym tuż po wyłączeniu się klawiatury |
| Czy `40 04` wysłane do hosta, z którym łącze BLE nadal trwa (po `KC_AP2_USB` albo przełączniku), wywołuje ponownie `20 0c`; jeśli nie, limit `AP2_BLE_CONNECT_TIMEOUT_MS` mógłby wrócić na USB mimo żywego łącza (limit działa tylko po przełączeniu z USB i ma wyłącznik `0`) [N] | tap slotu przy połączonym hoście z nagraniem UART (`08-testy-na-sprzecie.md` §3.3) |
| Czy obrazy w `do-wgrania/wycofanie/` są identyczne z buildami swoich commitów | build starszych commitów w osobnym drzewie |
| Czy układ BLE przyjmuje numer seryjny od portu (`AP2_BLE_SERIAL`), czy rekord pod `0x3E00` na danym egzemplarzu zawiera UDID i czy ramka budząca w formacie oryginału działa tak jak poprzednia | wgranie `6c03dfe813` i odczyt Device Information (`08-testy-na-sprzecie.md` §3.1) |

## 7. Rozstrzygnięte 2026-11-03

| Było | Jest |
|---|---|
| Skąd pochodzą dane `50 07` spod `0x200002c4` | nic tam nie pisze: 4 B zer i pierwszy bajt bufora odbiorczego procesu AT (`03-mcu.md` §11) |
| Funkcje `0xd950`, `0xdad4`, `0xd820` | mnożenie, dzielenie i `atan2` double, potwierdzone strukturą wywołań i stałymi π (`03-mcu.md` §11) [I mocne] |
| Przeznaczenie ramek `40 07 <n>` | martwy kod: etimery, które je wyzwalają, nigdy nie są uzbrajane (`02-protokol.md` §2) |
| Czy LED obsługuje `20 11` | nie: tylko potwierdzenie dla nadawcy 1 (`04-led.md` §3) |
| Znaczenie `20 1b` i `20 1f` | kolor wskaźnika Caps Lock i wybór tabeli jasności (`04-led.md` §3, §6, §7) |
| Bajty plik+0x90-0x91 (`93 8e`) i suma kontrolna obrazu BLE | CRC-16/XMODEM całego pliku, jak w bootloaderze szeregowym TI (`05-ble.md` §2) |
| Nazwa polecenia HCI 0xFC20 | `HCI_EXT_GET_CONNECTION_INFO`, zdarzenie 0x0420 (`05-ble.md` §7) [zewn.] |
| Rola PC3 | przesunięta do §1 jako linia uśpienia LED [I] |
