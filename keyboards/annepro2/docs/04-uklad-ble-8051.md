# Układ Bluetooth: obraz `ap2_c18_0205_ble.bin` (8051)

Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma. "Offset" to offset w pliku, "adres CPU" to adres widziany przez procesor (`b:AAAA`, b to numer banku, 0 to obszar wspólny). Nic z obrazu nie było uruchamiane. Analizę przeprowadzono własnym deasemblerem 8051 (256 opcodów) z nazwami rejestrów SFR wziętymi z zewnętrznego nagłówka TI (`ioCC2541.h`), użytego wyłącznie jako dane do nazw.

## 1. Identyfikacja układu

- Rodzina TI CC253x (CC2540 lub CC2541) ze stosem TI BLE [I]. Wariantu nie rozróżniłem, bo kod nie dotyka rejestrów USB (0x62xx).
- Dowody [O]: rejestr banków FMAP (SFR 0x9F), radio (RFD 0xD9, RFST 0xE1), koprocesor AES (ENCCS 0xB3), timer MAC (T2*), timer uśpienia (ST0-2), SLEEPCMD, WDCTL, USART0, DMA oraz kontroler flash pod XDATA 0x6270-0x6272.
- Flash co najmniej 256 KB [I]: bank 4 sięga adresu fizycznego 0x27FFF.
- Obraz jest płaskim zrzutem flash od adresu fizycznego 0x2000. Fizyczne 0x0000-0x1FFF (8 KB) nie ma w pliku i prawdopodobnie zawiera bootloader [I]. Zawartość kończy się na offsecie 0x1F480, dalej same 0xFF [O].
- W obszarze wspólnym adres CPU równa się offsetowi w pliku plus 0x2000 [O] (342 trafień testu "cel LCALL po RET", następny wynik to 83).
- Trampolina banków `0:25E6` czyta inline 3 bajty (adres LE i numer banku), ustawia FMAP i wraca. W obszarze wspólnym jest 830 stubów po 6 B, np. `12 25 e6 88 f7 01` pod 0x5395 odpowiada `1:F788` [O].
- Okno bankowane: 0x8000-0xFFFF, offset w pliku = `0x6000 + (n-1)*0x8000 + (addr-0x8000)`. Bank 1 to plik 0x6000-0xDFFF, bank 2 0xE000-0x15FFF, bank 3 0x16000-0x1DFFF, bank 4 0x1E000-0x25FFF [O].

Wektory własnej tablicy obrazu (offset 0, adres 0x2000) [O]:

| Offset | Przerwanie | Cel |
|---|---|---|
| 0x00 | reset | `LJMP 0x25CB` |
| 0x03 | RFERR | 0x3B72 |
| 0x2B | ST | 0x428B |
| 0x3B | UTX0 | 0x42E5 |
| 0x43 | DMA | 0x4262 |
| 0x53 | T2 | 0x3BE8 |
| 0x6B | P0INT | 0x42AA |
| 0x83 | RF | 0x39CF |

Start (`0:25CB`): PSW = 0, SP = 0x3F, wskaźnik stosu XDATA (0x1A:1B) = 0x0280, FMAP = 0 [O]. Wektory 0x0000-0x1FFF obsługuje najpewniej bootloader spoza obrazu [I, niezweryfikowane]. Brak wektora URX0: odbiór UART idzie przez DMA [O].

## 2. Łącze UART z procesorem aplikacyjnym

Fizyka [O]:
- USART0 w alternatywnej lokalizacji 1 (`3:E2EC`): P0.2 to RX, P0.3 to TX.
- Format: `U0UCR` = 0x02 (8N1) albo 0x42 (8N1 z kontrolą przepływu, wtedy P0.4 to CTS).
- Baudrate (`3:E37C`): indeksy 0-2 dają M=0x3B przy E=8, 9, 10; indeks 3 daje M=0xD8, E=10; indeks 4 daje M=0xD8, E=11, czyli 115200 przy 32 MHz. Który indeks jest wybrany, zależy od struktury w RAM [N]. 115200 przyjmuję jako wniosek z faktu, że MCU używa 115200 [I].
- Odbiór przez DMA kanał 3 (deskryptor XDATA 0x0831-0x0838): źródło 0x70C1 (U0DBUF), cel bufor 0x084C, długość 0x100, wyzwalacz URX0 [O].

