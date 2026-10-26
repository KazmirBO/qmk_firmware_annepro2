# Protokół ramek między procesorami (MCU, LED, BLE, host)

Znaczniki: **[O]** obserwacja z kodu lub bajtów, **[I]** inferencja, **[N]** niewiadoma.
Adresy w obrazie MCU (`key-c18-2.36.3.bin`) są adresami wirtualnymi przy założeniu bazy 0x4000 (patrz `05-metoda-i-dowody.md`).

## 1. Format ramki

Ramka składa się z 8-bajtowego nagłówka, dwóch bajtów polecenia i danych. Ten sam format służy do łączności MCU z układem LED, z układem BLE oraz z komputerem przez USB HID (ObinsKit).

| Offset | Pole | Opis |
|---|---|---|
| 0 | `0x7B` | znak początku ramki (`{`) [O] |
| 1 | typ | `0x12` (ramka pojedyncza) albo `0x10` (ramka z licznikiem fragmentów) [O] |
| 2 | adresy | wysoka połówka bajtu to węzeł docelowy, niska to węzeł nadawcy [O] |
| 3 | sekwencja | dla typu `0x12`: bit 7 flaga, bity 0-6 numer; dla typu `0x10`: wysoka połówka to liczba fragmentów, niska to indeks fragmentu [O] |
| 4-5 | długość | little-endian, liczy bajty polecenia i danych (czyli 2 + liczba bajtów danych) [O] |
| 6 | zero | zawsze 0 przy budowaniu ramki [O] |
| 7 | `0x7D` | znak końca nagłówka (`}`) [O] |
| 8 | `cmd` | grupa polecenia [O] |
| 9 | `sub` | podpolecenie [O] |
| 10... | dane | `długość - 2` bajtów [O] |

Numeracja węzłów i grup poleceń jest zgodna z niezależnym kodem `AnnePro2-Tools` (`AP2Target`: UsbHost=1, BleHost=2, McuMain=3, McuLed=4, McuBle=5; `L2Command`: GLOBAL=0x01, FW=0x02, KEYBOARD=0x10, LED=0x20, MACRO=0x30, BLE=0x40; nagłówek ramki z bajtem typu `0x10` i bajtem 3 równym `0x10`) [O, źródło zewnętrzne]. Z tego samego kodu wynika nazwa `02 01`: `IapMode`, gdzie dane `0x01` oznaczają wejście w tryb IAP, a `0x02` powrót do aplikacji.

Dowody (MCU 2.36): budowanie nagłówka w `0x85c0` (bajty 0x7B, 0x12, półbajty adresów, 0x7D w `[7]`) i `0x864a` (typ dowolny); wysyłka pól `cmd`/`sub`/dane w `0x8606`; odbiór w `0x86ae` (szuka `0x7B`, akceptuje tylko typ `0x12` lub `0x10`, czyta 6 bajtów, potem polecenie i dane przez `0x8748`).

Uwagi:
- Odbiornik MCU sprawdza w nagłówku tylko bajty 0 i 1. Bajtów 6 i 7 nie weryfikuje w miejscu, które przeczytałem (`0x86ae`) [O]. Nie znalazłem sumy kontrolnej ani CRC ramki [O, w zakresie przeczytanego kodu].
- Typ `0x10`: funkcja `0x870a` rozkłada bajt 3 na liczbę fragmentów i indeks, a `0x8778` odrzuca fragmenty o niezgodnym indeksie [O]. Sens "wysoka połówka to liczba fragmentów" wynika z `0x870a` (porównanie `total == idx + 1`) [I].
- Nadawca wysyła większe dane w kawałkach: `0x87b0` dzieli dane na fragmenty po 54 bajty (domyślnie), 8 bajtów (cel 2) lub 256 bajtów (cel 4) [O].

## 2. Węzły

