# Protokół ramek

Jeden format ramki łączy MCU z LED i BLE, komputer (ObinsKit, USB HID) z MCU oraz hosta po BLE (raport HID ID 2) z MCU. Znaczniki i konwencje: `README.md` §2.

## 1. Format ramki

| Offset | Pole | Opis |
|---|---|---|
| 0 | `0x7B` | początek ramki (`{`) [O] |
| 1 | typ | `0x12` albo `0x10` [O] |
| 2 | adresy | wysoka połówka: węzeł docelowy, niska: nadawca [O] |
| 3 | sekwencja | typ `0x12`: bit 7 oznacza, że będzie kolejny fragment, bity 0-6 to numer; typ `0x10`: wysoka połówka to liczba fragmentów, niska indeks [O] |
| 4-5 | długość | little-endian, liczy `cmd`, `sub` i dane (2 + liczba bajtów danych) [O] |
| 6 | zero | MCU i BLE zawsze wpisują 0; żaden odbiornik go nie sprawdza [O] |
| 7 | `0x7D` | koniec nagłówka (`}`) [O] |
| 8 | `cmd` | grupa polecenia [O] |
| 9 | `sub` | podpolecenie [O] |
| 10... | dane | `długość - 2` bajtów [O] |

- Odbiorniki MCU (`0x86ae`) i BLE (`3:9E71`) szukają `0x7B` i przyjmują tylko typ `0x12` albo `0x10`; bajtów 6 i 7 nie sprawdzają. Ramka nie ma sumy kontrolnej. Odbiornik BLE nie ogranicza długości z góry (`3:9F96` czyta `len - 2` bajtów) [O].
- Nadawca w MCU buduje nagłówek w `0x85c0` (typ `0x12`) i `0x864a` (typ dowolny), pola `cmd`/`sub`/dane w `0x8606`; w BLE odpowiednio `3:9B8A` i `3:9C10` [O].
- Numeracja węzłów i grup zgadza się z kodem `AnnePro2-Tools` (`AP2Target`, `L2Command`) i z ObinsKit (`USB_HOST=1` ... `MCU_BLE=5`) [O, źródła zewnętrzne].

## 2. Węzły i routing

| Węzeł | Rola | Droga w MCU (`0x88d0`) |
|---|---|---|
| 1 | komputer przez USB HID (ObinsKit), raporty 64 B na EP4 | wysyłka tylko przy podłączonym USB (`0x840c`) [O] |
| 2 | host połączony po BLE | ta sama kolejka co węzeł 5, czyli przez układ BLE; po stronie BLE węzeł 2 to raport HID ID 2 (§4) [O] |
| 3 | MCU | lokalna kolejka i tablica poleceń (`0x8400`) [O] |
| 4 | LED | kolejka `0x20000624`, USART0 [O] |
| 5 | BLE | kolejka `0x20000630`, USART1 [O] |

- Proces „AT process” (`0x59e2`) pobiera ramki z kolejki odbiorczej. Ramka z celem 3 trafia do tablicy poleceń `0x20000494` (13 wpisów po 8 B, wyszukiwanie liniowe w `0x58b2`), każda inna jest przekazywana bez zmian do węzła docelowego (`0x8912`) [O]. Ramki od komputera do BLE przechodzą więc przez MCU.
- Odpowiedź dostaje nadawca, z zamienionymi połówkami bajtu 2. MCU (`0x899e`) i BLE (`3:A326`) odpowiadają tylko nadawcom 1, 2 i 3 [O].
- Bajt 2 w praktyce: `0x53` MCU do BLE, `0x43` MCU do LED, `0x35` BLE do MCU, `0x34` LED do MCU, `0x13` i `0x23` odpowiedzi MCU dla komputera i hosta po BLE [O].
- MCU buduje też wewnętrzne ramki `40 07 <n>` z bajtem adresowym `0x00` (`0xb734`), których router nie wysyła nigdzie (brak gałęzi dla celu 0) [O].