Format ramki jest identyczny jak po stronie MCU (patrz `02-protokol-ramek.md`) [O]: konstruktor `3:9B8A`, odbiornik `3:9E71`. Odbiornik czeka na bajt 0x7B (`3:9ECA`), przyjmuje typ 0x12 albo 0x10 i odrzuca inne. Ramki od MCU mają starszy półbajt bajtu 2 równy 5 (to węzeł 5, czyli BLE). Przy takim adresie odbiornik idzie przez `3:9F3F`, `3:A019`, `3:9F96` i dyspozytor `1:F736` [O].

Fragmentacja typu 0x10 [I]: `3:9F3F` numeruje fragmenty; jeśli starszy półbajt bajtu 3 jest równy młodszemu plus 1, flaga bit 7 jest zerowana, inaczej ustawiana. Semantyka łączenia fragmentów nie jest potwierdzona.

## 3. Tablica poleceń i podpolecenia

Tablica jest pod offsetem 0x2B5: 13 wpisów po 3 bajty (`[cmd][adres handlera LE]`), kopiowana do XDATA 0x1878 i przeszukiwana w pętli w `1:F736` [O]. To ta sama lista kodów co w tablicy MCU, co wskazuje na wspólną bibliotekę protokołu [I].

| `cmd` | Handler | Stan |
|---|---|---|
| 0x01 | `1:F788` | zaimplementowany |
| 0x02 | `1:FB58` | zaimplementowany |
| 0x10 | `1:FB0B` | zaimplementowany |
| 0x11 | `1:FE92` | zaimplementowany |
| 0x40 | `1:FC24` | zaimplementowany |
| 0x03, 0x12, 0x20, 0x30, 0x50, 0x80 | `LJMP 0x25E3` | pusty handler |
| 0x21, 0x60 | brak (wpis zerowy) | nie obsługiwane |

Podpolecenia (z rozgałęzień DEC/ADD w handlerach):

- **`10 04`** [I]: raport klawiatury 8 B (`1:F4D7`). **`10 08`** [I]: raport consumer 4 B (`1:F573`).
- **`40`** (`1:FC24`), dyspozycja po `sub` [O]:

| `sub` | Działanie | Adres |
|---|---|---|
| 0x01 | rozgłaszanie w trybie parowania: dla `data[0] < 4` zatrzymuje bieżące rozgłaszanie, ustawia bieżący slot, ustawia flagę trybu `[0x0759] = 1` i startuje rozgłaszanie (`2:CADE`, `2:C9FD`) | `1:FCB5` |
| 0x02 | zatrzymanie: w stanie "połączony" zrywa łącze, w przeciwnym razie wyłącza rozgłaszanie; bez argumentu slotu | `1:FD27` |
| 0x04 | połączenie: jak 0x01, ale flaga `[0x0759] = 0` i start `2:C93D` | `1:FD2D` |
| 0x05 | usuwa **wszystkie** bondy (`GAPBondMgr_SetParameter(0x409, 0)`); nie przyjmuje numeru slotu | `1:FD54` |
| 0x0F | test klawisza: wysyła raport HID ID 1 (8 B) z `data[2]` w bajcie klawisza | `1:FD19`, `1:F189` |
| 0x12 | zwraca 1 B: liczba bondów (`GAPBondMgr_GetParameter(0x40E)`) | `1:FDC0` |
| 0x13 | zwraca 10 B z lokalnego bufora na stosie, którego nikt nie wypełnia (niedokończona funkcja, może zwracać śmieci ze stosu) [I] | `1:FDA9` |
| 0x14, 0x15, 0x16 | zwracają po 2 B: 0x14 minimalny, 0x15 i 0x16 maksymalny interwał połączenia (GAPRole 0x311 i 0x312) | `1:FD7F`, `1:FD88` |
| 0x30 | zwraca 6 B: adres BD ze strony informacyjnej układu | `1:FCDA` |
| 0x31 | ustawia adres BD z 6 B danych (`HCI_EXT_SetBDADDR`, tylko do restartu) [I] | `1:FCE4` |
| 0x40, 0x41, 0x42 | testy RF produkcji (modem test, poziom mocy testu) [I] | `1:FDE5`-`1:FE41` |

  Nazwy parametrów GAP i HCI pochodzą z numeracji TI SDK (wiedza zewnętrzna); są poparte tablicą wartości domyślnych w obrazie.
