# LED 2.33 (procesor podświetlenia)

Obraz `rgb-c18-2.33.2.bin`, adresy przy bazie 0x4000. Znaczniki i konwencje: `README.md` §2. Wiersze i kolumny matrycy LED liczone są od 0 (wiersz 0 to rząd cyfr, kolumna 0 to lewa krawędź), tak jak w kodzie.

## 1. System

- Contiki OS z czterema procesami; nazwy pod `0xbadc`-`0xbb34`, struktury w danych RW wiążą nazwę z funkcją [O]:

| Proces | Funkcja | Struktura | Rola [O] |
|---|---|---|---|
| AT process | `0xa184` | `0x200029a8` | odbiór ramek z USART1 i tablica poleceń (§2) |
| Led Animate Process | `0xb73c` | `0x20002a18` | co tick: wybór tego, co wyświetlić (`0x838e`, §6), i przeliczenie ramki na bufory wyjść (`0xb02c`, §7) |
| Led Hardware Matrix Process | `0xb79c` | `0x20002a28` | ustawia flagę `0x2000345d`; odpytywany z przerwania IRQ 12 |
| Keyboard Misc Process | `0x9030` | `0x20002a3c` | uśpienie na żądanie z linii PB12 i zasilanie matrycy (§8) |

- Takt: procedura SysTick (`0xb5b8`) przy każdym przerwaniu odpytuje proces animacji. SysTick liczy z STCLK z przeładowaniem 60 000, wyliczonym z zegara 48 MHz (zmienna `0x20002a4c` = 48 000 000) jako 48 000 000 / 8 / 100 (`0x9114`) [O]. Tick wynosi więc 10 ms przy STCLK = `CK_AHB`/8, jak w MCU [I mocne].
- Dane inicjowane rozpakowane z `0xbb54` do `0x20001000` (6749 B), BSS od `0x20002a60` (0xa50 B), stos od `0x20000600` [O].
- Przy starcie procesu „AT process” LED wysyła do MCU `01 03` z wersją u16 0x0221 (`0x9584`, bajt adresowy `0x34`) [O].
- Odpowiedzi (`0xac68`) idą tylko do nadawców 1, 2 i 3, jak w MCU i BLE [O].

## 2. Tablica poleceń

Tablica `0x200029b8` ma 12 wpisów: 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30, 0x40, 0x50, 0x60 (pętla w `0xa15e`-`0xa182` porównuje indeks z 12). Procedury mają tylko `0x01` (`0xa652`), `0x02` (`0xa5b4`), `0x20` (`0xa228`) i `0x21` (`0xa224`, sama instrukcja `bx lr`) [O]. Grupa `0x21` to w ObinsKit wizualizacja dźwięku (`writeAudioVisualConfig` `21 01`, `writeAudioVisualData` `21 02`), która przy pustej procedurze nie działa [O].

## 3. Polecenia

Procedura `0xa228` obsługuje podpolecenia 0x01, 0x03, 0x06-0x09, 0x0b, 0x0c, 0x0f, 0x11, 0x12, 0x15, 0x16, 0x1b i 0x1f. Każde inne `20 xx` (np. `20 02`, `20 04`, `20 0d`, `20 14`, `20 1a`, `20 1c`-`20 1e`) dostaje odpowiedź 1 B = 1 [O].

