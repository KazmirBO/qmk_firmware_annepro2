# LED 2.33 (procesor podświetlenia)

Obraz `rgb-c18-2.33.2.bin`, adresy przy bazie 0x4000. Znaczniki i konwencje: `README.md` §2.

## 1. System

- Contiki OS z czterema procesami: „Led Hardware Matrix Process”, „Led Animate Process”, „Keyboard Misc Process” i „AT process” (nazwy pod `0xbadc`-`0xbb34`, struktury pod `0x20002a28`, `0x20002a3c`, `0x20002a18`, `0x200029a8`) [O].
- Dane inicjalizowane rozpakowane z `0xbb54` do `0x20001000` (6749 B), BSS od `0x20002a60` (0xa50 B), stos od `0x20000600` [O].
- Przy starcie procesu „AT process” (`0xa1a0`) LED wysyła do MCU `01 03` z wersją u16 0x0221 (`0x9584`, bajt adresowy `0x34`) [O].

## 2. Tablica poleceń

Tablica `0x200029b8` ma 12 wpisów: 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30, 0x40, 0x50, 0x60 (pętla w `0xa15e`-`0xa182` porównuje indeks z 12). Procedury mają tylko `0x01` (`0xa652`), `0x02` (`0xa5b4`), `0x20` (`0xa228`) i `0x21` (`0xa224`, sama instrukcja `bx lr`) [O]. Grupa `0x21` to w ObinsKit wizualizacja dźwięku (`writeAudioVisualConfig` `21 01`, `writeAudioVisualData` `21 02`), która przy pustej procedurze nie działa [O].

## 3. Polecenia

| Ramka | Kierunek | Znaczenie [O] |
|---|---|---|
| `01 03` | do LED | odpowiedź: wersja 0x0221 |
| `02 01 02` | MCU do LED | ping; odpowiedź `21 02 02 21`, z której MCU czyta stan (bajt 1, czyli 2) i wersję (bajty 2-3) |
| `02 03` | do LED | emulacja odpowiedzi bootloadera: 24 B, trzy rekordy po 8 B z 0x40 w bajcie 5 |
| `20 01` | do LED | przełączenie podświetlenia; odpowiedź 2 B (stan, tryb). MCU przekazuje do LED `20 01` od komputera |
| `20 03 <x> <tryb> [dane]` | do LED | wybór efektu: tryb 1 kolor stały RGB, 2 kolory per klawisz, 3 i 4-160 efekty z danymi. Gdy zlecił to komputer (nadawca 1 lub 2), LED odpowiada 0 i wysyła do MCU `20 03 ff <tryb>`; gdy MCU, odsyła `20 03 <x> <tryb>` |
| `20 06 <F3/F4>` | MCU do LED | jasność w górę albo w dół; LED odsyła `20 06 <jasność>` |
| `20 07 <0/1>` | MCU do LED | dioda Caps Lock (także stan hosta z BLE) |
| `20 08 <5 × u16>` | MCU do LED | bitmapa wciśniętych klawiszy dla efektów reaktywnych; MCU wysyła ją tylko w trybach 5, 7, 10, 11, 12, 15, 22, 25 i przy niezerowej bitmapie |
| `20 09 <b>` | MCU do LED | ustawienie z `writeKbdCapslock` (`10 07`) |
| `20 0b <slot> <1/0>` | MCU do LED | wskaźnik slotu BLE: 1 parowanie, 0 łączenie |
| `20 0c 00 00` | MCU do LED | wyłączenie wskaźnika slotu (po połączeniu BLE) |
| `20 0f <procent>` | MCU do LED | wskaźnik baterii; gdy wskaźnik jest włączony, ramka go gasi niezależnie od wartości |
| `20 12 <1/0>` | MCU do LED | wskaźnik trybu kierownicy |
| `20 15 <b>` | oba | jasność podświetlenia; LED odsyła ją do MCU |
| `20 16 <x> <y> <z>` | MCU do LED | starsze bajty osi akcelerometru dla efektu 26 |
| `20 1b <R> <G> <B>` | MCU do LED | kolor wskaźnika [I co do nazwy] |
| `20 1f <b>` | MCU do LED | tryb zasilania LED [I co do nazwy] |

`20 05` nie trafia do LED: to wewnętrzne polecenie MCU „następny efekt”, które kończy się wysłaniem `20 03` (`03-mcu.md` §6). Czy LED obsługuje `20 11` (`writeAudioVisualize` w ObinsKit), nie sprawdzono [N].

## 4. Efekty

Dyspozytor `0x8240` obsługuje tryby 3-27 przez tablicę `0x8254`; tryby 1 i 2 obsługuje bezpośrednio `20 03` [O]. Nazwy z ObinsKit 1.2.11 (mapa efektów w `out/async-commons.js`, grupa „Trigger” to efekty reaktywne) [O]:

| Nr | Nazwa | Nr | Nazwa |
|---|---|---|---|
| 1, 2 | Static | 15 | Pulse (Trigger) |
| 3 | Breathing | 16 | Colorful |
| 4 | Gradual breathing | 17 | Wave |
| 5 | Poptang (Trigger) | 18 | Black Hole |
| 6 | Rainbow | 19 | Vertical Rainbow |
| 7 | Breathing per key (Trigger) | 20 | pusta funkcja (`0x49a4`), bez nazwy w ObinsKit |
| 8, 9 | pusta gałąź (`0x82f6`), bez nazwy | 21 | Fountain |
| 10 | Breathing trigger (Trigger) | 22 | brak w ObinsKit; reaktywny według MCU (`0x5718`) |
| 11 | Rainbow trigger (Trigger) | 23 | Flame |
| 12 | Ripple (Trigger) | 24 | Firework |
| 13 | Starry Sky | 25 | Star Fall (Trigger) |
| 14 | Comet | 26 | brak w ObinsKit; poziomica z akcelerometru (`0x7db0`) |
| | | 27 | brak w ObinsKit (`0x7bd8`) |

Lokalizacja ObinsKit zawiera też nazwę „Snake”, której żaden numer efektu nie używa [O].

## 5. Efekt poziomicy (nr 26)

Procedura `0x7db0` bierze oś Y z ostatniej ramki `20 16` i zamienia ją na kolumnę 0-13 (dla Y ≥ 0: Y/18 + 6, najwyżej 13), którą podświetla [O]. Dane wysyła MCU (`03-mcu.md` §11), więc efekt działa tylko z obsadzonym akcelerometrem.
