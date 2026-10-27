# Metoda i weryfikacja

Znaczniki i konwencje: `README.md` §2. Skrypty i wyniki z katalogu `analiza/` wymienione niżej leżą w lokalnym katalogu analizy autora, poza forkiem.

## 1. Zakres i zasady

- Analiza statyczna trzech obrazów fabrycznych i ObinsKit 1.2.11. Obrazów fabrycznych nie uruchamiano ani nie wgrywano; na klawiaturę wgrywano wyłącznie buildy portu QMK (`08-testy-na-sprzecie.md`).
- Obrazy pochodzą z repozytorium społeczności `dchang0/AnnePro2Firmware` (commit `2727333999bf0cf108b6247d3194a6b4679bbb40`, katalog `StockFirmware/`); sumy SHA-256 plików pobranych 2026-10-27 z tego commita są równe sumom plików w projekcie (`README.md` §1). Zgodności z wydaniami producenta nie sprawdzano.
- Każde twierdzenie ma znacznik [O], [I] albo [N]; twierdzenia oparte na źródłach zewnętrznych mają to zaznaczone w tekście.

## 2. Narzędzia

| Cel | Narzędzie |
|---|---|
| Dekompilacja i listingi MCU, LED i BLE | Ghidra 12.1.4 (headless, JDK 21) ze skryptami z `analiza/ghidra/` (§4) |
| Deasemblacja Thumb | `arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb --adjust-vma=0x4000 <obraz>` |
| Dane RW MCU i LED | `analiza/py/keil_lz.py` (§5) |
| Nazwy rejestrów HT32 | `analiza/py/ht32_labels.py`, z nagłówków `ht32f523x2.h` i `ht32f523x2_reg.h` z `qmk/chibios-contrib` (commit `5a9ad82b`, submoduł forka QMK) |
| Nazwy SFR i XREG CC2541 | `analiza/py/cc254x_labels.py`, z nagłówka TI `ioCC2541.h` (kopia z `github.com/michaellin/IMU-Project`), użytego tylko jako źródło nazw |
| Kontrola twierdzeń bajtowych | `analiza/py/sprawdz_bajty.py` |
| Krzywa baterii portu względem BLE | `analiza/py/bateria_korekta.py` |
| Rejestr twierdzeń | `analiza/py/rejestr.py`, wynik w `analiza/rejestr-twierdzen.md` |
| Kontrola odnośników i tabel dokumentacji | `analiza/py/sprawdz_odnosniki.py` |
| ObinsKit | własny czytnik `app.asar`, `objdump` i `strings` dla `HID_hidraw.node` (`06-obinskit-i-bootloader.md` §1) |
| Port QMK i Tools | `git`, `make annepro2/<rewizja>:<keymapa>`, `cargo test --release --offline` |
| Źródła zewnętrzne | instrukcja Anne Pro 2 (kopia w ManualsLib), datasheety TI CC2540 (Rev. F) i CC2541 (Rev. D), datasheet Holtek HT32F52342/52352 (Rev. 1.60), `openannepro.github.io/ap2_revisions`, repozytorium `OpenAnnePro/AnnePro2-Hardware` (schemat); dostęp 2026-10-27 |

REA 6.5.0, którego używano w pierwszej rundzie, importuje surowe obrazy tylko jako ARM 32-bit (bez Thumb i Cortex-M) i nie obsługuje 8051, więc do tych obrazów się nie nadaje (relacja z wcześniejszej rundy, w tej weryfikacji REA nie uruchamiano).

## 3. Jak powtórzyć analizę

```bash
analiza/ghidra/uruchom.sh            # import i eksport wszystkich trzech obrazów (albo: mcu, led, ble)
python3 -I analiza/py/sprawdz_bajty.py
python3 -I analiza/py/rejestr.py
python3 -I analiza/py/sprawdz_odnosniki.py docs
```

Zmienne `GHIDRA`, `JAVA_HOME` i `GHIDRA_PROJ` wskazują instalację Ghidry, JDK 21 i katalog projektu Ghidry (domyślnie nowy katalog tymczasowy). Wyniki trafiają do `analiza/eksport/<obraz>/`: `procedury.tsv` (adres, nazwa, rozmiar, cel thunka, miejsca wywołań), `pseudokod.c`, `listing.txt` (instrukcje z odwołaniami; przy odczytach pamięci zainicjalizowanej ARM dopisana wartość), `ghidra.log`, a dla MCU i LED także `rw_*.bin` i `wskazniki.txt`, dla BLE `stuby.txt`.

## 4. Import w Ghidrze