- **`02 01`** [O]: dane `0x01` (bootload) dają potwierdzenie tylko nadawcy 1, pętlę opóźniającą, `CLR EA`, `WDCTL = 0xAB`, `WDCTL = 0x5B` i pętlę własną, czyli reset przez watchdog; reset następuje od dowolnego nadawcy. Dane `0x02` są **ignorowane**, chyba że nadawca to 1 (wtedy ack). Ping `02 01 02` od MCU (nadawca 3) nie robi więc nic w aplikacji BLE; ma sens dla bootloadera (`IapMode` = 2 w AnnePro2-Tools). `02 02` (`IapGetMode`) odsyła 2, tylko dla nadawcy 1.
- **`01 03`** [O]: odpowiedź `0x25` (wersja 2.05 w kodowaniu półbajtowym), obsługa `1:F7B2`. To właściwe żądanie, na które BLE reaguje w trybie aplikacji.
- **`01 05`** [O/I]: dane 16-bitowe LE to **surowy odczyt ADC 12-bit** z MCU. Napięcie: `U = raw * 2 * 3,1 / 4095`. Procent (zapis do XDATA 0x189F): poniżej 3,4 V 0; 3,4 V 10; 3,5 V 20; 3,6 V 30; 3,65 V 40; 3,7 V 50; 3,75 V 60; 3,8 V 70; 3,9 V 80; 4,0 V 90; od 4,15 V 100 (u MCU próg 100 to 4,1 V). Callback Battery Level (`1:F646`) zwraca ten bajt. Poziom w atrybucie GATT (XDATA `0x165B`) jest zapisywany **tylko wtedy, gdy nowy jest mniejszy od obecnego** (`3:D827`, `SUBB`/`JNC`), i wtedy wysyłane jest powiadomienie do centrali z włączonym CCCD (`3:DB10`) [O/I]. Skutek: po naładowaniu baterii poziom w GATT nie rośnie do restartu układu BLE. Fabryczny MCU nie wysyła tej ramki do BLE w przeczytanym kodzie [N].
- **`01 10 <id> <dane>`** [O] (`1:FADF`, `3:F4F4`): stała długość danych **32 B** niezależnie od długości ramki. `id 2` zapisuje bufor XDATA `0x179F`, czyli **DIS Serial Number (0x2A25)**; `id 0` zapisuje `0x0750` (System ID, bufor 8 B) i **nadpisuje 24 B za nim**, w tym zmienne slotu (`0x0758`) i trybu (`0x0759`), więc id 0 należy omijać; id 1 i 3-8 nic nie robią; id powyżej 8 daje błąd. Wartość domyślna serial to 32 znaki z `0x21DC`. Fabryczny MCU wysyła 32 znaki ASCII-hex UDID bez bajtu id, więc BLE odczytuje pierwszy znak (0x30-0x46) jako id większy od 8 i odrzuca ramkę; serial zostaje domyślny [I]. Weryfikacja: `01 10 02 <32 znaki>` i odczyt GATT po ponownym połączeniu.
- **`11 01`** [O/I] (`1:FE92`): ack, uzbraja wybudzenie P0.1 (`1:F656`), zatrzymuje timer zdarzenia 1 zadania aplikacji i startuje timer **500 ms** (`3:BCC9`). Po upływie zdarzenie 1 (`1:FF34`) ponownie uzbraja wybudzenie i zwalnia blokadę snu. To ramka podtrzymania aktywności.
- **Puste handlery** (`LJMP 0x25E3`): `cmd 0x03`, `0x12`, `0x20`, `0x30`, `0x50`, `0x80`; wpisy `0x21` i `0x60` są zerowe w tablicy dyspozytora. Ramka `50 07` z MCU jest więc ignorowana [O]. **`60 04` nie jest ignorowane:** klasyfikator `1:F416` pomija ramki z celem 2 i `cmd` 0x40, 0x10, 0x01, 0x02, 0x50 (obsługuje je dyspozytor), a pozostałe zamienia na raport HID: `60 04` staje się raportem **ID 4, input, 8 B** (`1:F1E8` do `2:BEE4`) [O]. ID 4 nie występuje w Report Map (tylko w atrybutach Battery), więc host najpewniej widzi to jako 8 B na Battery Level [I]. Fabryczny MCU wysyła `60 04` tylko dla myszy wirtualnej.