| Ramka | Kierunek | Znaczenie [O] |
|---|---|---|
| `01 03` | do LED | odpowiedź: wersja 0x0221 |
| `02 01 02` | MCU do LED | ping; odpowiedź `21 02 02 21`, z której MCU czyta stan (bajt 1, czyli 2) i wersję (bajty 2-3) |
| `02 03` | do LED | emulacja odpowiedzi bootloadera: 24 B, trzy rekordy po 8 B z 0x40 w bajcie 5 |
| `20 01` | do LED | przełączenie podświetlenia; odpowiedź 2 B (stan, tryb). MCU przekazuje do LED `20 01` od komputera |
| `20 03 <x> <tryb> [dane]` | do LED | wybór efektu: tryb 1 kolor stały RGB, 2 kolory per klawisz, 3 i 4-160 efekty z danymi. Gdy zlecił to komputer (nadawca 1 lub 2), LED odpowiada 0 i wysyła do MCU `20 03 ff <tryb>`; gdy MCU, odsyła `20 03 <x> <tryb>` |
| `20 06 <F3/F4>` | MCU do LED | poziom jasności o 1 w górę (F3) albo w dół (F4), w zakresie 1-10; LED odsyła `20 06 <poziom>` |
| `20 07 <0/1>` | MCU do LED | stan Caps Lock (także stan hosta z BLE); rysowanie wskaźnika w §6 |
| `20 08 <5 × u16>` | MCU do LED | bitmapa wciśniętych klawiszy dla efektów reaktywnych; MCU wysyła ją tylko w trybach 5, 7, 10, 11, 12, 15, 22, 25 i przy niezerowej bitmapie |
| `20 09 <b>` | MCU do LED | styl wskaźnika Caps Lock (z `writeKbdCapslock`, `10 07`): 0 tylko klawisz Caps Lock, 1 cała klawiatura (§6) |
| `20 0b <slot> <1/0>` | MCU do LED | wskaźnik slotu BLE: 1 parowanie, 0 łączenie (§6) |
| `20 0c 00 00` | MCU do LED | wyłączenie wskaźnika slotu (po połączeniu BLE) |
| `20 0f <procent>` | MCU do LED | wskaźnik baterii (§6); gdy wskaźnik jest włączony, ramka go gasi niezależnie od wartości |
| `20 11` | do LED | brak działania: tylko odpowiedź 0, i to wyłącznie dla nadawcy 1; `writeAudioVisualize` z ObinsKit nic więc nie zmienia |
| `20 12 <1/0>` | MCU do LED | wskaźnik trybu kierownicy (§6) |
| `20 15 <b>` | oba | poziom jasności: zapisuje min(b, 10); LED odsyła wartość do MCU |
| `20 16 <x> <y> <z>` | MCU do LED | starsze bajty osi akcelerometru dla efektu 26 |
| `20 1b <R> <G> <B>` | MCU do LED | kolor wskaźnika Caps Lock (`0x2000318a`, §6); bez odpowiedzi |
| `20 1f <b>` | MCU do LED | tabela jasności (§7): 2 daje zakres 0-36, każda inna wartość 0-15; nazwa w ObinsKit `writeLedPowerMode` |

`20 05` nie trafia do LED: to wewnętrzne polecenie MCU „następny efekt”, które kończy się wysłaniem `20 03` (`03-mcu.md` §6).

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

Martwy kod: `0x82fa` z procedurami `0x8090`, `0x812c` i `0x8160` rysuje m.in. słupki w 14 kolumnach o wysokości z tablicy `0x200031b0`-`0x200031bd` (wartość / 50 wierszy), czyli coś w rodzaju wizualizacji dźwięku [I]. Uruchamia go tylko bajt `0x200031af`, a w kodzie nie ma żadnego zapisu ani do niego, ani do tablicy wysokości, więc ta ścieżka nigdy się nie wykonuje [O co do braku zapisów; I co do wywołań pośrednich].

## 5. Efekt poziomicy (nr 26)

Procedura `0x7db0` bierze oś Y z ostatniej ramki `20 16` i zamienia ją na kolumnę 0-13 (dla Y ≥ 0: Y/18 + 6, najwyżej 13), którą podświetla [O]. Dane wysyła MCU (`03-mcu.md` §11), więc efekt działa tylko z obsadzonym akcelerometrem.

## 6. Wskaźniki i ich pierwszeństwo

Proces animacji co tick woła `0x838e`, które wybiera jedną rzecz do wyświetlenia w ustalonej kolejności i zapisuje stan pod `0x20003069` [O]. Dopóki aktywny jest któryś wskaźnik, procedura efektu nie jest wołana, więc animacja efektu stoi [O]:

| Kolejność | Stan | Warunek | Co świeci [O] |
|---|---|---|---|
| 1 | 2 | `20 0b <slot> <tryb>` | klawisz w wierszu 0 i kolumnie slot + 1 (klawisze 1-4) miga na zielono (0, 255, 0), reszta jest wygaszana; tryb 1 (parowanie): 21 ticków świeci, 20 nie (okres 0,41 s); tryb 0 (łączenie): 101 i 100 ticków (okres 2,01 s); koniec po 6001 tickach (około 60 s) albo po `20 0c` (`0x7fe8`) |
| 2 | 4 | Caps Lock włączony albo zmieniony (`20 07`) | styl 0 (`20 09 0`): klawisz w wierszu 2 i kolumnie 0 (Caps Lock) w kolorze z `20 1b`, a przy wyłączonym podświetleniu reszta zgaszona; styl 1: cała klawiatura w tym kolorze (`0x7f04`) |
| 3 | 5 | `20 12 1` | w wierszu 0 świecą na czerwono pary klawiszy w kolumnach i + 1 i 12 - i, przesuwające się od krawędzi do środka co 10 ticków (0,1 s), siedem kroków w kółko, reszta zgaszona (`0x7f5c`) |
| 4 | 3 | `20 0f <procent>` | w wierszu 0 kolumny 1 do procent/10 na zielono, dalej do kolumny 10 na czerwono, czyli klawisze 1-0 jako pasek co 10 %; reszta zgaszona (`0x4120`) |
| 5 | 1 | żaden z powyższych | bieżący efekt (§4) |

