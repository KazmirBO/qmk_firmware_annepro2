# MCU 2.36 (procesor aplikacyjny)

Obraz `key-c18-2.36.3.bin`, adresy przy bazie 0x4000. Znaczniki i konwencje: `README.md` §2. Polecenia przyjmowane przez MCU: `02-protokol.md` §5.1.

## 1. Procesy Contiki

Nazwy procesów leżą pod `0xf364`-`0xf49d`, a struktury procesów w rozpakowanych danych RW wiążą nazwę z funkcją [O]:

| Proces | Funkcja | Struktura | Rola [O] |
|---|---|---|---|
| AT process | `0x59e2` | `0x20000474` | odbiór ramek z kolejki UART, dyspozytor poleceń i przekazywanie |
| Uart process | `0xad2a` | `0x2000063c` | nadawanie z kolejek porcjami do 0x40 B przez PDMA na USART0 i USART1 |
| Keyboard Daemon Process | `0xf012` | `0x20000500` | handshake z LED i BLE, jednorazowy start pomiaru ADC |
| Keyboard Tap Process | `0xa2b4` | `0x200005f0` | liczniki klawiszy slotów BLE i klawiszy tap-key |
| Power Manager Process | `0x705a` | `0x200005ac` | sen i wybudzanie (zdarzenia [2]-[5]) |
| Keyboard Matrix Scan Process | `0xb79a` | `0x20000540` | skanowanie macierzy 5 × 14 (`0x7fd6`, `0x96cc`) |
| Keyboard Misc Process | `0x93ec` | `0x2000057c` | odszumianie PC0, odczyt PC10, procent baterii, zapis konfiguracji, godzinny licznik czasu pracy |
| Keyboard Macro Process | `0xac38` | `0x20000564` | odtwarzanie makr |
| I2c Device Process | `0x8c44` | `0x200002b4` | akcelerometr LIS3DH (§11) |
| Event timer | `0x8a5c` | `0x20000004` | etimery |

Funkcje Contiki: `process_alloc_event` `0x6818` (numery od 0x8A), `process_start` `0x6824`, `process_post` `0x69dc` (kolejka 64 zdarzeń), `process_run` `0x69b8`, `process_poll` `0x6a6c`, `etimer_set` `0x8b58`, `etimer_stop` `0x8b78`. Zdarzenia systemowe: 0x81 INIT, 0x82 POLL, 0x83 EXIT, 0x88 TIMER [O].

## 2. Czas

- SysTick: 48 MHz, źródło STCLK, przeładowanie 60000; tick 10 ms przy STCLK = `CK_AHB`/8 (`01-architektura.md` §4) [I mocne].
- Procedura SysTick (`0xcaee`) liczy: 360 000 ticków (1 h) dla licznika czasu pracy (`01 02`, dziennik w SPI) i 60 000 ticków (10 min) dla pomiaru baterii; gdy nie ma zdarzeń, woła licznik bezczynności `0x6cfe` (§7) [O].

## 3. Start i handshake

1. `main` (`0xf248`) inicjalizuje USART0 (`0x77ea`), USART1 (`0x7758`) i PDMA (`0x7658`), uruchamia procesy i wchodzi w pętlę `process_run` [O].
2. Proces Daemon zaczyna od trzech zdarzeń TIMER naraz: etimer LED (`0x20000510`), etimer BLE (`0x20000520`) i jednorazowy start ADC (`0x20000530`), więc pierwsze pingi wychodzą od razu [O].
3. BLE: dopóki stan BLE (`0x200011a1`) nie wynosi 2, MCU wysyła `02 01 02` (`0x7f66`) i `01 03 02` (`0x7f9c`): 5 razy co 50 ticków (0,5 s), potem co 500 ticków (5 s), bez limitu [O]. Aplikacja BLE ignoruje `02 01 02` (to polecenie dla bootloadera) i odpowiada na `01 03` [O].
4. LED: dopóki stan LED (`0x200011a0`) nie wynosi 2, MCU wysyła `02 01 02` (`0x7f1e`) według tego samego harmonogramu, zmieniając za każdym razem typ ramki (`0x12`/`0x10`). LED odpowiada `21 02 02 21` (stan 2 i wersja 0x0221) [O]. Po stanie 2 MCU wysyła do LED `20 09`, `20 1b`, `20 1f` i `20 15` (`0x635a`), a gdy podświetlenie jest włączone (`0x20001166` różne od 0), także `20 03` z bieżącym efektem użytkownika (`0x7ee8`, dane efektu z SPI 0x20000 + indeks·0x1000) [O].
5. Odpowiedź BLE `01 03 25`: MCU składa wersję z połówek bajtu (`(b >> 4) << 8 | (b & 0xF)`, czyli 0x0205) i zapisuje ją pod `0x2000118b`; przy wersji co najmniej 0x100 ustawia stan BLE na 2; niezależnie od wersji odsyła `01 10` z 32 znakami hex UDID (`0x5edc`-`0x5f28`) [O]. Układ BLE odrzuca tę ramkę, bo pierwszy znak traktuje jako `id` większe od 8 (`05-ble.md` §4.1), więc numer seryjny DIS zostaje domyślny [O].
6. Stan BLE można też ustawić ramką `02 01 <stan>` od węzła 5, ale aplikacja BLE 2.05 jej nie wysyła [O]. Wartości stanu inne niż 2 nie są w kodzie porównywane [O].