- MCU i LED: `BinaryLoader` z bazą 0x4000, język `ARM:LE:32:Cortex`. Skrypt `ApArmSetup.java` dodaje blok bootloadera 0x0000-0x3FFF (bez danych), SRAM 16 KiB z rozpakowanymi danymi RW, peryferia od 0x40000000, rdzeń od 0xE0000000, 384 nazwy rejestrów HT32, tryb Thumb dla całego flash i procedury z 48 wektorów. Po analizie `ApArmSeedPointers.java` zakłada procedury wskazywane tylko z danych RW (np. tablica poleceń, procesy Contiki), jeśli pierwsza instrukcja to PUSH albo BX LR.
- BLE: `BinaryLoader` dla plik+0x0000-0x5FFF pod CODE 0x2000, język `8051:BE:16:default`. `ApBleSetup.java` dodaje banki 1-4 jako nakładki pod 0x8000, bloki XDATA (RAM, XREG, odbicie SFR, strona informacyjna), nazwy SFR i XREG CC2541 oraz 830 stubów trampoliny jako thunki do celów w bankach.
- Eksport `ApExport.java`: lista procedur, pseudokod wszystkich procedur bez thunków i listing.
- Wynik z 2026-10-27: MCU 647 procedur, LED 291, BLE 2911 (w tym 830 thunków); wszystkie dekompilacje udane. Pseudokod 8051 jest mało czytelny, więc BLE weryfikowano głównie listingiem.

## 5. Dane RW obrazów MCU i LED

Oba obrazy zbudowano kompilatorem Keil (armcc) z kompresją danych inicjalizowanych. Tablica scatter-load ma wpisy po 16 B: pierwsze słowo to przesunięcie procedury względem adresu wpisu, a pozostałe trzy słowa czyta sama procedura [O]:

| Obraz | Wpis | Procedura | Słowa |
|---|---|---|---|
| MCU | `0xf344` | `0xe522` (zerowanie) | rozmiar 0xc9c, cel `0x20000714`, 0 |
| MCU | `0xf354` | `0xf1c6` (dekompresja) | źródło względne (`0xf4b6`), długość·2 (0x205 B), cel `0x20000000` |
| LED | `0xba58` | `0xb9f4` (zerowanie) | rozmiar 0xa50, cel `0x20002a60`, 0 |
| LED | `0xba68` | `0xb938` (dekompresja) | źródło `0xbb54`, 0x100 B, cel `0x20001000` |

Dekompresor odtworzony z `0xf1c6` (każdy blok zaczyna się bajtem sterującym `b`):

```python
def keil_lz_unpack(d, src, n):          # d: bajty obrazu, src: offset skompresowanych danych, n: ich długość
    out, p, end = bytearray(), src, src + n
    while p != end:
        b = d[p]; p += 1
        lit = b & 3                      # liczba bajtów literalnych + 1
        if lit == 0:
            lit = d[p] + 3; p += 1
        ml = b >> 4                      # długość dopasowania - 2
        if ml == 15:
            ml = d[p] + 15; p += 1
        out += d[p:p + lit - 1]; p += lit - 1
        if ml == 0:
            continue
        low = d[p]; p += 1
        hi = (b >> 2) & 3
        if hi == 3:
            hi = d[p]; p += 1
        off = (hi << 8) + low
        for _ in range(ml + 2):
            out.append(out[-off])
    return bytes(out)
```

Wynik: MCU 0x711 B, LED 6749 B. W danych MCU struktury procesów wskazują nazwy procesów i ich funkcje, a tablica poleceń ma 13 wpisów z poprawnymi wskaźnikami Thumb [O].

## 6. Raport weryfikacji (2026-10-27)

Każde twierdzenie dokumentacji sprzed przebudowy (wersja z commitu `2e37abad5e` tego forka) sprawdzono ponownie u źródła, w czterech obszarach: MCU i LED (eksport Ghidry, objdump, bajty), BLE (listing Ghidry z rozwiązanymi stubami, bajty), ObinsKit i źródła zewnętrzne, port QMK z narzędziami (kod, `git`, buildy, testy). Wyniki z dowodami są w `analiza/wyniki/*.md`, a zestawienie z miejscem każdego twierdzenia w nowej dokumentacji w `analiza/rejestr-twierdzen.md`.

| Obszar | Potwierdzone | Poprawione | Obalone | Nieaktualne | Niesprawdzalne | Razem |
|---|---|---|---|---|---|---|
| MCU i LED | 316 | 30 | 2 | 0 | 15 | 363 |
| BLE | 223 | 42 | 12 | 0 | 13 | 290 |
| ObinsKit i źródła zewnętrzne | 133 | 14 | 4 | 0 | 8 | 159 |
| port QMK, Tools, obraz do wgrania | 55 | 11 | 5 | 5 | 8 | 84 |
| razem | 727 | 97 | 23 | 5 | 44 | 896 |

### 6.1. Twierdzenia obalone

