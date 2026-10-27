# BLE 2.05 (układ Bluetooth, 8051)

Obraz `ap2_c18_0205_ble.bin` (155 648 B). Adresy kodu w zapisie `b:AAAA` (b to bank, 0 obszar wspólny), `X:AAAA` to XDATA, `plik+0x…` offset w pliku. Znaczniki i konwencje: `README.md` §2. Nazwy funkcji i parametrów TI BLE SDK (np. `HidDev_Report`, `GAPBOND_*`) są wiedzą zewnętrzną; numery i wartości pochodzą z kodu.

## 1. Identyfikacja

- TI CC2540 albo CC2541 (CC254x) ze stosem TI BLE i OSAL, kompilator IAR (model bankowany) [O]. Kod używa rejestrów FMAP, RFD, RFST, ENCCS, timera T2, timera uśpienia ST0-ST2, SLEEPCMD, WDCTL, USART0, DMA oraz XREG: kontrolera flash `X:6270`-`X:6273`, OBSSEL, CHVER i IVCTRL [O].
- Wariantu nie da się rozstrzygnąć: obraz nie dotyka rejestrów USB (wyróżnik CC2540) ani I2C (wyróżnik CC2541) [O]. Śladem do sprawdzenia jest `1:9D1E`-`1:9D40` (IVCTRL = 0x1B, gałąź dla CHVER co najmniej 0x13) [N]. Według TI oba układy są zgodne wyprowadzeniami w obudowie QFN40 6 × 6 mm, jeśli CC2540 nie używa USB, a CC2541 I2C [zewn., datasheet CC2541 Rev. D].
- Flash: kod sięga banku 4 (fizycznie do 0x27FFF), a TI oferuje tylko warianty 128 KB i 256 KB, więc to wariant F256 [I].

## 2. Obraz, banki i pamięć

- Obraz to płaski zrzut flash od adresu fizycznego 0x2000; fizyczne 0x0000-0x1FFF (bootloader) w nim nie występuje [O, I co do bootloadera]. Treść kończy się na plik+0x1F480 (128 129 B), dalej są bajty 0xFF [O].
- Obszar wspólny: CPU = offset + 0x2000. Banki 1-4 w oknie 0x8000-0xFFFF: offset = 0x6000 + (b-1)*0x8000 + (adres - 0x8000), czyli bank 1 to plik+0x6000-0xDFFF, bank 2 plik+0xE000-0x15FFF, bank 3 plik+0x16000-0x1DFFF, bank 4 plik+0x1E000-0x25FFF [O].
- Wektory własnej tablicy (plik+0x00 = CPU 0x2000) [O]:

| Offset | Przerwanie | Cel |
|---|---|---|
| 0x00 | reset | `LJMP 0x25CB` |
| 0x03 | RFERR | 0x3B72 |
| 0x2B | ST (timer uśpienia) | 0x428B |
| 0x3B | UTX0 | 0x42E5 |
| 0x43 | DMA | 0x4262 |
| 0x53 | T2 | 0x3BE8 |
| 0x6B | P0INT | 0x42AA |
| 0x83 | RF | 0x39CF |

Brak wektora URX0, bo odbiór UART idzie przez DMA [O].
- Start (`0:25CB`): PSW = 0, SP = 0x3F, wskaźnik stosu XDATA (I:1A/1B) = 0x0280, FMAP = 0; `0:2537` ustawia MEMCTR.XBANK = 1, więc `X:8000`-`X:FFFF` to bank 1 (`X:8xxx` = plik+0x6xxx); kod kopiuje plik+0x92-0x536 do `X:1655`-`X:1AF9` (dane inicjujące: tablice atrybutów GATT, konfiguracja HID, dane rozgłaszania; X = plik + 0x15C3), zeruje `X:0280`-`X:1654` [O].
- Trampolina banków: w obszarze wspólnym jest 830 stubów po 6 B w jednym ciągu `0:434B`-`0:56BF` (`12 25 e6 <lo> <hi> <bank>`, np. `12 25 e6 88 f7 01` pod `0:5395` prowadzi do `1:F788`); cele: bank 0 37, bank 1 199, bank 2 189, bank 3 338, bank 4 67, wszystkie różne [O]. `0:25E6` zdejmuje adres danych ze stosu, odkłada FMAP, czyta 3 bajty, odkłada adres celu, ustawia FMAP i skacze przez RET; procedury w bankach wracają przez `0:25E3` (POP FMAP, RET) [O].
- Bez sumy kontrolnej: bajty plik+0x90-0x91 (`93 8e`) nie mają odwołań w aplikacji, a od plik+0x92 zaczynają się dane inicjujące, więc wcześniejsze skojarzenie z nagłówkiem TI OAD odpada; test CRC16 nie dał trafienia [O/N].
- Bajty pod plik+0x619A (`78 69 e0 ab 50 c3 f0 d2`) to stała używana przez kod warstwy łącza w `1:9F6A`, znaczenie [N]; następujące po nich `01 2a` to UUID 0x2A01 [O]. Ciąg `oLoLoL…` pod plik+0x1B9-0x1DB to wartość początkowa bufora RAM `X:177C`-`X:179E`, który zapisuje kod warstwy łącza [O].

