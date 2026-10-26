# Zachowanie procesora aplikacyjnego (MCU 2.36) w kontekście Bluetooth

Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma. Adresy dotyczą `key-c18-2.36.3.bin` przy bazie 0x4000.

## 1. Zadania systemu (RTOS)

Nazwy zadań są w obrazie jako napisy (0xf363-0xf49c). Tablica deskryptorów w rozpakowanych danych RW wiąże nazwę z funkcją [O]:

| Zadanie | Funkcja | Rola przy Bluetooth |
|---|---|---|
| AT process | 0x59e2 | odbiera ramki z kolejki UART, dyspozytor poleceń [O] |
| Uart process | 0xad2a | nadawanie ramek na USART0 (LED) i USART1 (BLE) przez PDMA [O] |
| Keyboard Daemon Process | 0xf012 | cykliczny handshake z układami BLE i LED [O] |
| Keyboard Tap Process | 0xa2b4 | rozróżnienie krótkiego i długiego naciśnięcia klawiszy gniazd BLE [O] |
| Power Manager Process | 0x705a | tryby zasilania, sen, wybudzanie [I] |
| Keyboard Matrix Scan Process | 0xb79a | skanowanie klawiatury [I] |
| Keyboard Misc Process | 0x93ec | zadania pomocnicze, m.in. pamięć zewnętrzna (SPI) [I] |
| Keyboard Macro Process | 0xac38 | makra [I] |
| I2c Device Process | 0x8c44 | akcelerometr LIS3DH (I2C1, adres 0x19, `WHO_AM_I` = 0x33): tryb kierownicy przez gamepad USB i dane dla efektu podświetlenia nr 26 [O] |
| Event timer | 0x8a5c | etimery Contiki [O] |

Zadania to procesy Contiki OS (protothready, `process_post`, etimery); tick systemowy wynosi 10 ms, więc wszystkie wartości liczników poniżej należy mnożyć przez 10 ms [I mocne: SysTick 48 MHz/8/100 i zgodność z instrukcją]. Szczegóły: `08-weryfikacja-i-nowe-ustalenia.md`, sekcja 3.1.

## 2. Inicjalizacja i handshake z układem BLE

1. Po uruchomieniu (`main` w `0xf248`) inicjalizowane są USART0 i USART1 (115200 8N1) oraz PDMA [O].
2. Zadanie "Daemon" (`0xf012`) uruchamia dwa liczniki. Gdy stan układu BLE (`0x200011a1`) jest różny od 2, wysyła do BLE parę poleceń: `02 01` (`0x7f66`) i `01 03` (`0x7f9c`). Pierwsze 5 prób co 50 ticków, potem co 500 [O], czyli co 0,5 s, a potem co 5 s (tick 10 ms) [I mocne].
3. Analogiczny licznik obsługuje układ LED: `02 01` do węzła 4 (`0x7f1e`) powtarza się, dopóki stan LED (`0x200011a0`) nie osiągnie 2. Dopiero wtedy MCU wysyła początkową konfigurację LED (`0x7ee8`, polecenie `20 03`) [O].
4. BLE zgłasza wersję poleceniem `01 03` (raz po własnej inicjalizacji i w odpowiedzi na żądanie `01 03 02`; ping `02 01 02` jest w aplikacji BLE ignorowany). MCU składa ją z połówek bajtu (główna i poboczna), zapisuje w `0x2000118b`, ustawia stan BLE na 2 (gotowy) i odsyła 16-bajtowy identyfikator (`01 10`) [O]. Wersja "2.05" pokazywana przez ObinsKit pochodzi z tego pola [I].
5. Odliczanie bezczynności menedżera zasilania (`0x6cfe`) działa tylko wtedy, gdy tryb magistrali to 0 (brak USB), stan LED (`0x200011a0`) i stan BLE (`0x200011a1`) są równe 2 oraz flaga konfiguracji `0x20001163` (odczyt `0x73e2`, zapis poleceniem `20 0d`) jest równa 1 [O]. Gotowość obu układów jest więc warunkiem wejścia w uśpienie [I].

Wartości stanu inne niż 2 nie są nigdzie porównywane w przeczytanym kodzie [O], więc ich znaczenie jest nieznane [N].

## 3. Wybór wyjścia: USB czy Bluetooth

Dwie zmienne decydują o torze raportów HID [O]:

| Zmienna | Adres | Źródło | Znaczenie |
|---|---|---|---|
| tryb magistrali | `0x20001182` | odczyt wejścia PC0 (`0x7ab4`, `0x7b3c`) | 0 brak USB, 2 podłączone USB [O]; PC0 jest wejściem bez podciągania z EXTI0 i debounce 30 ticków, a stan 1 uruchamia ponowne podłączenie USB, więc kod traktuje PC0 jako detekcję zasilania USB [O]; schemat C18 (`qmk_firmware/keyboards/annepro2/AnnePro2Schematics.PDF`) opisuje PC0 jako linię COL5 macierzy, a QMK nie używa PC0 w macierzy C18, więc konflikt ze schematem zostaje [N] |
| wybór wyjścia | `0x200011ad` | przerwanie na PC10 (`0x7bb4`) | 0 wyjście USB, 1 wyjście Bluetooth [O]; PC10 to przełącznik Bluetooth z tyłu klawiatury opisany w instrukcji; sprawdzone na C18 z opcją `AP2_BT_SWITCH` w QMK: Off daje USB, On Bluetooth [O] |