| Numer | Rola | Dowód |
|---|---|---|
| 1 | komputer przez USB HID (ObinsKit), 64-bajtowe raporty, endpoint 4 | `0x88d0`, `0x840c` (zapis EP4 przez `0xa97a`) [O] |
| 2 | zdalny host przez BLE (dane docierają przez układ BLE) | `0x88d0` wysyła cel 2 tą samą drogą co cel 5, `0x899e` odpowiada na nadawcę 2 [O]; rola "host przez BLE" to [I] |
| 3 | procesor aplikacyjny (MCU klawiatury) | cel 3 trafia do lokalnej kolejki (`0x8400`) i tablicy poleceń [O] |
| 4 | procesor LED | cel 4 idzie kolejką `0x20000624` na USART0 [O] |
| 5 | procesor Bluetooth | cel 5 idzie kolejką `0x20000630` na USART1 [O] |

Odpowiedź dostaje nadawca z zamienionymi połówkami adresów (`0x899e`, `0x894c`): na zapytanie od węzła 1 MCU odpowiada ramką z bajtem 2 równym `0x13`, od węzła 2 równym `0x23` [O].

Bajt adresów w praktyce: `0x53` to MCU do BLE, `0x43` to MCU do LED, `0x35` to BLE do MCU, `0x34` to LED do MCU (LED buduje taką ramkę w `0x9584`) [O].

## 3. Łącze fizyczne

| Połączenie | Peryferium | Parametry | Dowód |
|---|---|---|---|
| MCU - BLE | USART1 (0x40040000) | 115200 8N1, nadawanie przez PDMA kanał 3, odbiór w przerwaniu IRQ 24 | `0x7758` (inicjalizacja), `0xec82` (przerwanie), `0x806e`/`0x807e` (PDMA) [O] |
| MCU - LED | USART0 (0x40000000) | 115200 8N1, nadawanie przez PDMA kanał 1, odbiór w przerwaniu IRQ 23 | `0x77ea`, `0xebf2`, `0x8032`/`0x8042` [O] |

- Przypisanie USART1 do BLE i USART0 do LED wynika z kolejek: wysyłka do celu 5 kończy się zapisem do struktury PDMA `0x200011b4` i USART1, a do celu 4 do struktury `0x20000e4c` i USART0 [O]. Zgadza się to z portem społeczności QMK (`SD1` to BLE, `SD0` to LED) [I, źródło zewnętrzne].
- Odbiór: przerwanie czyta po 5 bajtów (flaga 0x20) albo tyle, ile jest w FIFO po przekroczeniu czasu (flaga 0x40), i wrzuca do wspólnej kolejki `0x20000468`. Obie linie (LED i BLE) zasilają tę samą kolejkę, którą czyta zadanie "AT process" [O].
- Piny: USART0 to PB0/PB1 (LED), USART1 to PA4/PA5 (BLE), zgodnie z konfiguracją AFIO (`03-zachowanie-mcu-bluetooth.md`, sekcja 7) [O].

## 4. Dyspozytor poleceń MCU

Zadanie "AT process" (funkcja `0x59e2`) pobiera ramki z kolejki. Jeśli wysoka połówka bajtu 2 to 3, ramka trafia do tablicy poleceń `0x20000494` (13 wpisów po 8 bajtów: kod `cmd` i wskaźnik funkcji, wyszukiwanie liniowe w `0x58b2`). W przeciwnym razie ramka jest przekazywana dalej do węzła docelowego (`0x8912`) [O].

Tablica leży w skompresowanych danych RW (`0x20000000`, rozpakowanie opisane w `05-metoda-i-dowody.md`):

| `cmd` | Funkcja | Rola (wniosek z kodu) |
|---|---|---|
| 0x01 | 0x5d88 | informacje o urządzeniu: wersje, identyfikator 16 B, czas pracy, bateria, limit snu [O] |
| 0x02 | 0x6256 | sterowanie: stan układów BLE i LED, restart do bootloadera [O] |
| 0x03 | 0x631e | odczyt zewnętrznej pamięci **SPI** (nie pamięci MCU) [O] |
| 0x10 | 0x5b56 | konfiguracja i mapa klawiszy w pamięci zewnętrznej [O] |
| 0x11 | brak | wpis pusty [O] |
| 0x12 | 0x62fc | włączenie strumieniowania zdarzeń klawiszy do komputera (flaga `0x20001351`) [O/I] |
| 0x20 | 0x6410 | konfiguracja podświetlenia (komunikacja z ObinsKit i LED) [O] |
| 0x21 | brak | wpis pusty [O] |
| 0x30 | 0x6034 | makra (zapis i odczyt w pamięci SPI od 0x10000) [O] |
| 0x40 | 0x6358 | sama instrukcja `bx lr`, czyli puste polecenie [O] |
| 0x50 | 0x5acc | akcelerometr LIS3DH (inicjalizacja, stop trybu kierownicy, odczyt `WHO_AM_I`) [O] |
| 0x60 | brak | wpis pusty, więc ustawienia myszy wirtualnej z ObinsKit nie działają w tej wersji [O] |
| 0x80 | 0x67e2 | `80 01` pomiar baterii jak `01 05`, `80 02` pusta funkcja [O] |