## 3. UART do MCU

- USART0 w lokalizacji 1 (`3:E2EC`): P0.2 RX, P0.3 TX. Kod obsługuje `U0UCR` = 0x02 (8N1) i 0x42 (z kontrolą przepływu, P0.4 jako CTS); `HalUARTOpen` (`4:91E0`-`4:923E`) otwiera port z indeksem prędkości 4 (M = 0xD8, E = 11, czyli 115200 przy 32 MHz), bez kontroli przepływu, z buforami 0x80 B i callbackiem `1:F3BD` [O].
- Odbiór przez DMA kanał 3 (deskryptor `X:0831`-`X:0838`): źródło `X:70C1` (U0DBUF), cel `X:084C`, wyzwalacz URX0, transfery 16-bitowe [O].
- Parser ramek: `3:9E71` (synchronizacja na 0x7B, typ 0x12/0x10), `3:9F3F` i `3:A019` (fragmenty), `3:9F96` (polecenie i dane do `X:0C68`), potem dyspozytor `1:F736` przez `1:F667` (zdarzenie OSAL 0x0080) [O]. Ten sam bufor `X:0C60` przyjmuje ramki od hosta po BLE (`02-protokol.md` §4).

## 4. Polecenia od MCU

Tablica dyspozytora: `02-protokol.md` §5.3.

### 4.1. Grupy `0x01`, `0x02`, `0x10`, `0x11`

- `01 03`: odpowiedź `01 03 25` (`1:F7B2`); dane żądania nie są używane [O].
- `01 05 <lo> <hi>`: surowy odczyt ADC baterii z MCU (§9); bez odpowiedzi [O].
- `01 10 <id> <dane>` (`1:FADF`, `3:F4F4`): zawsze 32 B danych niezależnie od długości ramki. `id` 2 zapisuje `X:179F`, czyli DIS Serial Number (0x2A25); `id` 0 zapisuje System ID (`X:0750`, 8 B) i nadpisuje 24 B za nim, w tym bieżący slot (`X:0758`) i tryb (`X:0759`); `id` 1 i 3-8 nic nie robią; `id` większe od 8 kończy się błędem bez odpowiedzi [O]. Fabryczny MCU wysyła 32 znaki hex bez bajtu `id`, więc BLE bierze pierwszy znak (0x30-0x46) za `id` i odrzuca ramkę [O].
- `02 01 01`: potwierdzenie (tylko dla nadawcy 1), pętla opóźniająca, `CLR EA`, `WDCTL` = 0xAB, potem 0x5B i pętla własna, czyli reset watchdogiem; działa od dowolnego nadawcy [O]. `02 01 02` daje potwierdzenie tylko nadawcy 1, w innym wypadku nic; `02 02` odsyła 2 tylko nadawcy 1 [O]. Ping MCU (`02 01 02` od węzła 3) nie robi więc w aplikacji nic; ma sens dla bootloadera BLE [I].
- `10 04 <8 B>`: raport klawiatury, HID ID 1 (`1:F4D7`); `10 08 <4 B>`: raport multimediów, HID ID 3 (`1:F573`), z którego przekazywany jest tylko pierwszy bajt, a pozostałe są zerami [O].
- `11 01` (`1:FE92`): potwierdzenie, uzbrojenie wybudzenia P0.1 (`1:F656`) i ponowny start 500-milisekundowego timera zadania `X:0841`, które po upływie zwalnia blokadę snu (§10). Ramka odsuwa więc zwolnienie blokady o 500 ms, ale sama jej nie zakłada [O].

### 4.2. Grupa `0x40` (`1:FC24`)