Zgodność z portem QMK (`40 01/04/05`, `10 04/08`, `02 01 01/02`) potwierdza interpretację. QMK to źródło zewnętrzne, nie wynik analizy pliku [I].

## 4. Zdarzenia BLE do MCU

Wszystko, co BLE wysyła do MCU, wychodzi czterema wywołaniami `1:F45A` plus odpowiedziami na żądania (`3:A326`) [O]. Bajt adresowy ramek to **`0x35`** (cel 3, nadawca 5): wszystkie cztery miejsca wywołania wkładają na stos programowy najpierw 5, potem 3, a konstruktor `3:9B8A` bierze wysoką połówkę z `[+0x11]` (3) i niską z `[+0x12]` (5) [O].

| Ramka | Miejsce i warunek | Znaczenie |
|---|---|---|
| `01 03 0x25` | `1:EF82`, raz po inicjalizacji (koniec `1:ED7E`), a także jako odpowiedź na żądanie `01 03` | zgłoszenie wersji [O] |
| `20 07 <0/1>` | `1:F225`/`1:F26F`, zapis raportu wyjściowego HID ID 1, bit 1 | stan Caps Lock hosta [O/I] |
| `20 0c 00` (dwa razy pod rząd) | `1:F388`, `1:F3AB`, w callbacku stanu GAP `1:F346`, tylko gdy nowy stan to 6 ("połączony") | wejście w stan połączenia [O]; przy rozłączeniu ramki nie ma |

Raporty HID do hosta: funkcja `2:BEE4` odrzuca długość od 19 wzwyż i odpowiada przez `HidDev_Report`. Wołają ją `1:F72C` (ID 2, 18 B), `1:F4E5` (ID 1, 8 B) i `1:F5A0` (ID 3, 4 B) [O]. Routing wyjść (`3:A256`): cel 2 trafia do raportu HID ID 2 (`1:F71E`), cele 1, 3 i 4 wychodzą UART-em, cel 5 jest lokalny [O]. To potwierdza, że 18 B raportu to 10 B nagłówka plus 8 B fragmentu. Ścieżka odwrotna (zapis raportu ID 2 od hosta) nie została prześledzona [N].

## 5. Stos BLE: usługi, HID, GAP

UUID (tablica pod offsetem 0x6300, czyli `1:8300`) [O]:
- usługi: GAP 0x1800, GATT 0x1801, Device Information 0x180A, Battery 0x180F, **HID 0x1812**, Scan Parameters 0x1813;
- HID: charakterystyki 0x2A4A, 0x2A4B, 0x2A4C, 0x2A4D, 0x2A4E, 0x2A22, 0x2A32 oraz deskryptory 0x2902, 0x2907, 0x2908;
- brak UUID 0x2A33 (boot mouse). UUID 128-bitowych nie przeszukiwałem wyczerpująco.

Report Map (offset 0x6000, `1:8000`, 149 bajtów) po zdekodowaniu [O]:

| Report ID | Zawartość |
|---|---|
| 1 | klawiatura: 8 bitów modyfikatorów, bajt zarezerwowany, 5 diod LED (wyjście) i 3 bity dopełnienia, 6 kodów klawiszy w zakresie 0..0x65 |
| 2 | strona producenta 0xFF00: wyjście i wejście po 18 B, wartości logiczne -128..255. Kanał niestandardowy; najpewniej nosi ramki konfiguracji z hosta po BLE (18 B to 10 B nagłówka plus 8 B danych fragmentu) [I] |
| 3 | consumer: Mute 0xE2, Vol Up 0xE9, Vol Down 0xEA, Play/Pause 0xCD, Next 0xB5, Prev 0xB6, 0x6F i 0x70 (jasność) i 3 bajty dopełnienia; układ zgadza się z mapowaniem QMK |

Nie ma deskryptora myszy [O].