Pełne podpolecenia: `08-weryfikacja-i-nowe-ustalenia.md`, sekcja 3.2.

## 5. Polecenia MCU do BLE (węzeł 5)

| `cmd` `sub` | Typ | Dane | Gdzie i kiedy | Znaczenie |
|---|---|---|---|---|
| `02 01` | `0x10` | 1 B = `0x02` | `0x7f66`, z zadania "Daemon" (`0xf012`) cyklicznie, dopóki BLE nie zgłosi gotowości | `IapMode` z danymi 2, czyli polecenie przejścia w tryb aplikacji; zgadza się z ramką "wakeup" z QMK [O/I]. Aplikacja BLE ją ignoruje (reaguje tylko na nadawcę 1), więc skutek ma tylko wobec bootloadera BLE [O] |
| `01 03` | `0x12` | 1 B = `0x02` | `0x7f9c`, tuż po powyższym | zapytanie o wersję układu BLE; BLE odpowiada `01 03 0x25` [O] |
| `40 01` | `0x12` | numer gniazda (1 B) | `0xa42c`, po przytrzymaniu klawisza gniazda | rozgłaszanie w trybie parowania: interwał 20 ms, limit 60 s [O] |
| `40 04` | `0x12` | numer gniazda (1 B) | `0xa444`, po krótkim naciśnięciu klawisza gniazda | połączenie z zapamiętanym urządzeniem: rozgłaszanie 20 ms, limit 5 s, w połączeniu dopuszczony tylko bond równy slotowi [O] |
| `10 04` | `0x12` | 8 B | `0xe1b2` (tryb BLE) | raport klawiatury (6KRO) [O] |
| `10 08` | `0x12` | 4 B | `0xe2a4` (tryb BLE) | raport multimediów (consumer) [O] |
| `60 04` | `0x12` | 8 B | `0xe240` (tryb BLE) | mysz wirtualna; BLE zamienia to na raport HID ID 4 (input, 8 B), a nie ignoruje [O/I] |
| `11 01` | `0x12` | brak | `0x6dde`, z menedżera zasilania przy zdarzeniu wybudzenia | podtrzymanie aktywności: BLE uzbraja wybudzenie P0.1 i na 500 ms wstrzymuje sen [O/I] |
| `50 07` | `0x12` | 5 B | `0x8c44` (proces czujnika), gdy tryb kierownicy (`KC_FN_WHEEL`) działa, a wyjście nie jest USB | odpowiednik raportu gamepada dla BLE; w obrazie BLE `cmd 0x50` ma pusty handler, więc ten firmware go ignoruje [O] |
| `01 10` | zależnie od żądania | 16 B | `0x5d88`, odpowiedź na wersję zgłoszoną przez BLE | UDID z `0x2000114c`; BLE przyjmuje `01 10 <id> <32 B>` i dla id 2 zapisuje DIS Serial Number; fabryczny MCU wysyła 32 znaki bez bajtu id, więc BLE je odrzuca [O/I] |

Polecenia od komputera do BLE (węzeł 1 do 5) MCU przekazuje bez zmian (`0x8912`). W taki sposób przechodzi np. ramka "bootload" znana z QMK: `7b 10 51 10 03 00 00 7d 02 01 01` [I].

## 6. Polecenia BLE do MCU (węzeł 3)