| `sub` | Działanie [O] | Odpowiedź |
|---|---|---|
| 0x01 `<slot>` | parowanie: dla slotu < 4 zrywa bieżące połączenie albo wyłącza rozgłaszanie (`2:BF3D`), ustawia slot (`1:9C1A`), nazwę i dane rozgłaszania slotu (`1:F5AF`), tryb parowania (`X:0759` = 1) i rozgłaszanie 20 ms przez 60 s (`2:C9FD`) | 1 B = 0; slot ≥ 4: brak działania i odpowiedzi |
| 0x02 | w stanie połączonym zrywa łącze, w przeciwnym razie wyłącza rozgłaszanie; bez numeru slotu | brak |
| 0x04 `<slot>` | łączenie: jak 0x01, ale tryb 0 i rozgłaszanie 20 ms przez 5 s (`2:C93D`); w tym trybie przyjmowany jest tylko bond o indeksie równym slotowi (§8) | 1 B = 0, także dla slotu ≥ 4 (bez działania) |
| 0x05 | usuwa wszystkie bondy (`GAPBondMgr_SetParameter(0x409, 0)`); nie przyjmuje numeru slotu | 1 B = 0 |
| 0x0F | test: wysyła raport HID ID 1 (8 B) z `data[2]` w bajcie klawisza (`1:F189`) | brak |
| 0x12 | liczba bondów (parametr 0x40E) | 1 B |
| 0x13 | 10 B z niezapisanego bufora na stosie (niedokończona funkcja, zwraca śmieci) | 10 B |
| 0x14 | minimalny interwał połączenia (GAPRole 0x311) | 2 B |
| 0x15, 0x16 | maksymalny interwał połączenia (GAPRole 0x312); 0x16 skacze do kodu 0x15 | 2 B |
| 0x30 | adres BD ze strony informacyjnej układu (`X:780E`-`X:7813`) | 6 B |
| 0x31 `<6 B>` | ustawia adres BD (polecenie HCI 0xFC0C, w SDK `HCI_EXT_SetBDADDR`, działa do restartu [zewn.]) | 6 B niezapisanego stosu |
| 0x40, 0x41, 0x42 | testy RF produkcji (polecenia HCI 0xFC08 i 0xFC0B; poziom mocy testu 0-2) | 1 B = 0 |

`deleteBleHost` w ObinsKit to `40 05` bez parametru (przycisk „Clear” i przywracanie ustawień fabrycznych); polecenia usuwającego bond jednego slotu BLE 2.05 nie ma [O].

### 4.3. Puste i nieobsługiwane grupy

`cmd` 0x03, 0x12, 0x20, 0x30, 0x50 i 0x80 mają pustą procedurę (`LJMP 0x25E3`), a 0x21 i 0x60 wpis zerowy w tablicy [O]. `50 07` od MCU (tryb kierownicy) jest więc ignorowane. W obrazie istnieje kod zamieniający `60 04` na raport HID ID 4 (klasyfikator `1:F416`, `1:F1E8`, `2:BEE4` z ID 4 i 8 B), ale woła go tylko obsługa zdarzenia OSAL 0x0008, którego nic poza nią samą nie uzbraja, więc to martwy kod i `60 04` jest w praktyce ignorowane [O/I: wniosek z kompletu wywołań `osal_set_event` i `osal_start_timerEx` dla zadania `X:0CA8`].

## 5. Ramki do MCU

Wszystko, co BLE wysyła do MCU samo z siebie, wychodzi czterema wywołaniami `1:F45A` z bajtem adresowym 0x35 (cel 3, nadawca 5) [O]:

| Ramka | Miejsce i warunek [O] |
|---|---|
| `01 03 25` | `1:EF82`, raz na końcu inicjalizacji (`1:ED7E`) |
| `20 07 <0/1>` | `1:F26F`, zapis hosta do raportu wyjściowego ID 1 (bit 1, Caps Lock) |
| `20 0c 00`, dwa razy | `1:F388` i `1:F3AB` w callbacku stanu `1:F346`, tylko przy wejściu w stan 6 (połączony) |

Dochodzą odpowiedzi na polecenia (`3:A326`, tylko dla nadawców 1, 2 i 3) oraz ramki od hosta po BLE przekazywane do węzła 3. Rozłączenie, koniec czasu rozgłaszania i zmiana poziomu baterii nie dają żadnej ramki [O].

