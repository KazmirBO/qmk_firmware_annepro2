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

## 7. Zasilanie i sen

- Licznik bezczynności (`0x6cfe`, wołany z SysTick, gdy nie ma zdarzeń) rośnie tylko, gdy jednocześnie: nie ma USB (tryb 0), flaga `0x20001163` = 1 (ustawiana `20 0d`, w ObinsKit przełącznik „Auto Sleep”, `writeLightHoldOn`), LED i BLE są w stanie 2, wskaźnik baterii jest wyłączony i żaden proces nie zgłasza aktywności. W przeciwnym razie licznik jest zerowany [O].
- Przy 90 tickach `0x836c` uzbraja RTC (zegar LSE, preskaler 2^15) limitem snu w sekundach (`01 0f`, 10-3600, wartość spoza zakresu daje 10) [O].
- Powyżej 100 ticków (około 1 s) zdarzenie [3] wprowadza MCU w DeepSleep (`0x6e74`: SLEEPDEEP i WFI) [O]. Przed snem PC3 = 1 tylko wtedy, gdy podświetlenie, Caps Lock i wskaźnik baterii są wyłączone [O].
- Wybudzenie (`0xe988`): linia EXTI (wiersze macierzy, PC0, PC10, PA6) daje zwykłe wybudzenie i powrót do pracy. Dopasowanie RTC, czyli upływ limitu snu, publikuje zdarzenie [2]: MCU wysyła do BLE `11 01` (`0x6dde`, jedyne wywołanie z `0x71c2`) i ustawia tryb długiego snu (`0x200011a7` = 1) [O].
- W trybie długiego snu kolejne uśpienie nie uzbraja RTC, ustawia PC3 = 1 i przełącza PA4 (TX do BLE) na GPIO w stanie wysokim (`0xb5de`). Wybudzenie klawiszem ustawia PA4 nisko, przywraca USART i kasuje tryb (`0xb616`) [O]. Co to robi z układem BLE, nie wiadomo [N].
- Przerwanie LVD (`0xee78`, `0x83d2`) ustawia PC3 = 1, gasi podświetlenie i zapisuje konfigurację [O].
- Rola PC3: kod nie wiąże go z macierzą, a warunki ustawiania wskazują na zasilanie lub uśpienie układu LED [I].

## 8. Bateria

- Pomiar: ADC kanał 7 na PA7 (funkcja AFIO 2), zegar ADC z dzielnikiem APBCFGR.ADCDIV = 6 (`HCLK/64` [I]), wyzwalanie programowe, 12 bitów; rejestry `0x40010000` + 0x000, 0x004, 0x030, 0x070, 0x074, 0x080, 0x088, 0x08C [O].
- Przerwanie ADC (`0xed0c`) kopiuje próbki, `0x959a` zbiera 8 próbek i liczy średnią bez najmniejszej i największej: (suma - min - max) / 6, zapisaną pod `0x2000118d` [O].
- Pomiar startuje przy starcie, co 60 000 ticków (10 min), po `KC_BL_BATT`, po `01 05` i po `80 01` [O].
- `0x94b2` przelicza wynik na napięcie `U = ADC * 3,1 * 2,02 / 4095` (arytmetyka double, 2,02 jako float; rozpoznanie funkcji mnożenia i dzielenia [I]) i na procent progami `U >= próg`: 4,1 V 100; 4,0 V 90; 3,9 V 80; 3,8 V 70; 3,75 V 60; 3,7 V 50; 3,65 V 40; 3,6 V 30; 3,5 V 20; 3,4 V 10; poniżej 0 (schodki, bez interpolacji) [O].
- Procent idzie do LED (`20 0f <procent>`) tylko po `KC_BL_BATT` (flaga `0x2000119a`). Ponowne `KC_BL_BATT` przy włączonym wskaźniku wysyła `20 0f 00` i go gasi [O].
- Do BLE MCU nie wysyła ani pomiaru (`01 05`), ani procentu [O]. Układ BLE startuje z poziomem 100 % i bez `01 05` go nie zmienia, więc host połączony z oryginałem widzi stałe 100 % [I, `05-ble.md` §9].

## 9. Pamięć SPI NOR

- SPI1 z CS na PA3; identyfikacja poleceniem `0x9F` (`0xdc1c`) [O].
- Tablica identyfikatorów JEDEC (`0x2000064c`) ma 15 wpisów (Winbond, Macronix, SST, GigaDevice, Boya), z czego 14 różnych (0x684015 dwa razy) [O].
- Mapa: konfiguracja 0x0000 (`0x7dba` zapisuje też sektor 0x4000), mapy klawiszy 0x1000-0x13FF (0x100 na warstwę), dziennik czasu pracy 0xA000 i 0xB000, makra od 0x10000 (0x1000 na makro), efekty użytkownika od 0x20000 (0x1000 na efekt, `0x75a8`) [O].
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
- Tryb kierownicy (`KC_FN_WHEEL`, „Game wheel”): włącza się tylko przy wyjściu USB i podłączonym USB; LED dostaje `20 12 01` (przy wyłączeniu `20 12 00`). Co tick proces liczy kąt przechyłu (funkcja rozpoznana jako `atan2` [I], przeliczenie na stopnie) i wpisuje go jako oś Ry raportu gamepada (IF3, EP 0x86). Klawisze 0xD0-0xDF służą jako pedały i przyciski [O].
- Poza USB proces co 2 ticki wysyła do BLE `50 07` z 5 B spod `0x200002c4`, którą BLE 2.05 ignoruje; zapisu pod ten adres nie znaleziono, więc to, że to dane gamepada, jest tylko przypuszczeniem [N]. Kierownica działa więc tylko po USB [O].
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