Dane GAP [O]:
- nazwa `AnnePro2`: 4 kopie w rekordach odpowiedzi skanowania (offsety 0x2E8, 0x2F8, 0x308, 0x318) i 4 kopie w atrybucie nazwy (0x616E, 0x6177, 0x6180, 0x6189). Cztery kopie odpowiadają czterem gniazdom urządzeń (numer gniazda 0..3 w poleceniu `40`) [I];
- odpowiedź skanowania: `05 12 09 00 09 00` (zakres interwału 9..9, czyli 11,25 ms) i `09 09 'AnnePro2'`, 4 rekordy od offsetu 0x2E0;
- rozgłaszanie: Appearance `03 19 c1 03` (0x03C1, klawiatura HID), Flags `02 01 05`, UUID `03 03 12 18`, Tx Power `02 0a 00`, LE Role `02 1c 00`, krótka nazwa `04 08 41 50 32` ("AP2"), niekompletna lista UUID `05 02 12 18 0f 18`;
- Device Information: producent `HEXCORE` (0x61AA), wersja `1.2.0` (0x61B2), model `KB181` (0x61C4); ciąg `experimentalBLE-1.5.0` pod 0x6158. "HEXCORE" to napis producenta w usłudze DIS, a nie nazwa układu;
- adres BLE: `1:F4EF` kopiuje 6 B ze strony informacyjnej układu (adres IEEE, 0x780E-0x7813). Adres nie jest zaszyty w obrazie [O/I];
- bajty pod 0x6196 (`78 69 e0 ab 50 c3 f0 d2 01 2a`): przeznaczenie nieustalone [N];
- tablica domyślnych parametrów GAP pod offsetem 0xD3 (39 słów 16-bitowych, kopiowana do XDATA `0x1696`) zawiera wartości domyślne TI [O]. Wcześniejsze dopasowanie danych 0x90-0x1B8 do HidEmuKbd było błędne.

## 6. Parowanie, sloty, rozgłaszanie, połączenie

Inicjalizacja GAPRole i GAPBond to `1:ED7E`. Wartości odczytane z kodu [O], nazwy parametrów z TI SDK (wiedza zewnętrzna):

| Parametr | Wartość | Adres |
|---|---|---|
| `GAPBOND_PAIRING_MODE` (0x400) | 1: czekaj na żądanie | `1:EE9E` |
| `GAPBOND_MITM_PROTECTION` (0x402) | 0 | `1:EEAB` |
| `GAPBOND_IO_CAPABILITIES` (0x403) | 3: brak wejścia i wyjścia (Just Works) | `1:EEB9` |
| `GAPBOND_BONDING_ENABLED` (0x406) | 1 | `1:EEC7` |
| `GAPBOND_DEFAULT_PASSCODE` (0x408) | 0 | `1:EE90` |
| `GAPBOND_AUTO_SYNC_WL` (0x40D) | 0: bez white listy | `2:BD9B` |
| `GAPROLE_ADV_FILTER_POLICY` (0x30D) | 0 przy każdym starcie rozgłaszania | `2:C979`, `2:CA39` |

Nie ustawiono `KEY_DIST_LIST`, `KEYSIZE`, `OOB` ani `BOND_FAIL_ACTION` (zostają domyślne) [N]. Nie znalazłem LE Secure Connections ani rozgłaszania kierowanego [I].

**Sloty** [O/I]:
- Sloty to indeksy bondów. Bieżący slot to `[0x0758]`. Rekordy rozgłaszania i odpowiedzi skanowania są identyczne bajt w bajt dla wszystkich slotów, w tym ten sam adres BD ze strony informacyjnej. Hosta nie odróżnia się więc po adresie ani nazwie.
- Slot jest zapisywany w NV `0x81` (10 B, bajt 2 to slot); `1:9C87` czyta, w razie niepowodzenia wypełnia `0xFF` i zapisuje; `1:9BE6` odczytuje przy starcie (akceptuje 0..9).
- Przy nawiązaniu połączenia (`2:9B16`, `1:884C` zwraca indeks bonda peera, 10 lub więcej oznacza nieznany): w trybie połączenia (`[0x0759] = 0`) dopuszczony jest tylko bond o indeksie równym slotowi, inny peer jest rozłączany i rozgłaszanie jest wznawiane (`2:9BE7`). W trybie parowania (`[0x0759] = 1`) nieznany peer jest akceptowany.
- `40 05` usuwa wszystkie bondy, nie bond slotu.

**Rozgłaszanie i połączenie** (wartości z tablicy GAP; jednostka interwału 0,625 ms) [O]:

| Tryb | Gdzie | Interwał | Limit czasu rozgłaszania |
|---|---|---|---|
| parowanie (`40 01`) | `2:C9FD` | 20 ms | 60 s |
| połączenie (`40 04`) | `2:C93D` | 20 ms | 5 s |
| powrót po rozłączeniu | `2:C99D` | około 1022,5 ms | bez limitu |