## 3. Fragmentacja

- MCU dzieli dłuższe dane na fragmenty w `0x87b0`: 54 B danych domyślnie, 8 B dla celu 2, 256 B dla celu 4. Fragmentuje oba typy ramek: przy typie `0x12` bit 7 bajtu 3 mówi, że będzie kolejny fragment [O].
- Odbiór: MCU (`0x870a`, `0x8778`) i BLE (`3:9F3F`, `3:A019`) odrzucają fragment o niezgodnym indeksie; `0x870a` zamienia zapis typu `0x10` (liczba i indeks) na zapis typu `0x12` (bit 7 i indeks) [O].
- Układ BLE nie składa fragmentów: każdy idzie osobno do dyspozytora albo dalej [O].

## 4. Kanał przez BLE (raport HID ID 2)

- Report Map BLE ma raport producenta ID 2 (strona 0xFF00) z wyjściem i wejściem po 18 B [O].
- Zapis hosta do raportu wyjściowego ID 2 trafia do bufora cyklicznego `X:0C60` (0x100 B) i do tego samego parsera co ramki z UART (zdarzenie OSAL 0x0080, `1:F667`) [O]. Ramki z celem innym niż 5 BLE przekazuje UART-em do MCU, a odpowiedzi dla węzła 2 wysyła jako raport wejściowy ID 2 (`3:A256`, `1:F72C`), zawsze 18 B od początku ramki [O].
- 18 B to 8 B nagłówka, `cmd`, `sub` i 8 B danych fragmentu, co zgadza się z fragmentacją MCU dla celu 2 [O].
- ObinsKit po BLE używa raportów 19 B (ID plus 18 B), ramek typu `0x12` z nadawcą 2; po USB raportów 64 B, ramek typu `0x10` z nadawcą 1 (grupa `0x02` zawsze typ `0x10`) [O, kod ObinsKit].

## 5. Polecenia według odbiorcy

Kolumna „ObinsKit” podaje nazwę polecenia w ObinsKit 1.2.11 (`06-obinskit-i-bootloader.md` §3). Polecenie bez obsługi zostaje bez odpowiedzi.

### 5.1. MCU 2.36 (węzeł 3)

Tablica `0x20000494`: `0x01` `0x5d88`, `0x02` `0x6256`, `0x03` `0x631e`, `0x10` `0x5b56`, `0x12` `0x62fc`, `0x20` `0x6410`, `0x30` `0x6034`, `0x40` `0x6358` (sama instrukcja `bx lr`), `0x50` `0x5acc`, `0x80` `0x67e2`; `0x11`, `0x21` i `0x60` mają wpis zerowy [O].