Kolor wskaźnika Caps Lock ma przed pierwszym `20 1b` wartość zerową (BSS); MCU wysyła `20 1b` po handshake (`03-mcu.md` §3) [O]. Czasy w sekundach zakładają tick 10 ms (§1).

## 7. Matryca LED

Piny odczytane z kodu: wiersze z masek w `0xb02c`, kolumny z tablicy w danych RW `0x200027fc` (po 8 B: port i maska), zasilanie z `0x94ec`/`0x94fa` [O]. Wszystkie co do bitu zgadzają się z wariantem C18 otwartego firmware `annepro2-shine` (commit `0bcaf1c`, `board/board.h`) [O, porównanie ze źródłem zewnętrznym]:

| Wiersz | R | G | B |
|---|---|---|---|
| 0 | PA11 | PB2 | PC14 |
| 1 | PA14 | PB3 | PA6 |
| 2 | PA15 | PB4 | PA7 |
| 3 | PB0 | PB5 | PC4 |
| 4 | PC5 | PC7 | PC6 |

- Kolumny 0-13: PB1, PD1, PD2, PD3, PC15, PC1, PC2, PC3, PB6, PB7, PB8, PA0, PA1, PA2 [O].
- Zasilanie matrycy: PA10, stan 1 włącza (`0x94ec`), 0 wyłącza (`0x94fa`) [O]; w `annepro2-shine` ten sam pin i ta sama polaryzacja [zewn.].
- Ramka: 5 × 14 diod po 3 B w kolejności R, G, B (wskaźnik `0x200032cc`, 210 B) [O].
- Przeliczenie (`0xb02c`, po każdym kroku animacji): kanał R mnożony przez 0,8 (stała double `0x3FE999999999999A`), potem każdy kanał zamieniany na 33 poziomy wzorem (v + 1) >> 3 i rozkładany na 32 szczeliny czasowe w buforach portów PA, PB i PC; bufory są podwójne [O].
- Wyjście (`0xb288`, przerwanie BFTM1, IRQ 18): każde przerwanie obsługuje jedną kolumnę i jedną szczelinę. Ustawia piny wierszy (GPIO SRR), włącza kolumnę na czas pętli o liczbie przebiegów z tabeli jasności, wyłącza kolumnę i wiersze (GPIO RR); po 14 kolumnach przechodzi do następnej z 32 szczelin [O]. Okres BFTM1 to 20 · 48 = 960 taktów (`0x9394`), czyli 20 µs przy 48 MHz, a pełna ramka 448 przerwań, około 9 ms [I co do zegara BFTM].
- Jasność: poziom 1-10 (`0x200031a6`) wybiera wpis tabeli `poziom - 1`. Tabela dla `20 1f 2` pod `0x200012e0`: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36; dla każdej innej wartości pod `0x200012ec`: 0, 1, 2, 3, 5, 7, 9, 11, 13, 15 [O]. Bajt trybu (`0x200031c8`) leży w BSS, więc do pierwszego `20 1f` obowiązuje tabela 0-15 [O]. Nazwa ObinsKit „tryb zasilania LED” pasuje do tego, że tryb 2 pozwala na ponad dwukrotnie dłuższe świecenie [I].

## 8. Uśpienie układu LED

- Przerwanie EXTI linii 12 (IRQ 6, `0xb6d8`) przekazuje zdarzenie procesowi Misc, a ten (`0x9518`) czyta PB12 [O].
- Przy PB12 = 1 i stanie 1 (żaden wskaźnik z §6 nie jest aktywny) LED ustawia wszystkie piny matrycy w stan nieaktywny (`0x93d4`), wyłącza PA10, uzbraja wybudzenie z linii EXTI 12 (`0xa874`, `0xa858`) i przechodzi w DeepSleep (`0xa928`: SLEEPDEEP i WFI). Po wybudzeniu włącza PA10 z powrotem, jeśli podświetlenie jest włączone [O].
- Przy PB12 = 0 i włączonym podświetleniu LED tylko wznawia matrycę [O].
- MCU ustawia PC3 = 1 przed snem tylko wtedy, gdy podświetlenie, Caps Lock i wskaźnik baterii są wyłączone (`03-mcu.md` §7), czyli gdy LED nie ma nic do pokazania. PC3 w MCU to więc najpewniej linia, która usypia układ LED przez PB12 [I; potwierdzi schemat C18 albo pomiar]. Ani port QMK, ani `annepro2-shine` tej linii nie używają [O].