- Powrót po rozłączeniu (`2:C3DD` z `2:C45C`) następuje, gdy liczba bondów jest niezerowa i ustawiony jest bit 1 flagi konfiguracji [I]. Flagi AD to `0x05` (limited discoverable).
- Po upływie limitu rozgłaszanie się nie wznawia (`GAPROLE_ADVERT_OFF_TIME` = 0), a MCU nie dostaje żadnej ramki. MCU musi wysłać `40 01` lub `40 04` ponownie [O/I].
- Naciśnięcie klawisza bez połączenia: `HidDev_Report` (`2:BEE4`) samo uruchamia rozgłaszanie: przy niezerowej liczbie bondów jak `C93D` (5 s), przy zerze jak `C9FD` (60 s); raport trafia do kolejki [O].
- Parametry połączenia (`1:EE19`-`1:EE57`): aktualizacja parametrów włączona, interwał 9 jednostek po 1,25 ms (11,25 ms), latency 15, limit czasu 4 s; po wejściu w stan połączony ustawiane jest żądanie aktualizacji parametrów [O].
- Moc nadawania: w kodzie nie ma `HCI_EXT_SetTxPower`; poziom zmienia się tylko w testach RF [O].

## 7. Pamięć trwała, zasilanie, aktualizacja

- Pamięć trwała to SNV z biblioteki TI, ID w zakresie `0x02`-`0x04` i `0x20`-`0x5B` oraz element aplikacji `0x81` [O/I]: lokalne IRK i SRK (16 B), licznik podpisu, a dla każdego bonda `i` (0..9) rekordy `0x20 + 6*i` (14 B), `0x21 + 6*i` (27 B, LTK), `0x22 + 6*i` (18 B), `0x23`/`0x24 + 6*i` (16 B), `0x25 + 6*i` (4 B). Procedury: `4:8E19` (zapis przez DMA do FWDATA), `4:8E83` (kasowanie strony), `3:8AC3` (kontrola skasowania).
- Zarządzanie energią to standardowy OSAL TI z maską blokad snu w `0x15CD` (`4:90B3`): przerwanie P0.1 ustawia zdarzenie 2 i blokuje sen, `11 01` odnawia 500 ms, zdarzenie 1 zwalnia blokadę i uzbraja wybudzenie [O/I]. Kod snu: `0:3A92`-`0:3B40` i `0:7C76`-`0:7D30` (SLEEPCMD, kod z RAM).
- P0.0 i P0.1 to wejścia z odszumianiem (aktywne w stanie niskim); callback `4:9046` przełącza flagę `0x0D7D` [O/I]. Który pin MCU napędza P0.1, nie wiem [N].
- Aktualizacja: mechanizm leży w bootloaderze spoza obrazu; po `02 01 01` układ resetuje się watchdogiem do wektora 0, a dalej działa bootloader [O/N].
- Odbiornik (`3:9E71`) nie sprawdza bajtów 6 i 7 ani długości, nie ma sumy kontrolnej; `3:9F96` czyta `len - 2` bajtów bez górnej granicy w kodzie [O]. Fragmenty typu 0x10: `3:9F3F` i `3:A019` odrzucają fragment o złym indeksie, identycznie jak MCU.
- Suma kontrolna obrazu: pole 16 B pod 0x90 przypomina nagłówek TI OAD, ale test CRC16-CCITT dał tylko przypadkowe trafienie, więc sumy nie potwierdzam [N].

## 8. Wyniki trzeciej rundy (uzupełnienie)