| `cmd sub` | Działanie [O] | ObinsKit |
|---|---|---|
| `01 01` | 2 B z `0x20001146`: bajty +2 i +3 rekordu urządzenia (domyślnie 1, 5) | `readDeviceType` |
| `01 02` | u32: czas pracy w godzinach | `readPowerOnTime` |
| `01 03` | od komputera: 6 B, trzy wersje u16 (MCU, LED, BLE); od LED: zapis wersji, przy wersji co najmniej 0x100 stan LED = 2 i konfiguracja LED; od BLE: wersja z połówek bajtu do `0x2000118b`, przy co najmniej 0x100 stan BLE = 2, a zawsze odpowiedź `01 10` (`03-mcu.md` §3) | `readFirmwareVersion` |
| `01 05` | start pomiaru ADC; odpowiedź: u32 z zapamiętaną średnią z poprzedniego pomiaru | `readBattery` |
| `01 06` | u32 z `0x20001191` | - |
| `01 08` | 2 B: flaga pamięci SPI (`0x200011ae`) i obecność akcelerometru (`0x200011af`) | - |
| `01 09` | 24 B stanu (liczniki i bajty konfiguracji) | `readHardwareState` |
| `01 0a` | od LED z danymi 1: ping `02 01 02` do LED | - |
| `01 0b` | zeruje czas pracy, kasuje sektory SPI 0xA000 i 0xB000 | `resetPowerOnTime` |
| `01 0d` / `01 0e` | odczyt / zapis 16 B UDID; zapis idzie z rekordem urządzenia na stronę flash 0x3E00, po kontroli | `readUDID` / `writeUDID` |
| `01 0f` | u16: limit snu w sekundach, 10-3600, zapis konfiguracji | `writeSleepTimeout` |
| `02 01` | od LED: stan i wersja LED; od BLE: zapis stanu BLE (aplikacja BLE 2.05 tej ramki nie wysyła); od komputera (nadawca 1) z danymi 1 przy podłączonym USB: sygnatura `0xfab2` pod `0x20001ffc` i reset do bootloadera; host po BLE nie wejdzie w IAP | `writeIapMode` |
| `02 02` | odpowiedź 2 (aplikacja) | `readIapMode` |
| `03 01` | odczyt pamięci SPI: adres u32 + `data[5]`·0x10000, długość `data[4]` | - |
| `10 01` | zapis mapy klawiszy warstwy 0-3 do SPI 0x1000 + 0x100·warstwa; przy warstwie 0 kasuje sektor | `downloadKbdLayout` |
| `10 02` | odczyt mapy warstwy (z kopii w RAM) | `uploadKbdLayout` |
| `10 03` / `10 05` / `10 06` / `10 07` | bajty konfiguracji `0x2000115e` / `0x2000116b` / `0x2000115c` / `0x2000115f`; `10 07` wysyła też `20 09` do LED | `writeKbdMagicFn` / `writeKbdTap` / `writeKbdLayout` / `writeKbdCapslock` |
| `10 11` / `10 12` / `10 13` | tryb tap-key (2/3), tryb modyfikatora (4/5), czas 5-200 | `writeKbdTapkeyActMode` / `writeKbdTapkeyModifykeyActMode` / `writeKbdTapkeyTimeout` |
| `12 01` | strumień zdarzeń klawiszy wł./wył. (`0x20001351`); przy włączonym MCU wysyła do komputera `12 02` z 10 B (5 × u16) przy każdej zmianie macierzy | - |
| `20 xx` | podświetlenie: podpolecenia 1-7, 0x0c-0x0e, 0x12, 0x15, 0x1a, 0x1b, 0x1e, 0x1f (`04-led.md` §3) | `writeLightList` (`20 04`), `writeLightHoldOn` (`20 0d`), `writeBacklightBrightness` (`20 15`), `readLedIndicatorColor` / `writeLedIndicatorColor` (`20 1a` / `20 1b`), `readLedPowerMode` / `writeLedPowerMode` (`20 1e` / `20 1f`), `downloadLight` (`20 02`) |
| `30 03` | makra wł./wył. (`0x2000115d`) | `writeMacroFn` |
| `30 05` | zapis danych makr do SPI 0x10000 + offset | `downloadMacro` |
| `30 06` / `30 07` / `30 08` / `30 09` | nagłówek, lista aktywnych (3 B na wpis), mapa (14 B na wpis), treść makra | `readMacroConfig` / `readMacroActiveList` / `readMacroMap` / `readMacro` |
| `30 0a` | kasowanie makr (do 16 sektorów) | `clearMacro` |
| `50 01` / `50 02` / `50 03` / `50 05` / `50 06` | akcelerometr: potwierdzenie, stop trybu kierownicy, odczyt `WHO_AM_I`, inicjalizacja, konfiguracja wykrywania stuknięć (`50 04` nie istnieje) | - |
| `80 01` / `80 02` | jak `01 05` / pusta funkcja z potwierdzeniem | - |