| ID | Było | Jest |
|---|---|---|
| M-163 | wybudzenie klawiszem wysyła do BLE `11 01` | `11 01` idzie tylko po wybudzeniu przez RTC, czyli po upływie limitu snu (`03-mcu.md` §7) |
| M-298 | kody 0xA5-0xA7 sterują systemem | nie mają w MCU 2.36 żadnego działania |
| B-017, B-172 | po rozłączeniu BLE samo wznawia rozgłaszanie, jeśli ma bondy | nigdy: hidFlags = 0x01, procedura `2:C99D` jest nieosiągalna; rozgłaszanie wznawia klawisz albo `40 01`/`40 04` |
| B-033, B-119, B-237 | BLE zamienia `60 04` na raport HID ID 4 | kod konwersji jest martwy, `60 04` jest ignorowane |
| B-041 | BLE wysyła do MCU `02 01 <stan>` | aplikacja BLE 2.05 tej ramki nie wysyła |
| B-214 | `oLoLoL…` to nieużywane wypełnienie | to wartość początkowa bufora RAM używanego przez kod warstwy łącza |
| B-220, B-225, B-283 | funkcja evtCB HID nieustalona | to pusta funkcja `1:F343`; trzecie pole struktury (`1:F346`) wysyła `20 0c` |
| B-223 | początkowy poziom baterii w BLE nieznany | 100 % |
| B-228 | 16 B odczytu raportu ID 2 pochodzi z `X:0CF8`-`X:0D06` | 1 B z `X:0CF7` i 15 B niezapisanego bufora odpowiedzi ATT |
| O-019 | schemat leży w `qmk_firmware/keyboards/annepro2/` | źródło to `OpenAnnePro/AnnePro2-Hardware` |
| O-032 | domyślna baza 0x4000 w AnnePro2-Tools dotyczy tylko MCU i LED | dotyczy wszystkich trzech celów, zgodnie z odczytem bootloadera |
| O-049 | QMK nie ma odpowiednika `KC_BL_BATT` | ma `KC_AP2_BATT` |
| O-085 | dokumentacji Holtek nie ma w sieci | datasheet HT32F52342/52352 jest publiczny |
| P-014 | patche stosuje się w `qmk_firmware` | fork to `qmk_firmware_annepro2`, a zmiany są w commitach |
| P-029 | przy parowaniu port nie przełącza raportów na BLE | przełącza (od `4c87841d68`) |
| P-053 | `KC_AP2_BATT` nie jest przypisany w keymapie | jest na `Fn2` + `8` |
| P-059 | port wysyła zbędny bajt `0x00` po numerze slotu | już nie (od `4c87841d68`) |
| P-069 | `AP2_BT_SWITCH` nie jest włączone w żadnej keymapie | jest włączone w `Kazmir_BO` |

### 6.2. Najważniejsze poprawki