- **Kanał ID 2 od hosta** [O]: zapis GATT do raportu output (0x2A4D) trafia do callbacku `1:F2B1`, potem `1:F225`: id 1 wyciąga bit Caps Lock i wysyła `20 07`; **id 2 kolejkuje bajty do bufora cyklicznego XDATA `0x0C60`** (`3:F288`, 0x100 B) i ustawia zdarzenie OSAL `0x0080`. Ten sam bufor zasila handler zdarzeń UART (`1:F3BD`), więc ramki od hosta po BLE trafiają do tego samego parsera co ramki z UART (pętla `1:F0A0`-`1:F13D`). Odpowiedzi dla węzła 2 idą routerem `3:A256` do raportu ID 2 input (`1:F72C`, 18 B). BLE nie składa fragmentów typu 0x10; przekazuje ramki bez zmian [O/I].
- **Parametry połączenia** [O/I]: lokalnie ustawiane GAPRole 0x310 = 1, 0x311 = 9, 0x312 = 9, 0x313 = 15, 0x314 = 400 (`1:EDC5`-`1:EE57`), czyli żądanie aktualizacji (11,25 ms, latency 15, 4 s). PPCP w GATT (100-200 ms, latency 0, 10 s) to osobna, statyczna wartość. Co host zaakceptuje, pokaże dopiero `btmon`.
- **Bateria:** `3:D773(1, 1, &0x0A)` ustawia próg krytyczny baterii (10) w `0x0288`. Poziom jest sprawdzany w stanie połączonym (`2:C5D6` wywołuje `3:D827`, gdy `[0x0B55] == 6`) i timer jest odnawiany z okresem **15 000** (stała 0x3A98 pod XDATA `0x82DA`, czyli stała z flash widziana przez okno XDATA; jednostka OSAL to ms, więc 15 s) [O/I, runda Ghidry]. Obok leży stała zmiennoprzecinkowa 3,1 ze wzoru napięcia.
- **Odczyt raportu output ID 2** [O/I]: wartość raportu leży w XDATA `0x0CF7`, a zapis hosta idzie do innego bufora (`0x0C60`). Odczyt zwraca więc bajty sąsiadujące w XDATA, a nie zdefiniowaną zawartość. To wygląda na odsłanianie pamięci, nie na funkcję; czy znajdują się tam dane wrażliwe (np. materiał kluczowy), nie ustalono [N]. Nie udostępniaj wyniku takiego odczytu.
- **`40 13`** może zwracać śmieci ze stosu (patrz tabela podpoleceń) [I].
- **`oLoLoL...`** (CPU `0x21B9`-`0x21DB`) nie ma żadnych odwołań w kodzie; wygląda na wypełnienie lub nieużywane dane [O/N].
- **Scan Parameters** (0x2A4F, 0x2A31): standardowa usługa TI (`4:81C0`-`4:82F0`), nie połączona z parametrami rozgłaszania ani połączenia [I, płytkie sprawdzenie].
- **HID Control Point** (0x2A4C): przyjmuje tylko 0 (suspend) i 1 (exit suspend), inna wartość daje błąd `0x80` [O]. `0x0B5E` to wskaźnik na strukturę callbacków podaną do `HidDev_Register` (`2:BEB1`, wołane z `1:EF15` z adresami XDATA `0x1948` i `0x194D`); w `2:C372` wywoływane jest jej pole +2, czyli `evtCB` biblioteki TI. Control Point przekazuje zdarzenie 0 lub 1, a Protocol Mode (0x2A4E, tylko 1 bajt, wartość 0 lub 1) zapisuje tryb i przekazuje zdarzenie 2 (boot) lub 3 (report) [O, poprawka: wcześniej przypisałem `2:C372` tylko do Control Point]. Kod funkcji `evtCB` w aplikacji nieustalony [N].
- **`40 12`** zwraca liczbę bondów (1 B); `40 14`-`40 16` interwały połączenia; HCI `0xFC20` (`3:FC4A`, parametry `20 04 07`) to nierozpoznane polecenie vendor TI [N].

## 9. Niewiadome

1. Poziom początkowy `0x165B` (okres timera baterii ustalony: 15 s, sekcja 8).
2. Kod funkcji `evtCB` w strukturze callbacków HID (`0x0B5E` wskazuje na tę strukturę, sekcja 8).
3. Czy bufor `0x0C60` ma osobnych konsumentów dla UART i dla hosta.
4. Czym jest HCI `0xFC20`.
5. Zawartość XDATA `0x0CF8`-`0x0D06` (źródło 16 B z odczytu ID 2).
6. Czy host rzeczywiście dostaje `60 04` jako powiadomienie na Battery Level.
7. Przeznaczenie `oLoLoL...`; `40 0F` i wiązanie `40 12` z NV.
8. Parametry stosu poza ustawionymi (`KEY_DIST_LIST`, `KEYSIZE`, polityka błędów parowania).
9. Bootloader i IAP (poza obrazem), suma kontrolna obrazu, wariant CC2540 czy CC2541, wartość indeksu baudrate, pin MCU napędzający P0.1, wartość jednostki ADC.