## 6. GATT i HID

- Tablice atrybutów leżą w danych inicjujących (rekordy po 8 B: długość UUID, wskaźnik UUID, uprawnienia, uchwyt, wskaźnik wartości): Battery plik+0xA2, GAP plik+0x13A i 0x162, GATT plik+0x197, Device Information plik+0x210, HID plik+0x3AE, Scan Parameters plik+0x507; tablica UUID pod plik+0x630C-0x6357 [O].
- Usługi: GAP 0x1800, GATT 0x1801, Device Information 0x180A, Battery 0x180F, HID 0x1812, Scan Parameters 0x1813 (standardowa usługa TI z pustym callbackiem) [O].
- HID: HID Information `11 01 00 01` (bcdHID 1.11, flaga remote wake), Report Map (0x2A4B), Control Point (0x2A4C: przyjmuje tylko 0 i 1, inna wartość daje błąd 0x80, inna długość 0x0D), Protocol Mode (0x2A4E: 1 B, 0 albo 1, początkowo 1), Boot Keyboard Input (0x2A22) i Output (0x2A32), sześć charakterystyk 0x2A4D (Report Reference: ID 1 input, ID 1 output, ID 2 input, ID 2 output, ID 3 input, feature ID 0); brak 0x2A33 (boot mouse) [O].
- Report Map (plik+0x6000, 149 B) [O]:

| Report ID | Zawartość |
|---|---|
| 1 | klawiatura: 8 bitów modyfikatorów, bajt zarezerwowany, 5 diod (wyjście) i 3 bity dopełnienia, 6 kodów klawiszy 0..0x65 |
| 2 | strona producenta 0xFF00: wyjście i wejście po 18 B, wartości -128..255; kanał ramek protokołu (`02-protokol.md` §4) |
| 3 | consumer: Mute 0xE2, Vol Up 0xE9, Vol Down 0xEA, Play/Pause 0xCD, Next 0xB5, Prev 0xB6, 0x6F i 0x70 (jasność ekranu), 3 bajty dopełnienia |

Nie ma deskryptora myszy ani raportu ID 4 [O].
- Konfiguracja HidDev (`X:1948`): idleTimeout 0, hidFlags 0x01 (tylko remote wake). Struktura callbacków (`X:194D`, wskaźnik w `X:0B5E`, rejestracja `2:BEB1` z `1:EF15`): reportCB `1:F2B1`, evtCB `1:F343` (pusta funkcja), callback stanu `1:F346` (ramki `20 0c`) [O]. Control Point przekazuje do evtCB zdarzenia 0 i 1, Protocol Mode zdarzenia 2 (boot) i 3 (report), które giną w pustej funkcji [O].
- Zapis raportu wyjściowego (`1:F2B1`, `1:F225`): ID 1 wyciąga bit Caps Lock i wysyła `20 07`; ID 2 dopisuje bajty do bufora `X:0C60` i ustawia zdarzenie 0x0080 [O].
- Odczyt raportów (`3:E9D8`): output ID 1 zwraca 1 B z `X:0CEE`, feature 1 B z `X:0CF3`, boot output 1 B z `X:0CF2`, a output ID 2 1 B z `X:0CF7` przy długości 16, więc pozostałe 15 B to niezapisana zawartość bufora odpowiedzi ATT [O]. To ujawnianie pamięci, nie funkcja; czy bufor może zawierać materiał kluczowy, nie ustalono [N]. Wyniku takiego odczytu nie należy udostępniać.
- Wysyłka raportu (`2:BEE4`, w SDK `HidDev_Report`): odrzuca długość od 19 B wzwyż; wołają ją `1:F4E5` (ID 1, 8 B), `1:F5A0` (ID 3, 4 B), `1:F72C` (ID 2, 18 B), `1:F1DB` (`40 0F`) i martwy `1:F218` (`60 04`). Bez połączenia raport trafia do kolejki i uruchamia rozgłaszanie: przy niezerowej liczbie bondów jak `40 04` (5 s), bez bondów jak `40 01` (60 s); w stanie rozgłaszania tylko kolejkuje [O].
- Device Information [O]:

| Charakterystyka | Wartość | Źródło |
|---|---|---|
| Manufacturer Name (0x2A29) | `HEXCORE` | plik+0x61AA |
| Model Number (0x2A24) | `KB181` | plik+0x61C4 |
| Serial Number (0x2A25) | `0123456789ABCDEF0123456789ABCDEF` | plik+0x1DC (`X:179F`); zmienia go tylko `01 10 02` |
| Firmware Revision (0x2A26) | `BLE-1.5.0` | plik+0x6164 |
| Hardware Revision (0x2A27) | `1.2.0` | plik+0x61B2 |
| Software Revision (0x2A28) | `2.2.0` | plik+0x200 |
| System ID (0x2A23) | same zera | `X:0750`, zapisuje go tylko `01 10 00` |
| IEEE 11073 (0x2A2A) | `fe 00` i `experimental` | plik+0x6156 |
| PnP ID (0x2A50) | `01 0d 00 e0 f0 10 01`: źródło Bluetooth SIG, VID 0x000D (Texas Instruments [zewn.]), PID 0xF0E0, wersja 0x0110 | plik+0x209 |

- GAP: nazwa `AnnePro2` (ustawiana dla slotu, §7), atrybut Appearance 0x0000 (w danych rozgłaszania jest 0x03C1), PPCP (0x2A04) `50 00 a0 00 00 00 e8 03`: interwał 100-200 ms, latency 0, timeout 10 s [O].
- Battery: Battery Level (0x2A19) z właściwościami read i notify i z CCCD, Report Reference `04 01` (ID 4, input), Presentation Format `04 00 ad 27 01 11 31`, wartość początkowa 100 % (§9) [O].

## 7. GAP, sloty i rozgłaszanie

- Inicjalizacja GAPRole i GAPBond: `1:ED7E`. Tablica domyślnych parametrów GAP (plik+0xD3, 39 słów, kopiowana do `X:1696`) ma wartości domyślne TI; kod zmienia TGAP_CONN_PAUSE_PERIPHERAL na 4 s (`1:EDAA`) [O].
- Slot (`1:F5AF`, wołane przy starcie, przy zmianie slotu i przez `40 01`/`40 04`) wybiera nazwę urządzenia oraz dane rozgłaszania i odpowiedzi skanowania [O]:

| Slot | Nazwa (GGS) | Dane rozgłaszania | Odpowiedź skanowania |
|---|---|---|---|
| 0 | `X:8177` | plik+0x316, 21 B: pełna nazwa `AnnePro2`, Appearance 0x03C1, Flags 0x05, pełna lista UUID 0x1812 | plik+0x2DD, 9 B: Tx Power 0, zakres interwału 9..9 (11,25 ms) |
| 1 | `X:8189` | plik+0x32B, 30 B: Flags 0x05, Tx Power 0, LE Role 0, Appearance 0x03C1, krótka nazwa `AP2`, niepełna lista UUID 0x1812 i 0x180F, zakres interwału 9..9 | plik+0x2E6, 16 B: pełna nazwa `AnnePro2`, zakres interwału 9..9 |
| 2 | `X:816E` | plik+0x349, jak slot 1 | plik+0x2F6, jak slot 1 |
| 3 | `X:8180` | plik+0x367, jak slot 1 | plik+0x306, jak slot 1 |

Wszystkie cztery kopie nazwy to `AnnePro2`, a adres BD jest wspólny i pochodzi ze strony informacyjnej układu (`1:F4EF`, `X:780E`-`X:7813`), więc host nie odróżni slotów po nazwie ani adresie [O].
- Tryby rozgłaszania (interwał w jednostkach 0,625 ms) [O]:

| Tryb | Procedura | Interwał | Limit |
|---|---|---|---|
| parowanie (`40 01`, raport bez bondów) | `2:C9FD` | 20 ms | 60 s |
| łączenie (`40 04`, raport przy bondach) | `2:C93D` | 20 ms | 5 s |
| powrót po rozłączeniu | `2:C99D` | 1022,5 ms | bez limitu; procedura nieosiągalna |