| ID | Było | Jest |
|---|---|---|
| B-150 | bajty `78 69 e0 ab…` pod plik+0x6196 | pod plik+0x619A |
| B-277 | 764 stuby do banków 1-4 i 37 do banku 0 | 793 i 37, razem 830; liczba 764 nie wynika z obrazu |
| M-345 | tablica poleceń LED ma 8 wpisów | 12 wpisów, 4 z procedurami |
| M-117 | nazwy procesów pod `0xf363`-`0xf49c` | pod `0xf364`-`0xf49d` |
| M-187, M-248 | wzór MCU dzieli przez 4094 | przez 4095 |
| B-101 | procent baterii w BLE to schodki | interpolacja liniowa między progami |
| M-047 | kolejki nadawcze po 256 B | po 512 B |
| M-102, M-105, B-253 | MCU wysyła do BLE 16 B UDID, tylko przy wersji co najmniej 0x100, i być może ustawia nim numer seryjny | zawsze 32 znaki hex; BLE je odrzuca |
| M-053 | typ `0x12` to ramka pojedyncza | też bywa fragmentowany (bit 7 bajtu 3) |
| M-162 | dopasowanie RTC wyzwala uśpienie | DeepSleep zaczyna się po około 1 s bezczynności, a RTC po limicie snu wybudza i przełącza w długi sen |
| M-165 | PC3 steruje macierzą | kod nie wiąże PC3 z macierzą; raczej układ LED [I] |
| M-143, M-211 | `0x7ab4`, `0x7b3c`, `0x7bb4` to przerwania | to odczyty pinów w procesach; przerwania to `0xea4e` i `0xeaf6` |
| M-131 | Daemon ma dwa liczniki | trzy etimery, wszystkie wyzwalane od razu |
| M-033 | MCU skacze do bootloadera | zapisuje sygnaturę i resetuje się |
| M-279 | `50 01`-`50 06` | `50 04` nie istnieje |
| M-284, M-321 | LED zawsze odsyła `20 03 ff <tryb>`; MCU co 100 ms wysyła `20 16` dla efektu 26 | odpowiedź zależy od nadawcy; cykliczne `20 16` tylko przy zapamiętanym trybie 0x1a |
| M-334 | 15 identyfikatorów JEDEC | 15 wpisów, 14 różnych |
| M-337 | PID 0xA293 | domyślnie, a 0xA297 przy bajcie +3 rekordu równym 4 |
| M-358 | port odtwarza przełącznik oryginału z odszumianiem | oryginał nie odszumia i ustawia wyjście przy każdym odczycie |
| B-003 | układ BLE z rodziny CC253x | CC2540 albo CC2541 (CC254x) |
| B-034, B-114 | `11 01` wstrzymuje sen na 500 ms | odsuwa o 500 ms zwolnienie blokady, której samo nie zakłada |
| B-069 | indeks prędkości UART nieznany | indeks 4, 115200, bez kontroli przepływu |
| B-082 | `40 01` zatrzymuje rozgłaszanie | zrywa też istniejące połączenie; slot ≥ 4 jest ignorowany bez odpowiedzi |
| B-142, B-145, B-146, B-164 | dane rozgłaszania i odpowiedzi skanowania wszystkich slotów są identyczne | slot 0 ma inny układ niż sloty 1-3 |
| B-179, B-181 | rekordy SNV bonda: 0x22+6i ma 18 B | 27 B; dochodzą rekordy 0x70+i po 18 B |
| B-211 | odczyt raportu ID 2 zwraca sąsiednie XDATA | 1 B i 15 B bufora ATT |
| B-273 | treść BLE ma 128 128 B | 128 129 B (0x1F480 to offset ostatniego bajtu) |
| O-015, O-018 | schemat C18; `UART_LED` na PB0 i PB1 | schemat rysuje wariant 48-pinowy (C15); etykietę ma tylko PB0 |
| O-051, O-066, O-078-O-082, O-086, O-088, O-100, O-106, O-114 | adresat `readIapMode` to MCU; makra `30 03`-`30 0a`; kilka nazw nieustalonych | adresat z argumentu; `writeMacro` = `30 01`, `downloadMacro` = `30 05`; `readDeviceType` = `01 01`, `readFirmwareVersion` = `01 03`, `writeSleepTimeout` = `01 0f`, `writeIapMemory` = `02 31`, `writeEraseMemory` = `02 43`, `downloadKbdLayout` = `10 01`; nazwy klawiszy HID do 0xA1 |
| P-022 | port powtarza handshake według harmonogramu oryginału | 10 razy częściej (50 ms, potem 500 ms) |
| P-065 | korekta baterii daje 10 % od 3,25 V | 10 % od 3,2 V, dalej liniowo |
| P-078 | obraz do wgrania z `2e37abad5e` i niezacommitowanej zmiany | z commitu `6b71633911` (identyczny build) |
| P-002 | nic nie wgrywano na klawiaturę | nie wgrywano obrazów fabrycznych; buildy QMK tak |
| B-021-B-025 | niewiadome z README: okres timera baterii, `0x0B5E`, `40 13`, `40 16`, `60 04` | rozstrzygnięte |

### 6.3. Niesprawdzalne

44 twierdzenia nie dały się sprawdzić u źródła: relacje z testów na sprzęcie i zrzuty ekranu użytkownika, testy z wcześniejszych sesji, których nie zachowano (parser na komputerze, test 4096 wartości ADC, pary plik/cel w Tools), działanie bootloaderów spoza obrazów, tożsamość schematu z plikiem używanym we wcześniejszej rundzie, liczby z poprzedniego przebiegu Ghidry (644 procedury, 398 nazw, 764 stuby) oraz twierdzenia o REA. W nowej dokumentacji mają oznaczone źródło albo trafiły do `10-niewiadome.md`.

## 7. Ograniczenia

- Nie przechwytywano ruchu UART ani HID; testy na klawiaturze potwierdzają działanie funkcjonalnie, nie bajt po bajcie. Najtańsze potwierdzenie: podsłuch UART (`08-testy-na-sprzecie.md` §3.3).
- Nazwy funkcji i parametrów z TI BLE SDK są wiedzą zewnętrzną; wartości i numery pochodzą z kodu.
- Część funkcji rozpoznano po kontekście (np. arytmetyka zmiennoprzecinkowa w MCU, `atan2`); takie miejsca mają znacznik [I].
- Wnioski o martwym kodzie w BLE (`60 04`, zdarzenie 0x0008) opierają się na komplecie stałych wywołań `osal_set_event` i `osal_start_timerEx`; wywołanie pośrednie przez wskaźnik obaliłoby je [I].
- Schemat płytki C18 nie jest dostępny, więc role części pinów zostają nieustalone.