- Raport klawiatury (`0xe1b2`): gdy `wybór == 0` i `tryb == 2`, MCU wysyła do komputera 37 bajtów na endpoint 2 (USB, NKRO); w każdym innym przypadku buduje ramkę `10 04` z 8 bajtami i wysyła ją na USART1 do BLE [O].
- Raport multimediów (`0xe2a4`): USB endpoint 5 (5 bajtów) albo `10 08` z 4 bajtami do BLE [O].
- Po przełączeniu wyboru na USB (PC10 w stanie 1) MCU wysyła pusty raport (37 zer), żeby zwolnić klawisze [O].
- Źródło zmiennej `0x200011ad` to odczyt wejścia PC10 (`0x7bb4`) i jest to jedyne miejsce, które ją ustawia [O]. Instrukcja Anne Pro 2 każe przestawić przełącznik Bluetooth z tyłu klawiatury na „On” przy pracy bezprzewodowej i na „Off” przy USB, więc PC10 to ten przełącznik [I mocne, runda Ghidry]; potwierdził to test na C18 z opcją `AP2_BT_SWITCH` w QMK (Off: USB, On: Bluetooth) [O]. Zastrzeżenie dotyczące obudowy układu ze schematu C18 zostaje [N]. Opcja `AP2_BT_SWITCH` w porcie QMK korzysta z tego wejścia.
- Raporty przez BLE mają stały rozmiar 8 bajtów, więc nie przenoszą pełnego NKRO (w USB 37 bajtów) [O].

## 4. Gniazda parowania

Klawisze o kodach 200-203 (0xC8-0xCB) obsługują cztery gniazda urządzeń Bluetooth [O]:

- naciśnięcie (`0xa188`) uruchamia licznik 500 jednostek dla gniazda `kod - 200`;
- zwolnienie przed upływem licznika (`0xa1b2`) wywołuje `0xa444`: ramka `40 04 <gniazdo>` do BLE (połącz) i `20 0b <gniazdo> 0` do LED [O];
- licznik, który dobiegnie do zera przy wciśniętym klawiszu (`0xa3d4`-`0xa3e4`), wywołuje `0xa42c`: ramka `40 01 <gniazdo>` do BLE (rozgłaszaj, paruj) i `20 0b <gniazdo> 1` do LED [O].

To odpowiada zachowaniu opisywanemu w instrukcjach: krótkie naciśnięcie łączy z zapamiętanym urządzeniem, długie rozpoczyna parowanie [I]. Licznik 500 jest zmniejszany o 10 co 10 ticków (100 ms), więc parowanie startuje po **5 s** przytrzymania; instrukcja podaje „FN2 + 1 (5 Seconds)” [O, poprawka wcześniejszego założenia „500 ms”]. Port QMK z `AP2_BLE_SLOT_TAP_HOLD` używa 500 ms, czyli celowo krócej.

Polecenia rozłączenia i usunięcia parowania (`40 02`, `40 05` w portach społeczności) nie są w MCU wywoływane z kodu klawiszy [O, w zakresie przeczytanego kodu]. Mogą przechodzić z ObinsKit jako ramki przekazywane [I].

## 5. Zasilanie i sen

- Menedżer zasilania (`0x705a`) obsługuje zdarzenia z kolejki. Licznik bezczynności (`0x6cfe`) rośnie tylko bez USB, przy fladze `0x20001163` = 1 (`20 0d`, w ObinsKit `writeLightHoldOn`) i przy gotowych LED i BLE. Po 90 tickach bezczynności `0x836c` uzbraja RTC limitem snu w sekundach (`01 0f`, 10-3600), a dopasowanie RTC wyzwala uśpienie (DeepSleep, `0x6e74`) [O]. Poprawka: wcześniej opisałem `0x836c` jako przyciemnianie podświetlenia.
- Wybudzenie klawiszem wywołuje `0x6dde`, czyli ramkę `11 01` do BLE [O]. Układ BLE traktuje ją jako podtrzymanie aktywności: uzbraja wybudzenie P0.1 i na 500 ms blokuje sen [O/I].
- Pin PC3 jest ustawiany i zerowany przez menedżera zasilania zależnie od trybu (`0x70f0`, `0x7102`, `0x7198`, `0x71b6`) [O]. Schemat C18 podpisuje PC3 jako ROW5 macierzy klawiszy [O], więc to najpewniej sterowanie macierzą przy usypianiu i wybudzaniu, a nie zasilanie układu BLE [I].
- Schemat C18 podpisuje dwie linie do układu LED: PB7 (pin 45) to `LED_RESET`, PB8 (pin 46) to `LED_WKUP`; PB6 (pin 44) jest niepodłączony. Schemat nie zawiera układu Bluetooth ani jego linii [O].
- Inicjalizacja (`0x799c`) ustawia w stan wysoki PB6 (maska 0x40, `0x79e0`). Linią CS pamięci SPI na C18 jest PA3 (sterownik SPI w MCU i `c18/config.h` w QMK), więc rola PB6 na C18 jest nieustalona [N]; wcześniejszy wniosek o CS pochodził z portu C15. PB7 i PB8 są ustawione jako I2C1 (funkcja 7) i obsługują akcelerometr LIS3DH [O]. Schemat C18 opisuje te linie jako `LED_RESET`/`LED_WKUP`; czy czujnik jest obsadzony na tych płytkach, nie wiadomo [N]. Bez czujnika sonda `WHO_AM_I` wyłącza jego funkcje.