Strona nadawcza w obrazie BLE: funkcja `1:F45A` buduje i wysyła ramkę (R1 to `cmd`, R2 to `sub`, R3 długość, R4:R5 wskaźnik danych) [O]. Bajt adresowy tych ramek to **`0x35`** (cel 3, nadawca 5), co potwierdzają wszystkie cztery miejsca wywołania [O].

| `cmd` `sub` | Dane | Obsługa w MCU | Znaczenie |
|---|---|---|---|
| `02 01` | stan (1 B) | `0x6256`: zapis do `0x200011a1` przez `0x7456` | stan układu BLE; wartość 2 oznacza gotowość [O] |
| `01 03` | 1 B = `0x25` (wysoka połówka to wersja główna, niska to poboczna) | `0x5e70`: wersja do `0x2000118b` (`0x727e`); jeśli składana wersja wynosi co najmniej 0x100, ustawia stan BLE na 2 i odsyła `01 10` | wersja układu BLE, ObinsKit pokazuje "2.05". BLE wysyła ją raz po inicjalizacji (`1:EF82`) oraz jako odpowiedź na zapytanie [O] |
| `20 07` | 1 B: bit 0 lub 1 | `0x6612`: wymaga nadawcy 5; zapisuje wartość w `0x2000119e` (`0x736a`) i przesyła `20 07` do LED | stan diody Caps Lock hosta; BLE wyciąga go z raportu wyjściowego HID (bit 1 w ID 1) [O/I] |
| `20 0c` | 1 B = 0 | `0x66d6`: MCU wysyła do LED `20 0c 00`, co wyłącza wskaźnik slotu BLE (poprawka: `0x66e2` to obsługa `20 0d`) | BLE wysyła to dwukrotnie, wyłącznie po wejściu w stan połączony; przy rozłączeniu nic nie wysyła [O] |
| ramki z nadawcą 2 (host przez BLE) | jak od hosta USB | te same procedury co dla węzła 1 | konfiguracja klawiatury przez BLE [O] |

Ramki od hosta po BLE przechodzą przez wirtualny raport HID o ID 2 (18 bajtów w obie strony). To zgadza się z fragmentacją po stronie MCU: dla celu 2 fragment ma 8 bajtów danych, a z 10-bajtowym nagłówkiem daje dokładnie 18 bajtów [I, silnie poparte].

Wniosek o zdarzeniach połączenia: układ BLE wysyła `20 0c 00` po wejściu w stan połączony, a MCU zamienia to na `20 0c 00` do LED, które gasi wskaźnik gniazda [O]. Wskaźnik włączają wciśnięcia klawiszy w MCU (`20 0b`), a wyłącza połączenie [O]. Wcześniejsza wersja tego akapitu przypisywała `20 0c` procedurze `0x66e2`, która obsługuje `20 0d`. Procedura `cmd 0x40` w MCU jest pusta [O].

## 7. Polecenia MCU do LED (węzeł 4) związane z Bluetoothem

| `cmd` `sub` | Dane | Gdzie | Znaczenie |
|---|---|---|---|
| `20 0b` | numer gniazda, 1 (rozgłaszanie) lub 0 (połączenie) | `0x7eac` z `0xa42c` i `0xa444` | podświetlenie wskaźnika gniazda BLE [I] |
| `20 03` | konfiguracja | `0x7ee8` | początkowa konfiguracja LED po zgłoszeniu gotowości [I] |
| `20 0f` | 1 B: procent baterii 0-100 | `0x94b2` | poziom baterii policzony z pomiaru ADC (patrz `03-zachowanie-mcu-bluetooth.md`, sekcja 7) [O/I] |
| `02 01` | 1 B = `0x02` | `0x7f1e` | ping do układu LED; typ ramki zmienia się przy każdym wysłaniu (0x12/0x10) [O] |
| `20 0c` | 2 B = `00 00` | `0x66d6` | wyłączenie wskaźnika gniazda po połączeniu BLE [O] |

LED wysyła przy starcie ramkę `01 03` z wersją (u16, `0x0221` = 2.33) i nadawcą 4 (`0x9584` w obrazie LED), a na ping `02 01 02` odpowiada `21 02 02 21` (stan 2 i wersja) [O]. Pozostałe polecenia MCU do LED: `08-weryfikacja-i-nowe-ustalenia.md`, sekcja 3.3.