Polecenia ObinsKit bez obsługi w MCU 2.36: `01 04` (`readMcuUUID`), `01 0c` (`writeBacklightAuto`), `01 11` (`readBatteryPercentage`), `01 f1` (`getCombinationMode`), `20 14` (`writeIndicatorBrightness`), `20 18` / `20 19` (`readLedIndicatorLocation` / `writeLedIndicatorLocation`), `20 1c` / `20 1d` (kolor Caps Lock), `30 01` (`writeMacro`), `30 04` (`resetMacro`), cała grupa `0x60` (mysz wirtualna: `60 01`-`60 03`, `60 05`) i `0x70` (OLED innych modeli) [O].

### 5.2. LED 2.33 (węzeł 4)

Tablica `0x200029b8` ma 12 wpisów (0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30, 0x40, 0x50, 0x60), procedury tylko dla `0x01` (`0xa652`), `0x02` (`0xa5b4`), `0x20` (`0xa228`) i `0x21` (`0xa224`, sama instrukcja `bx lr`) [O]. Szczegóły w `04-led.md` §3:

| `cmd sub` | Działanie [O] |
|---|---|
| `01 03` | odpowiedź: wersja u16 0x0221 |
| `02 01` | na `02 01 02` od MCU odpowiedź `21 02 02 21` (stan 2, wersja 0x0221) |
| `02 03` | emulacja odpowiedzi bootloadera: 24 B, trzy rekordy po 8 B z 0x40 w bajcie 5 |
| `20 xx` | podświetlenie, wskaźniki, efekty |
| `21 xx` | pusta procedura, więc wizualizacja dźwięku z ObinsKit (`21 01`, `21 02`) nie działa |

### 5.3. BLE 2.05 (węzeł 5)

Tablica plik+0x2B5 (13 wpisów po 3 B, kopiowana do `X:1878`, przeszukiwana w `1:F736`): `0x01` `1:F788`, `0x02` `1:FB58`, `0x10` `1:FB0B`, `0x11` `1:FE92`, `0x40` `1:FC24`; `0x03`, `0x12`, `0x20`, `0x30`, `0x50`, `0x80` mają pustą procedurę (`LJMP 0x25E3`), `0x21` i `0x60` wpis zerowy [O]. Szczegóły w `05-ble.md` §4:

| `cmd sub` | Działanie [O] | Odpowiedź do MCU |
|---|---|---|
| `01 03` | wersja 0x25 | `01 03 25` (nadawcom 1-3) |
| `01 05 <lo> <hi>` | surowy odczyt ADC z MCU; BLE liczy napięcie i procent Battery Level | brak |
| `01 10 <id> <32 B>` | `id` 2 ustawia DIS Serial Number; `id` 0 zapisuje System ID i nadpisuje 24 B za nim (zmienne slotu); `id` 1 i 3-8 nic nie robią; `id` > 8 jest odrzucany | brak |
| `02 01 01` | potwierdzenie (tylko nadawcy 1), potem reset watchdogiem do bootloadera, od dowolnego nadawcy | nadawcy 1 |
| `02 01 02` | nic, chyba że nadawca 1 (wtedy potwierdzenie) | tylko nadawcy 1 |
| `02 02` | odpowiedź 2, tylko nadawcy 1 | tylko nadawcy 1 |
| `10 04 <8 B>` | raport klawiatury, HID ID 1 | brak |
| `10 08 <4 B>` | raport multimediów, HID ID 3; przekazywany jest tylko pierwszy bajt | brak |
| `11 01` | podtrzymanie aktywności: uzbraja wybudzenie P0.1 i odsuwa o 500 ms zwolnienie blokady snu | 1 B = 0 |
| `40 xx` | sterowanie radiem i bondami (`05-ble.md` §4.2) | zależnie od `sub` |
| `50 07`, `60 04` | ignorowane (`50` pusta procedura, `60` wpis zerowy) | brak |

### 5.4. Bootloader (wszystkie trzy procesory w trybie IAP)