## 4. Wybór wyjścia: USB albo BLE

| Zmienna | Adres | Źródło [O] |
|---|---|---|
| tryb magistrali | `0x20001182` | PC0 (wejście bez podciągania, EXTI0): `0x7b3c` ustawia 0 przy PC0 = 0 (i odłącza stos USB) oraz 2 przy PC0 = 1 (i podłącza USB ponownie); po przerwaniu odczyt następuje po odszumieniu 30 ticków w procesie Misc; `0x7b3c` wołają też `main` i Power Manager, a `0x7ab4` (drugi odczyt PC0) Power Manager |
| wybór wyjścia | `0x200011ad` | PC10 (wejście z podciąganiem w dół, EXTI10): `0x7bb4` ustawia 0 (USB) przy PC10 = 1 i 1 (BLE) przy PC10 = 0; wołana przy starcie, po każdym zboczu EXTI10 (przez proces Misc) i w zdarzeniach Power Managera; to jedyne miejsce zapisu tej zmiennej |

- PC10 to przełącznik Bluetooth z tyłu klawiatury: instrukcja każe ustawić go na On przy pracy bezprzewodowej i na Off przy USB [zewn.], a test na C18 potwierdził kierunek (Off daje USB, `08-testy-na-sprzecie.md`) [O]. Oryginał nie odszumia PC10 i ustawia wyjście z poziomu pinu przy każdym odczycie [O].
- PC0 to w kodzie detekcja zasilania USB [O]. Schemat w sieci podpisuje PC0 jako kolumnę macierzy, ale dotyczy innego wariantu płytki (`01-architektura.md` §7).
- Raport klawiatury (`0xe1b2`): przy wyborze 0 i trybie 2 MCU wysyła 37 B (NKRO) na EP2, o ile stos USB jest skonfigurowany (w przeciwnym razie nic nie wysyła i nie przechodzi na BLE); w każdym innym przypadku wysyła do BLE `10 04` z 8 B (6KRO) [O]. Przez BLE nie ma więc NKRO.
- Raport multimediów (`0xe2a4`): EP5 z 5 B albo `10 08` z 4 B; raport myszy wirtualnej (`0xe240`) do BLE jako `60 04` z 8 B, którą BLE ignoruje [O].
- Przy każdym odczycie PC10 = 1 i trybie 2 MCU wysyła pusty raport 37 B, żeby zwolnić klawisze [O].

## 5. Klawisze slotów BLE

