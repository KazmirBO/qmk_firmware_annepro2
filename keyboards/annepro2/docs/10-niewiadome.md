# Niewiadome

Stan na 2026-10-27, po weryfikacji z `09-metoda-i-weryfikacja.md` §6. Przy każdym punkcie: co by go rozstrzygnęło.

## 1. Płytka i piny MCU

| Pytanie | Co rozstrzygnie |
|---|---|
| Czy akcelerometr LIS3DH jest obsadzony na płytkach C18 (`03-mcu.md` §11) | oględziny płytki albo odpowiedź `01 08` (bajt obecności czujnika) z ObinsKit lub własnego narzędzia |
| Czy PA4 przełączany na GPIO w stanie wysokim przed długim snem budzi układ BLE i który pin MCU steruje wejściem P0.1 układu BLE (`03-mcu.md` §7, `05-ble.md` §10) | schemat C18 albo podsłuch PA4 przy przejściu w długi sen i wybudzeniu |
| Role PA6 (tylko źródło wybudzenia), PB6 (wyjście 1 przy starcie), PC1 (funkcja 6) i PC3 (przed snem; przesłanki wskazują na układ LED [I]) | schemat C18 albo pomiar, np. PC3 przy włączonym i wyłączonym podświetleniu |
| Schemat płytki C18 (LQFP64): znany schemat rysuje wariant 48-pinowy (`01-architektura.md` §7) | schemat producenta albo odrysowanie płytki |
| Mapowanie funkcji 6 i 5 AFIO na USART0/USART1 i SPI1 jest zgodne z QMK, ale nie z kartą katalogową | tabela funkcji alternatywnych w podręczniku HT32F52342/52352 |

## 2. MCU 2.36

| Pytanie | Co rozstrzygnie |
|---|---|
| Czy SysTick naprawdę liczy z STCLK = `CK_AHB`/8 (tick 10 ms) i czy ADCDIV = 6 to podział przez 64 | User Manual Holtek HT32F52342/52352 (rejestr SysTick i CKCU) |
| Skąd pochodzą dane `50 07` spod `0x200002c4` (`03-mcu.md` §11) | wyszukanie zapisów pośrednich przez wskaźniki do struktury `0x20000288` albo odczyt pamięci działającego MCU |
| Funkcje zmiennoprzecinkowe `0xd950`, `0xdad4`, `0xd820` rozpoznane z kontekstu jako mnożenie, dzielenie i `atan2` | porównanie z biblioteką fplib Keil |
| Bootloader MCU (0x0000-0x3FFF), rozmiar strony flash i to, czy 0x3E00 to ostatnia strona obszaru bootloadera | zrzut flash przez debuger albo karta katalogowa (rozmiar strony) |
| Znaczenie bajtów +0, +1 i u16 +4 (0x3311) rekordu urządzenia (`03-mcu.md` §12) | kod bootloadera albo ObinsKit |
| Cel przełączania typu ramki pingu LED (`0x12`/`0x10`) | kod bootloadera LED albo podsłuch USART0 przy starcie |
| Przeznaczenie wewnętrznych ramek `40 07 <n>` z adresem 0, których router nie wysyła (`02-protokol.md` §2) | dalsza analiza procesu macierzy (`0xb734`) |

## 3. LED 2.33

| Pytanie | Co rozstrzygnie |
|---|---|
| Czy LED obsługuje `20 11` (`writeAudioVisualize` w ObinsKit) | analiza gałęzi `0x11` w `0xa228` |
| Znaczenie `20 1b` (kolor wskaźnika) i `20 1f` (tryb zasilania LED) przyjęte z nazw ObinsKit | test z ObinsKit albo analiza `0x883c` i `0x88d8` |

## 4. BLE 2.05

| Pytanie | Co rozstrzygnie |
|---|---|
| CC2540 czy CC2541 | odczyt CHIPID (`X:624A`) z urządzenia albo porównanie `1:9D1E`-`1:9D40` z bibliotekami TI BLE dla obu układów |
| Bootloader BLE, protokół IAP i odwzorowanie bazy 0x4000 z bootloadera na flash od fizycznego 0x2000; czy wgrywanie obrazu BLE przez AnnePro2-Tools jest bezpieczne | zrzut flash przez debuger CC254x albo nagranie aktualizacji z ObinsKit |
| Bajty plik+0x90-0x91 (`93 8e`) i ewentualna suma kontrolna obrazu | kod bootloadera BLE |
| Rola stałej 8 B pod plik+0x619A, używanej w `1:9F6A` | dalsza analiza kodu warstwy łącza |
| Czy 15 B ujawnianych przy odczycie raportu wyjściowego ID 2 może zawierać materiał kluczowy (`05-ble.md` §6) | prześledzenie bufora odpowiedzi ATT albo kontrolowany test na urządzeniu |
| Nazwa polecenia HCI 0xFC20 (zdarzenie 0x0420, 9 B na połączenie) | nagłówek `hci.h` z TI BLE SDK 1.4.x |
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
| Czy wcześniej wgrywane obrazy są identyczne z buildami swoich commitów | build starszych commitów w osobnym drzewie |
| Czy układ BLE przyjmuje numer seryjny od portu (`AP2_BLE_SERIAL`), czy rekord pod `0x3E00` na danym egzemplarzu zawiera UDID i czy ramka budząca w formacie oryginału działa tak jak poprzednia | wgranie `6c03dfe813` i odczyt Device Information (`08-testy-na-sprzecie.md` §3.1) |