- Po rozłączeniu rozgłaszanie nie startuje samo: `2:C3DD` woła `2:C99D` tylko przy ustawionym bicie 1 hidFlags, a hidFlags ma wartość 0x01 i nic jej nie zmienia [O]. Wyjątkiem jest parowanie zakończone statusem 4, po którym rusza `2:C9FD` [O]. Rozgłaszanie wznawia dopiero naciśnięcie klawisza (raport, §6) albo `40 01`/`40 04`.
- Po upływie limitu rozgłaszanie się nie wznawia (GAPROLE_ADVERT_OFF_TIME = 0), a MCU nie dostaje żadnej ramki [O].
- Parametry połączenia (`1:EE19`-`1:EE57`): aktualizacja włączona (0x310 = 1), interwał 9 × 1,25 ms = 11,25 ms (0x311, 0x312), latency 15 (0x313), timeout 4 s (0x314 = 400); żądanie idzie 4 s po połączeniu. To inne wartości niż statyczne PPCP (100-200 ms, latency 0, 10 s); co host przyjmie, pokaże dopiero `btmon` [O/N].
- Polityka filtra rozgłaszania 0 przy każdym starcie; brak rozgłaszania kierowanego i LE Secure Connections [I]. Moc nadawania zmieniają tylko testy RF (`40 40`-`40 42`) [O].

## 8. Parowanie i bondy

- GAPBond (`1:EE90`-`1:EEC7`) [O]:

| Parametr | Wartość |
|---|---|
| `GAPBOND_PAIRING_MODE` (0x400) | 1: czekaj na żądanie |
| `GAPBOND_MITM_PROTECTION` (0x402) | 0 |
| `GAPBOND_IO_CAPABILITIES` (0x403) | 3: brak wejścia i wyjścia (Just Works) |
| `GAPBOND_BONDING_ENABLED` (0x406) | 1 |
| `GAPBOND_DEFAULT_PASSCODE` (0x408) | 0 |
| `GAPBOND_AUTO_SYNC_WL` (0x40D) | 0: bez white listy |

Kod nie ustawia `KEY_DIST_LIST`, `KEYSIZE`, `OOB` ani `BOND_FAIL_ACTION`, więc obowiązują wartości domyślne biblioteki [O/N].
- Do 10 bondów; sloty to indeksy bondów. Bieżący slot to `X:0758`, tryb `X:0759` (1 parowanie, 0 łączenie), stan slotów `X:075A`-`X:075D` (1 = połączony w tym slocie) [O]. Slot jest zapisywany w SNV 0x81 (10 B, bajt 2): `1:9C87` czyta go, a przy błędzie wypełnia 0xFF i zapisuje; `1:9BE6` przy starcie przyjmuje wartości 0-9 [O].
- Przyjęcie połączenia (`2:9B16`-`2:9C12`, indeks bonda peera z `1:884C`, 10 lub więcej oznacza nieznanego) [O]:
  - w trybie łączenia przyjmowany jest tylko bond o indeksie równym slotowi; inny peer jest rozłączany, a rozgłaszanie wznawiane;
  - w trybie parowania nieznany peer jest przyjmowany; znany peer z innym indeksem jest odrzucany, jeśli jego slot ma stan 1, a w przeciwnym razie przyjmowany i staje się bieżącym slotem;
  - po każdym przyjętym połączeniu tryb wraca na 0.
- `40 05` usuwa wszystkie bondy. Parowanie slotu od nowa zastępuje tylko bond tego slotu [I].
- SNV (`4:8E19` zapis przez DMA do FWDATA, `4:8E83` kasowanie strony, `3:8AC3` kasowanie z kontrolą) [O]:
  - 0x02 i 0x03: lokalne IRK i SRK, po 16 B; 0x04: licznik podpisu, 4 B;
  - dla bonda `i` (0-9): 0x20+6i (14 B), 0x21+6i (27 B), 0x22+6i (27 B), 0x23+6i i 0x24+6i (po 16 B), 0x25+6i (4 B) oraz 0x70+i (18 B); przypisanie pól do kluczy (np. LTK) według struktur TI [I];
  - 0x81: dane aplikacji (slot).

## 9. Bateria

- `01 05 <lo> <hi>` niesie surowy odczyt ADC; BLE liczy napięcie `U = raw * 2 * 3,1 / 4095` (stałe `X:82F6` = 3,1 i `X:82AA` = 4095,0) [O]. Że odczyt jest 12-bitowy, wynika z dzielnika [I].
- Procent (`1:F82C`-`1:FADC`, progi `X:8236`-`X:82A6`) z interpolacją liniową `(U - próg) / krok * 10 + baza`, ucinaną do liczby całkowitej [O]:

| Napięcie | Procent |
|---|---|
| od 4,15 V | 100 |
| 4,0-4,15 V | 90-100 (krok 0,15 V) |
| 3,9-4,0 V | 80-90 |
| 3,8-3,9 V | 70-80 |
| 3,6-3,8 V | 30-70, co 0,05 V o 10 |
| 3,5-3,6 V | 20-30 |
| 3,4-3,5 V | 10-20 |
| poniżej 3,4 V | 0 |