- Kody 200-203 (0xC8-0xCB, w ObinsKit `KC_BLE_WL_1`-`4`) to sloty 0-3 [O].
- Naciśnięcie (`0xa188`) ustawia licznik slotu na 500. Proces Tap (`0xa2b4`) co 10 ticków odejmuje 10; gdy licznik spadnie poniżej 1 przy wciśniętym klawiszu, `0xa42c` wysyła `40 01 <slot>` do BLE (parowanie) i `20 0b <slot> 1` do LED [O]. Parowanie startuje więc po 5 s przytrzymania; instrukcja podaje „FN2 + 1 (5 Seconds) = Turn on Bluetooth broadcast on position 1” [zewn.].
- Zwolnienie przed upływem licznika (`0xa1b2`) wywołuje `0xa444`: `40 04 <slot>` (połącz z zapamiętanym hostem) i `20 0b <slot> 0` do LED [O].
- Połączenie: `20 0c` od BLE trafia do `0x66d6`, które wysyła do LED `20 0c 00 00` i gasi wskaźnik slotu [O]. Rozłączenie nie daje żadnej ramki (`05-ble.md` §5).
- `40 02` (rozłącz) i `40 05` (usuń bondy) nie są wysyłane z klawiszy; procedura `cmd 0x40` w MCU jest pusta [O]. `40 05` wysyła ObinsKit (`deleteBleHost`), a MCU przekazuje je do BLE.

## 6. Kody klawiszy

Nazwy z ObinsKit 1.2.11, działanie w MCU 2.36 [O]:

| Kody | ObinsKit | Działanie w MCU 2.36 |
|---|---|---|
| 0x04-0xA4 | klawisze HID (nazwy do 0xA1 `KC_EXSEL`) | raport klawiatury |
| 0xA5-0xA7 | `KC_SYSTEM_POWER`, `KC_SYSTEM_SLEEP`, `KC_SYSTEM_WAKE` | brak działania: naciśnięcie jest pomijane, zwolnienie woła pustą funkcję; deskryptory USB nie mają raportu System Control |
| 0xA8-0xB5 | multimedia, jasność ekranu, poczta, kalkulator | bit w słowie raportu consumer; deskryptor USB definiuje 8 usages, a ramka BLE `10 08` przenosi 1 bajt |
| 0xB6-0xB9 | `KC_MY_COMPUTER`, `KC_WWW_SEARCH`, `KC_WWW_HOME`, `KC_WWW_BACK` | kółko i przesuw myszy wirtualnej (rozbieżność z nazwami ObinsKit) |
| 0xBA-0xBD | `KC_WWW_FORWARD`, `KC_WWW_STOP`, `KC_WWW_REFRESH`, `KC_WWW_FAVORITES` | wyzwalanie makr |
| 0xBF | `KC_BOOTLOADER` | brak obsługi |
| 0xC0-0xC6 | `KC_FN0`-`KC_FN6` | warstwy |
| 0xC8-0xCB | `KC_BLE_WL_1`-`4` | sloty BLE (§5) |
| 0xCC | `KC_BLE_WL_ON` | brak obsługi |
| 0xCF | `KC_FN_WHEEL`, w lokalizacji „Game wheel” | tryb kierownicy (§11) |
| 0xD0, 0xD1 | `KC_FN16`, `KC_FN17` | gamepad: osie X i Y rosną od 0x32 do 0xFF podczas trzymania (pedały) |
| 0xD2-0xDF | `KC_FN18`-`KC_FN31` | gamepad: przyciski 1-14 |
| 0xF0 | `KC_BL_TOGGLE` | `20 01 01` do LED, przy trybie 0 następny efekt (`20 05`) |
| 0xF1 | `KC_BL_COLOR` | następny efekt (`20 05` w lokalnej kolejce, potem `20 03`) |
| 0xF2, 0xF5 | `KC_BL_LEVEL`, `KC_BL_RATE` | brak obsługi |
| 0xF3, 0xF4 | `KC_BL_INCREASE`, `KC_BL_DECREASE` | jasność (`20 06`) |
| 0xF6 | `KC_BL_BATT` | wskaźnik baterii (§8) |
| 0xF7 | `KC_QUICKNUM` | brak obsługi |
| 0xF8-0xFE | `KC_MS_BTN1`-`3`, `KC_MS_XY_*` | przyciski i ruch myszy wirtualnej |

### 6.1. Mapa klawiszy i mapa fabryczna