## 6. Czego brakuje lub co jest niejasne w MCU

- Połączenie jest widoczne dla wskaźników: `20 0c` od BLE powoduje wysłanie do LED `20 0c 00`, które gasi wskaźnik gniazda (`0x66d6`) [O, poprawka]. Rozłączenie nie daje żadnej ramki, a procedura `cmd 0x40` jest pusta [O].
- Caps Lock przez BLE jest obsłużony: BLE zgłasza `20 07`, MCU zapisuje wartość i przekazuje ją do LED (`0x6612`) [O]. Port QMK oznacza ten element jako nieznany, więc fabryczny firmware ma tu więcej niż port społeczności [I].
- Piny USART są ustalone (sekcja 7). Nie ustaliłem, czy jest osobna linia zasilania lub resetu układu BLE. Po stronie BLE linia P0.1 wybudza układ (patrz `04-uklad-ble-8051.md`), ale nie wiem, który pin MCU ją napędza [N].
- Nie znalazłem w MCU miejsca wysyłającego `01 05` (napięcie baterii) do BLE, które obsługuje obraz BLE [N].

## 7. Piny i zasilanie (uzupełnienie)

Konfiguracja AFIO w `0xb1b8` (funkcja `0xe0c0`: port, maska pinów, numer funkcji; numery z nagłówka HT32: 1 GPIO, 2 ADC, 5 SPI, 6 USART, 7 I2C) [O]:

| Piny | Funkcja | Zastosowanie |
|---|---|---|
| PB0, PB1 | USART (6) | USART0, łącze do układu LED; zgodne z `LINE_UART_TX B0`, `LINE_UART_RX B1` w QMK i z etykietą `UART_LED` na schemacie [O] |
| PA4, PA5 | USART (6) | USART1, łącze do układu BLE; zgodne z `LINE_BT_UART_TX A4`, `LINE_BT_UART_RX A5` w QMK [O] |
| PA0, PA1, PA2 | SPI (5) | zewnętrzna pamięć SPI (SCK, MOSI, MISO; zgodne z konfiguracją QMK) [O] |
| PA3 | GPIO | CS pamięci SPI (zgodne z QMK `c18/config.h`) [O] |
| PA7 | ADC (2) | kanał 7 przetwornika, pomiar baterii [I] |
| PB7, PB8 | I2C (7) | I2C1, akcelerometr LIS3DH [O] |
| PC0, PC10 | GPIO, EXTI | detekcja USB i przełącznik Bluetooth (sekcja 3) [O/I] |

Macierz klawiszy (wiersze PB5, PB4, PB3, PB2, PD1; 14 kolumn) zgadza się z `c18/keyboard.json` w QMK; pełna lista pinów: `08-weryfikacja-i-nowe-ustalenia.md`, sekcja 3.6.

Pomiar baterii [O/I]:
- Przerwanie ADC (`0xed0c`) kopiuje próbki 12-bitowe, a `0x959a` zbiera 8 próbek i liczy średnią uciętą ((suma - min - max) / 6).
- `0x94b2` przelicza wynik na napięcie: `U = ADC * 3,1 * 2,02 / 4094` (stałe w puli literałów, działania mnożenie, mnożenie, dzielenie rozpoznane z kontekstu wywołań procedur zmiennoprzecinkowych) [I].
- Progi napięcia i wynik w procentach: od 4,1 V 100; od 4,0 V 90; od 3,9 V 80; od 3,8 V 70; od 3,75 V 60; od 3,7 V 50; od 3,65 V 40; od 3,6 V 30; od 3,5 V 20; od 3,4 V 10; poniżej 0 [O].
- Procent trafia do układu LED jako `20 0f <procent>` (`0x94b2`) [O]. Do układu BLE MCU go w przeczytanym kodzie nie wysyła [N].