- Procent trafia do `X:189F` (wartość początkowa 100). Wartość charakterystyki Battery Level (`X:165B`, początkowo 100) jest zmieniana tylko wtedy, gdy nowy poziom jest mniejszy (`3:D827`, `SUBB`/`JNC`), i wtedy idzie powiadomienie do hosta z włączonym CCCD; odczyt atrybutu przez hosta (`3:D8F7`) też obniża poziom, bez powiadomienia [O]. Po naładowaniu poziom nie rośnie do restartu układu BLE, a po restarcie zaczyna od 100 % [O].
- Poziom jest sprawdzany co 15 000 jednostek OSAL (stała `X:82DA` = 0x3A98, jednostka ms, czyli 15 s [I]) w stanie połączonym, od chwili włączenia powiadomień przez hosta (`2:C58B`, `2:C5D6`); próg krytyczny wynosi 10 (`X:0288`) [O].
- Fabryczny MCU nie wysyła `01 05` (`03-mcu.md` §8), więc z oryginalnym firmware host widzi stałe 100 % [I]. Port QMK wysyła tę ramkę (`07-port-qmk-i-narzedzia.md` §1).

## 10. Zasilanie

- OSAL usypia układ, gdy maska blokad snu (`X:15CD`, `4:90B3`) jest pusta. Zadanie `X:0841` (obsługa `1:FF34`): zdarzenie 2 z przerwania portu P0 (`0:42AA`-`0:42D4`) zakłada blokadę, zdarzenie 1 (timer 500 ms, odnawiany przez `11 01`) ją zdejmuje i ponownie uzbraja wybudzenie P0.1, zdarzenie 0x10 co 100 ms odpytuje P0.0 i P0.1 [O].
- P0.0 i P0.1 to wejścia aktywne w stanie niskim; callback `4:9046` przełącza flagę `X:0D7D`; jawnego odszumiania nie ma, jest odpytywanie co 100 ms z wykrywaniem zmiany [O]. Który pin MCU napędza P0.1, nie wiadomo [N].
- SLEEPCMD jest zapisywany w `0:3CAA` i `4:86A4`, PCON w `0:39C8` i `0:7FF8` [O].

## 11. Aktualizacja

- Mechanizm aktualizacji leży w bootloaderze spoza obrazu. Po `02 01 01` układ resetuje się watchdogiem, a dalej działa bootloader [O co do resetu, I dalej].
- Bootloader zgłasza dla BLE bazę 0x4000 i rozmiar 0x1fe00 (`06-obinskit-i-bootloader.md` §6). Treść obrazu (128 129 B) się w nim mieści, cały plik (155 648 B) już nie. Jak baza 0x4000 odpowiada fizycznemu adresowi flash (obraz zaczyna się od fizycznego 0x2000), nie wiadomo, a wgrywania BLE nie testowano [N].

## 12. Odczyt GATT połączonej klawiatury

Odczyt przez BlueZ (D-Bus), tylko do odczytu, z klawiatury z firmware QMK na MCU i fabrycznym BLE 2.05 [O, urządzenie]:

- Report Map jest bajt w bajt taka jak plik+0x6000.
- Device Information jak w §6: numer seryjny jest domyślny, bo QMK nie wysyła `01 10`; System ID same zera.
- Appearance 0x0000, PPCP 100-200 ms, latency 0, 10 s.
- Battery Level: read i notify, host ustawił CCCD na 0x0001; odczyt dał 67 %, wcześniej host pokazywał 73 %, co zgadza się z tym, że poziom tylko maleje.
- Raporty HID jak w §6. Odczyt raportu wyjściowego ID 2 zwrócił 16 B, których nie zapisano (§6).

## 13. Uwagi o bezpieczeństwie

- Parowanie Just Works bez MITM [O].
- Przez UART (czyli także przez ObinsKit i hosta po BLE, przez MCU) można zmienić adres BD (`40 31`), skasować bondy (`40 05`) i nadpisać zmienne slotu (`01 10 00`) [O].
- Trzy miejsca zwracają niezainicjowaną pamięć: odczyt raportu wyjściowego ID 2 (15 B bufora ATT), `40 13` (10 B stosu) i odpowiedź `40 31` (6 B stosu) [O].