| `cmd sub` | Działanie | ObinsKit |
|---|---|---|
| `02 01 <tryb>` | 1 wejście w IAP, 2 powrót do aplikacji | `writeIapMode` |
| `02 02` | tryb: 1 IAP, 2 aplikacja | `readIapMode` |
| `02 03` | bazy i rozmiary obszarów aplikacji MCU, LED i BLE | `readIapFwVersion` |
| `02 31 <adres> <dane>` | zapis fragmentu | `writeIapMemory` |
| `02 43 <adres>` | kasowanie | `writeEraseMemory` |

Kody i układ danych pochodzą z ObinsKit i z odczytu `--info` na urządzeniu (`06-obinskit-i-bootloader.md` §4 i §6) [O]; kod bootloaderów nie jest w obrazach.

## 6. Ruch wysyłany samodzielnie

### 6.1. MCU do BLE (bajt adresowy `0x53`)

| Ramka | Kiedy [O] |
|---|---|
| `02 01 02` (typ `0x10`, bajt 3 = `0x10`) i zaraz `01 03 02` (typ `0x12`) | przy starcie, potem 5 razy co 50 ticków i dalej co 500 ticków, dopóki stan BLE nie wynosi 2 (`0x7f66`, `0x7f9c`); aplikacja BLE ignoruje pierwszą i odpowiada na drugą |
| `01 10 <32 znaki hex UDID>` | po każdym `01 03` od BLE; BLE odrzuca ją, bo pierwszy znak traktuje jako `id` > 8 |
| `40 01 <slot>` / `40 04 <slot>` | przytrzymanie / krótkie naciśnięcie klawisza slotu |
| `10 04 <8 B>`, `10 08 <4 B>`, `60 04 <8 B>` | raporty klawiatury, multimediów i myszy wirtualnej, gdy wyjściem jest BLE |
| `11 01` | po wybudzeniu przez RTC, czyli po upływie limitu snu |
| `50 07 <5 B>` | co 2 ticki w trybie kierownicy, gdy wyjściem nie jest USB; 5 B spod `0x200002c4` |

### 6.2. BLE do MCU (bajt adresowy `0x35`)

| Ramka | Kiedy [O] |
|---|---|
| `01 03 25` | raz po inicjalizacji (`1:EF82`) i w odpowiedzi na `01 03` |
| `20 07 <0/1>` | zapis hosta do raportu wyjściowego ID 1 (stan Caps Lock, bit 1) |
| `20 0c 00`, dwa razy | wejście w stan połączony; przy rozłączeniu ramki nie ma |
| odpowiedzi 1 B = 0 | na `40 01`, `40 04`, `40 05`, `40 40`-`40 42` i `11 01` |
| ramki od hosta po BLE | przekazywane z celem 3 i nadawcą 2 |

### 6.3. MCU i LED

| Ramka | Kierunek | Kiedy [O] |
|---|---|---|
| `02 01 02` | MCU do LED | ping co 50, potem co 500 ticków, dopóki stan LED nie wynosi 2; typ ramki zmienia się przy każdym wysłaniu (`0x12`/`0x10`) |
| `01 03 <u16 0x0221>` | LED do MCU (bajt `0x34`) | przy starcie LED |
| `20 09`, `20 1b`, `20 1f`, `20 15` | MCU do LED | po zgłoszeniu stanu 2 przez LED |
| `20 03 <efekt> <dane>` | MCU do LED | początkowy efekt, gdy podświetlenie jest włączone |
| `20 0b <slot> <1/0>` | MCU do LED | klawisz slotu (1 parowanie, 0 łączenie) |
| `20 0c 00 00` | MCU do LED | po `20 0c` od BLE (gasi wskaźnik slotu) |
| `20 07 <0/1>` | MCU do LED | po `20 07` od BLE |
| pozostałe `20 xx` | oba kierunki | `04-led.md` §3 |