- Deskryptor mapy w danych RW (`0x2000017c`, wskaźnik ustawia `0x92b4`): liczba klawiszy 61 (0x3d), typ układu 0, bufor map w RAM `0x20000bec`, tablica pozycji `0xf2a4` [O]. Bufor ma 5 warstw po 61 B [O].
- Tablica `0xf2a4` (5 × 14 B) przypisuje pozycji macierzy indeks klawisza 0-60. Pozycje bez klawisza to wiersz 2 kolumna 13, wiersz 3 kolumny 1 i 13 oraz wiersz 4 kolumny 1, 4, 5, 7, 8 i 13, dokładnie jak w `LAYOUT_60_ansi` portu QMK (`info.json`) [O]. Dolny rząd: LCtrl 0, LGui 2, LAlt 3, spacja 6, RAlt 9, FN1 10, FN2 11, RCtrl 12 [O].
- Wybór mapy (`0x80aa`, wołane m.in. przez `10 06`): gdy bajt `0x2000115c` (`writeKbdLayout`, `10 06`) jest różny od 0, MCU czyta 5 warstw z SPI (`0x7d40`, §9) i zamienia bajty 0xFF na 0; gdy wynosi 0, kopiuje mapę fabryczną z flash (`0xcb78`) [O]. `10 06` przełącza więc między mapą użytkownika z ObinsKit a mapą fabryczną.
- Mapa fabryczna ANSI leży pod `0xcbac` (5 × 61 B), obok tablica ISO pod `0xcce0` (5 × 62 B, z Enter w rzędzie Q, kodami 0x32 i 0x64). `0xcb78` wybiera ISO tylko przy typie układu 1, a w kodzie nie ma zapisu do tego bajtu deskryptora, więc MCU 2.36 zawsze używa mapy ANSI [O co do braku zapisu]. W tablicy ISO warstwa 4 jest pusta i brakuje myszy na IJKL [O].
- Warstwy mapy ANSI (kody bez działania pominięte; nazwy kodów z §6) [O]:

| Warstwa | Zawartość |
|---|---|
| 0 | standardowy układ ANSI 60 %; dolny rząd: LCtrl, LGui, LAlt, spacja, RAlt, FN1, FN2, RCtrl |
| 1 (FN1) | rząd cyfr: `` ` ``, F1-F12; W, A, S, D: strzałki; P: PrtSc; `[`: Home; `]`: End; `;`: PgUp; `'`: PgDn; `.`: Ins; `/`: Del |
| 2 (FN2) | 1-4: sloty BLE (0xC8-0xCB); 5: tryb kierownicy (0xCF); 8: wskaźnik baterii; 9: następny efekt; 0: podświetlenie wł./wył.; `-` i `=`: jasność w dół i w górę; I, J, K, L: ruch myszy wirtualnej (0xFB, 0xFD, 0xFC, 0xFE); strzałki, PrtSc, Home, End, PgUp, PgDn, Ins i Del jak w warstwie 1 |
| 3 | RShift: strzałka w górę; FN1, FN2, RCtrl: strzałki w lewo, w dół, w prawo |
| 4 | przyciski gamepada: Tab 0xD0 i `\` 0xD1 (osie), Q 0xD2, W 0xD3, A 0xD4, S 0xD5, Z 0xD6, X 0xD7, `]` 0xD8, `[` 0xD9, `'` 0xDA, `;` 0xDB, `/` 0xDC, `.` 0xDD |

Warstw 3 i 4 nie włącza klawisz warstwy; obsługa klawisza (`0x9788`) czyta je obok bieżącej warstwy (`0x92e6`: warstwa, pozycja; kod 0 jest zwracany jako 1, czyli brak) [O]:

- Warstwa 4 jest czytana tylko wtedy, gdy bajt trybu `0x20001181` wynosi 3 (`0x97a8`). Ustawia go na 3 włączenie trybu kierownicy (`0xe472`), a na 0 jego wyłączenie (`0xe4a8`-`0xe4b2`) i start (`0x79e6`). W trybie kierownicy klawisz, który ma kod w warstwie 4, wysyła przy naciśnięciu ten kod zamiast zwykłego (`0x98e8`), a pozostałe klawisze działają normalnie [O]. To warstwa pedałów i przycisków gamepada z §11.
- Warstwa 3 jest czytana poza trybem kierownicy oraz dla klawiszy bez kodu w warstwie 4 (`0x97bc`-`0x97d0`). Klawisz z kodem w warstwie 3 staje się klawiszem tap-key tylko wtedy, gdy bajt `0x2000116b` (`10 05`, `writeKbdTap`) jest różny od 0 (`0x983e`, `0x80c6`). Naciśnięcie uzbraja licznik klawisza (`0xa0b0`) wartością czasu tap-key (`0x2000116c`, `10 13`, zakres 5-200, poza nim 6; `0xa27e`). Zwolnienie przed końcem licznika wysyła kod z warstwy 3, czyli strzałkę (`0xa0ec`, `0x9e06`), a gdy licznik dojdzie do zera przy wciśniętym klawiszu, proces Tap naciska zwykły kod klawisza, np. RShift albo FN1 (`0xa2b4`, `0x9d34`), który późniejsze zwolnienie zwalnia razem z kodami pozostałych warstw (`0x9d90`) [O]. Przy trybie tap-key 2 (`0x2000116d`, `10 11`) i więcej niż jednym bicie w tablicy stanu macierzy (`0xaf64`), czyli przy drugim wciśniętym klawiszu [I], kod z warstwy 3 jest pomijany, a oczekujące tapnięcia zamieniają się w przytrzymania (`0x97d6`, `0xa1f2`) [O]. Liczniki klawiszy zmniejsza o 1 proces Tap (`0xa2b4`) przy każdym zdarzeniu swojego etimera, ustawianego na 1 tick (`0xa37a`), oraz przy odpytaniu po naciśnięciu, więc jednostką jest tick systemowy (10 ms, §2): czas tap-key 5-200 to 50 ms do 2 s, domyślne 6 to 60 ms, a kod tapnięcia jest trzymany 5 ticków (`0xa0ec`) [O].

## 7. Zasilanie i sen

- Licznik bezczynności (`0x6cfe`, wołany z SysTick, gdy nie ma zdarzeń) rośnie tylko, gdy jednocześnie: nie ma USB (tryb 0), flaga `0x20001163` = 1 (ustawiana `20 0d`, w ObinsKit przełącznik „Auto Sleep”, `writeLightHoldOn`), LED i BLE są w stanie 2, wskaźnik baterii jest wyłączony i żaden proces nie zgłasza aktywności. W przeciwnym razie licznik jest zerowany [O].
- Przy 90 tickach `0x836c` uzbraja RTC (zegar LSE, preskaler 2^15) limitem snu w sekundach (`01 0f`, 10-3600, wartość spoza zakresu daje 10) [O].
- Powyżej 100 ticków (około 1 s) zdarzenie [3] wprowadza MCU w DeepSleep (`0x6e74`: SLEEPDEEP i WFI) [O]. Przed snem PC3 = 1 tylko wtedy, gdy podświetlenie, Caps Lock i wskaźnik baterii są wyłączone [O].
- Wybudzenie (`0xe988`): linia EXTI (wiersze macierzy, PC0, PC10, PA6) daje zwykłe wybudzenie i powrót do pracy. Dopasowanie RTC, czyli upływ limitu snu, publikuje zdarzenie [2]: MCU wysyła do BLE `11 01` (`0x6dde`, jedyne wywołanie z `0x71c2`) i ustawia tryb długiego snu (`0x200011a7` = 1) [O].
- W trybie długiego snu kolejne uśpienie nie uzbraja RTC, ustawia PC3 = 1 i przełącza PA4 (TX do BLE) na GPIO w stanie wysokim (`0xb5de`). Wybudzenie klawiszem ustawia PA4 nisko, przywraca USART i kasuje tryb (`0xb616`) [O]. Co to robi z układem BLE, nie wiadomo [N].
- Przerwanie LVD (`0xee78`, `0x83d2`) ustawia PC3 = 1, gasi podświetlenie i zapisuje konfigurację [O].
- Rola PC3: kod nie wiąże go z macierzą. Układ LED przy stanie wysokim na swoim PB12 wyłącza matrycę i zasypia (`04-led.md` §8), a MCU ustawia PC3 = 1 dokładnie wtedy, gdy LED nie ma nic do pokazania, więc PC3 to najpewniej linia uśpienia układu LED [I; potwierdzi schemat C18 albo pomiar].

## 8. Bateria

- Pomiar: ADC kanał 7 na PA7 (funkcja AFIO 2), zegar ADC z dzielnikiem APBCFGR.ADCDIV = 6 (`HCLK/64` [I]), wyzwalanie programowe, 12 bitów; rejestry `0x40010000` + 0x000, 0x004, 0x030, 0x070, 0x074, 0x080, 0x088, 0x08C [O].
- Przerwanie ADC (`0xed0c`) kopiuje próbki, `0x959a` zbiera 8 próbek i liczy średnią bez najmniejszej i największej: (suma - min - max) / 6, zapisaną pod `0x2000118d` [O].
- Pomiar startuje przy starcie, co 60 000 ticków (10 min), po `KC_BL_BATT`, po `01 05` i po `80 01` [O].
- `0x94b2` przelicza wynik na napięcie `U = ADC * 3,1 * 2,02 / 4095` (arytmetyka double, 2,02 jako float; mnożenie `0xd950` i dzielenie `0xdad4` rozpoznane po kontekście, §11 [I]) i na procent progami `U >= próg`: 4,1 V 100; 4,0 V 90; 3,9 V 80; 3,8 V 70; 3,75 V 60; 3,7 V 50; 3,65 V 40; 3,6 V 30; 3,5 V 20; 3,4 V 10; poniżej 0 (schodki, bez interpolacji) [O].
- Procent idzie do LED (`20 0f <procent>`) tylko po `KC_BL_BATT` (flaga `0x2000119a`). Ponowne `KC_BL_BATT` przy włączonym wskaźniku wysyła `20 0f 00` i go gasi [O].
- Do BLE MCU nie wysyła ani pomiaru (`01 05`), ani procentu [O]. Układ BLE startuje z poziomem 100 % i bez `01 05` go nie zmienia, więc host połączony z oryginałem widzi stałe 100 % [I, `05-ble.md` §9].

## 9. Pamięć SPI NOR

- SPI1 z CS na PA3; identyfikacja poleceniem `0x9F` (`0xdc1c`) [O].
- Tablica identyfikatorów JEDEC (`0x2000064c`) ma 15 wpisów (Winbond, Macronix, SST, GigaDevice, Boya), z czego 14 różnych (0x684015 dwa razy) [O].
- Mapa: konfiguracja 0x0000 (`0x7dba` zapisuje też sektor 0x4000), mapy klawiszy 0x1000-0x13FF (0x100 na warstwę, warstwy 0-3; `10 01` zapisuje tylko je) i 0x2000 (warstwa 4, czytana w `0x7d40`, ale bez zapisu w kodzie) [O], dziennik czasu pracy 0xA000 i 0xB000, makra od 0x10000 (0x1000 na makro), efekty użytkownika od 0x20000 (0x1000 na efekt, `0x75a8`) [O].
- Bez rozpoznanej pamięci flaga `0x200011ae` = 0, a zapisy konfiguracji, map i makr są odrzucane [O].

## 10. USB

- Deskryptor urządzenia w danych RW (`0x200000a0`): VID 0x04D9, PID 0xA293, bcdDevice 0x0100. Gdy bajt +3 rekordu urządzenia wynosi 4, PID zmienia się na 0xA297 (`0xc0d2`-`0xc0e0`) [O].
- Napisy: „OBINS” i „OBINS\nAnnePro2” (ze znakiem nowej linii, UTF-16 pod `0xf2ec`-`0xf31a`); numer seryjny to 32 znaki hex UDID wpisywane do `0x200000ce` [O].
- Konfiguracja: remote wakeup, 500 mA, cztery interfejsy HID [O]:
  - IF0: klawiatura boot z NKRO (37 B, EP 0x82, co 1 ms), diody na EP 0x01;
  - IF1: kanał producenta 64 B (EP 0x03 i 0x84) dla ObinsKit;
  - IF2: consumer (ID 1), klawiatura 6KRO (ID 2), mysz (ID 3: osie 16-bitowe, kółko, przesuw), EP 0x85;
  - IF3: gamepad (Rx, Ry, Rz, X, Y, 16 przycisków), EP 0x86.

## 11. Akcelerometr, tryb kierownicy i efekt poziomicy

- Czujnik: LIS3DH na I2C1 (PB7/PB8), adres 0x19; `WHO_AM_I` (rejestr 0x0F) musi dać 0x33 albo 0x19, inaczej flaga obecności `0x200011af` = 0 i funkcje czujnika są wyłączone [O]. Czy czujnik jest obsadzony na płytkach C18, nie wiadomo [N].
- Inicjalizacja: ODR 100 Hz, ±2 g, osie XYZ, bit HR. Opcjonalna konfiguracja wykrywania stuknięć (0x38 = 0x2A, 0x3B = 120, 0x3C = 200), ale wyniku stuknięć nic nie odczytuje [O].
- Tryb kierownicy (`KC_FN_WHEEL`, „Game wheel”): włącza się tylko przy wyjściu USB i podłączonym USB; LED dostaje `20 12 01` (przy wyłączeniu `20 12 00`). Co tick proces liczy kąt przechyłu i wpisuje go jako oś Ry raportu gamepada (IF3, EP 0x86). Klawisze 0xD0-0xDF służą jako pedały i przyciski [O].
- Kąty (`0x8d40`-`0x8dee`): trzy pary osi (starsze 12 bitów) zamienione na double (`0xd80c`) trafiają do `0xd820`, wynik jest mnożony przez 180,0 (`0xd950`) i dzielony przez stałą 3,1416 (double, `0x8eec`; `0xdad4`), a potem zamieniany na liczbę całkowitą (`0xdb98`) [O]. `0xd820` dzieli argumenty (`0xdad4`) i woła `0xe560`, które korzysta ze stałych π/4, π/2 i π pod `0xe72c`-`0xe7bc` i wielokrotnie z `0xd950` i `0xe7c8` w obliczaniu wielomianu. Stąd rozpoznanie: `0xd820` to `atan2`, `0xe560` to `atan`, `0xd950` mnożenie, `0xdad4` dzielenie double [I mocne; nazw biblioteki Keil nie porównywano].
- Poza USB proces co 2 ticki wysyła do BLE `50 07` z 5 B spod `0x200002c4`, którą BLE 2.05 ignoruje. Pod `0x200002c4`-`0x200002c7` nic w kodzie nie pisze (dane RW mają tam zera), a piąty bajt to pierwszy bajt bufora odbiorczego procesu AT (`0x200002c8`, po odebraniu ramki 0x7B). Ramka niesie więc zawsze `00 00 00 00` i jeden bajt z innego bufora, a nie dane gamepada [O]. Kierownica działa tylko po USB [O].
- Efekt poziomicy (nr 26): gdy LED zgłosi tryb 0x1a, MCU wysyła `20 16 <x> <y> <z>` (starsze bajty osi). Cyklicznie, co 10 ticków, robi to tylko wtedy, gdy zapamiętany tryb (`0x20001164`) też wynosi 0x1a; zmiana efektu zlecona z komputera (`20 03 ff 1a`) tego bajtu nie ustawia, więc daje jedną ramkę [O]. LED zamienia oś Y na kolumnę 0-13 (`04-led.md` §5) [O]. ObinsKit 1.2.11 nie ma tego efektu na liście.

## 12. Rekord urządzenia i UDID

- Flash `0x3E00` (kopia 0x18 B pod `0x20001144`): domyślnie `02 01 01 05`, u16 0x3311, u16 0xA293, a od +8 16 B UDID [O].
- Bajt +2 musi mieć wartość 1-4, a bajt +3 wartość 1-0x1f; `01 01` zwraca bajty +2 i +3; bajt +3 = 4 zmienia PID USB (§10) [O]. Znaczenie bajtów +0, +1 i u16 +4 jest nieznane [N].
- `01 0e` zapisuje UDID razem z rekordem: `0x8450` sprawdza dane, kasuje stronę 0x3E00 i zapisuje 6 słów [O]. Czy 0x3E00 to ostatnia strona obszaru bootloadera, zależy od rozmiaru strony flash [I].
- UDID służy jako numer seryjny USB (§10) i jest wysyłany do BLE w `01 10` (§3) [O].

## 13. Wykaz adresów

| Adres | Funkcja |
|---|---|
| `0xf248` | `main` |
| `0x7658`, `0x7758`, `0x77ea` | inicjalizacja UART i PDMA; USART1 (BLE); USART0 (LED) |
| `0xec82`, `0xebf2` | przerwania USART1 (IRQ 24) i USART0 (IRQ 23) |
| `0x86ae`, `0x8748`, `0x870a`, `0x8778` | odbiór ramki, odczyt polecenia i danych, fragmenty |
| `0x85c0`, `0x864a`, `0x8606` | budowanie nagłówka (typ `0x12`, typ dowolny) i pól ramki |
| `0x87b0`, `0x88d0`, `0x8912`, `0x899e` | wysyłka z fragmentacją, routing, przekazywanie, odpowiedź |
| `0xacac`, `0xacbe`, `0xad2a` | kolejki nadawcze LED i BLE, proces nadawczy |
| `0x59e2`, `0x58b2`, `0x20000494` | proces „AT process”, wyszukiwanie w tablicy poleceń, tablica |
| `0x5d88`, `0x6256`, `0x631e`, `0x5b56`, `0x62fc`, `0x6410`, `0x6034`, `0x5acc`, `0x67e2` | procedury grup `0x01`, `0x02`, `0x03`, `0x10`, `0x12`, `0x20`, `0x30`, `0x50`, `0x80` |
| `0xf012` | proces Daemon (handshake) |
| `0x7f66`, `0x7f9c` | `02 01 02` i `01 03 02` do BLE |
| `0x7f1e`, `0x7ee8` | `02 01 02` i `20 03` do LED |
| `0x7e76`, `0x7eac` | `40 xx` do BLE, `20 xx` do LED |
| `0x5edc`-`0x5f28` | obsługa `01 03` od BLE i wysyłka `01 10` |
| `0x66d6`, `0x6612`, `0x66e2` | `20 0c` od BLE, `20 07` od BLE, `20 0d` |
| `0xa188`, `0xa1b2`, `0xa2b4`, `0xa42c`, `0xa444` | klawisz slotu: naciśnięcie, zwolnienie, proces Tap, parowanie, łączenie |
| `0xe1b2`, `0xe2a4`, `0xe240` | raporty klawiatury, multimediów i myszy (USB albo BLE) |
| `0x7ab4`, `0x7b3c`, `0x7bb4` | odczyty PC0 (tryb magistrali) i PC10 (wybór wyjścia) |
| `0xea4e`, `0xeaf6`, `0xe988` | przerwania EXTI0-1 (PC0), EXTI4-15 (PC10) i WKUP |
| `0x7288`, `0x72aa`, `0x7438`, `0x7450` | odczyt trybu magistrali, wyboru wyjścia, stanu LED, stanu BLE |
| `0x705a`, `0x6cfe`, `0x836c`, `0x6e74`, `0x6dde` | Power Manager, licznik bezczynności, RTC, DeepSleep, `11 01` do BLE |
| `0xed0c`, `0x959a`, `0x94b2` | przerwanie ADC, średnia, napięcie i procent |
| `0x71ec`, `0x8450` | wczytanie i zapis rekordu urządzenia |
| `0xb1b8`, `0xe0c0` | konfiguracja pinów, ustawienie funkcji AFIO |
| `0x8c44`, `0x8bbc` | proces akcelerometru, sonda `WHO_AM_I` |
| `0xcaee` | procedura SysTick |
| `0x2000017c`, `0x80aa`, `0x7d40`, `0xcb78` | deskryptor mapy, wybór mapy, mapa z SPI, mapa fabryczna |
| `0xcbac`, `0xcce0`, `0xf2a4` | mapa fabryczna ANSI i ISO, pozycje klawiszy w macierzy |
| `0x9788`, `0x92e6`, `0xa0b0`, `0xa0ec`, `0xa1f2` | obsługa klawisza z wyborem warstw 3 i 4, odczyt mapy, tap-key: naciśnięcie, zwolnienie, zamiana na przytrzymanie |
| `0x20001181`, `0x2000116b`, `0x2000116c`, `0x2000116d` | bajt trybu (3 = kierownica), włącznik tap-key (`10 05`), czas (`10 13`) i tryb tap-key (`10 11`) |
| `0xd80c`, `0xd820`, `0xd950`, `0xdad4`, `0xdb98`, `0xe560` | int na double, `atan2`, mnożenie, dzielenie, double na int, `atan` |
| `0xb734` | martwe ramki `40 07` (`02-protokol.md` §2) |
