# Metoda, narzędzia, dowody i ograniczenia

## 1. Zakres i zasady

- Analiza statyczna trzech obrazów firmware. Żaden obraz nie był uruchamiany, nie wgrywano niczego na klawiaturę.
- Rozróżnienie: **[O]** obserwacja (bajty, instrukcje), **[I]** inferencja (wniosek z obserwacji), **[N]** niewiadoma.
- Pliki wejściowe (SHA-256 w `01-architektura.md`): `key-c18-2.36.3.bin`, `rgb-c18-2.33.2.bin`, `ap2_c18_0205_ble.bin`. Pierwsze dwa pochodzą z repozytorium społeczności `dchang0/AnnePro2Firmware` (commit `2727333999bf0cf108b6247d3194a6b4679bbb40`); autentyczności względem producenta nie weryfikowałem.

## 2. Narzędzia

| Cel | Narzędzie |
|---|---|
| Deasemblacja ARM (Thumb) | `arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb --adjust-vma=0x4000 <obraz>` |
| Deasemblacja 8051 | własny deasembler w Pythonie (wątek BLE, patrz `04-uklad-ble-8051.md`) |
| Rozpakowanie danych RW | własny skrypt Python (algorytm z sekcji 3) |
| Adnotacja stałych literałów | własny skrypt Python: do każdej instrukcji `ldr rX, [pc, #n]` dopisuje wartość z puli literałów |
| Ekstrakcja pól ramek | własny skrypt Python: śledzi stałe w `r0`-`r3` przed wywołaniami funkcji budujących ramki |
| REA | otwarcie `key-c18-2.36.3.bin` jako `raw-binary` ARM pod 0x4000 potwierdziło identyfikację pliku (SHA-256). REA 6.5.0 importuje surowe obrazy tylko jako ARM 32-bit (bez Thumb i Cortex-M) i nie obsługuje 8051, więc do dekompilacji nie nadaje się dla tych obrazów |
| Ghidra 12.1.4 (headless, runda z 2026-10-26) | MCU i LED jako `ARM:LE:32:Cortex` z mapą pamięci (flash, SRAM z rozpakowanymi danymi RW, peryferia, rdzeń) i nazwami rejestrów HT32; BLE jako `8051:BE:16:default` z bankami 1-4 jako nakładkami i rozwiązanymi stubami trampoliny `0:25E6`. Eksport pseudokodu wszystkich funkcji i listingów z celami wywołań; wyniki w `08-weryfikacja-i-nowe-ustalenia.md` |
| Źródła zewnętrzne (tylko do porównania) | nagłówek `ht32f523x2.h` z `qmk/chibios-contrib` (commit `5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767`), katalog `keyboards/annepro2` z `qmk/qmk_firmware` (commit `7a1bbf37c5c07da4ea0a162bb139083a46ef40a1`) |

Wnioski z kodu źródłowego QMK są oznaczone jako zewnętrzne. Ramki BLE z `annepro2_ble.c` potwierdzają format zaobserwowany w fabrycznym MCU, ale nie służyły jako jedyny dowód.

## 3. Rozpakowanie danych RW obrazu MCU

Obraz jest zbudowany kompilatorem Keil (armcc) z kompresją danych inicjalizowanych. Tablica scatter-load pod `0xf344` zawiera dwa wpisy [O]:

1. zerowanie BSS: 0xc9c bajtów od `0x20000714`;
2. dekompresja z `0xf4b6` (długość skompresowana 0x205 B) do `0x20000000`, handler `0xf1c6`.

Dekompresor `0xf1c6` odtworzyłem z deasemblacji. Algorytm (każdy blok zaczyna się bajtem sterującym `b`):

```python
def keil_lz_unpack(d, src, n):          # d: bytes obrazu, src: offset skompresowanych danych, n: ich długość
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

Wynik MCU ma 0x711 bajtów. Walidacja [O]: w rozpakowanych danych deskryptory zadań wskazują napisy "Event timer", "AT process", "Keyboard Daemon Process" itd. oraz funkcje o sensownych adresach (np. "AT process" wskazuje `0x59e2`, czyli procedurę dyspozytora); tablica poleceń zawiera 13 wpisów z poprawnymi wskaźnikami Thumb. Ta sama metoda dla LED (wpis scatter pod `0xba68`, dane od `0xbb54`, 0x100 B skompresowane) daje 6749 B z tablicą poleceń `0x200029b8` [O].

## 4. Wykaz kluczowych adresów (MCU 2.36, baza 0x4000)

| Adres | Funkcja |
|---|---|
| 0xf248 | `main`: kolejność inicjalizacji i pętla główna |
| 0x7758, 0x77ea | inicjalizacja USART1 (BLE) i USART0 (LED) |
| 0xec82, 0xebf2 | przerwania USART1 i USART0 |
| 0x86ae, 0x8748, 0x870a, 0x8778 | odbiór ramki, fragmenty |
| 0x85c0, 0x864a, 0x8606 | budowanie nagłówka i pól ramki |
| 0x87b0, 0x88d0, 0x8912, 0x899e | wysyłka z fragmentacją, routing, przekazywanie, odpowiedź |
| 0xacac, 0xacbe | kolejki nadawcze LED i BLE |
| 0xad2a | zadanie nadawcze UART |
| 0x59e2, 0x58b2, 0x20000494 | zadanie "AT process", wyszukiwanie w tablicy, tablica poleceń |
| 0xf012 | zadanie "Daemon": handshake z BLE i LED |
| 0x7f66, 0x7f9c, 0x7f1e, 0x7ee8 | polecenia `02 01`, `01 03` do BLE; `02 01` i `20 03` do LED |
| 0x7e76, 0x7eac | `40 xx` do BLE; `20 xx` do LED |
| 0xa188, 0xa1b2, 0xa3d4, 0xa42c, 0xa444 | klawisze gniazd BLE (naciśnięcie, zwolnienie, licznik, paruj, połącz) |
| 0xe1b2, 0xe2a4, 0xe240 | raporty HID (USB lub BLE) |
| 0x7ab4, 0x7b3c, 0x7bb4 | przerwania PC0 i PC10 (tryb magistrali, wybór wyjścia) |
| 0x7288, 0x72aa, 0x7438, 0x7450 | odczyt: tryb magistrali, wybór wyjścia, stan LED, stan BLE |
| 0x6256 | polecenie `cmd 0x02` (stan BLE i LED, restart do bootloadera) |
| 0x5d88 | polecenie `cmd 0x01` (wersje, identyfikator) |
| 0x705a, 0x6cfe, 0x6dde | menedżer zasilania, licznik bezczynności, `11 01` do BLE |

Obraz LED: `0x9584` wysyła `01 03` z wersją; `0xa228` obsługuje `cmd 0x20`; `0xa652` i `0xa5b4` obsługują `cmd 0x01` i `0x02`.

## 5. Ograniczenia

- Pierwsze rundy nie miały dekompilatora i opierały się na asemblerze Thumb. Runda z Ghidrą (`08-weryfikacja-i-nowe-ustalenia.md`) uzupełniła `cmd 0x20`, `cmd 0x30`, `cmd 0x50` i poprawiła kilka wniosków; pseudokod 8051 z Ghidry jest mało czytelny, więc BLE weryfikowano głównie listingiem.
- Nie przechwytywano ruchu UART ani HID. Część wniosków potwierdzono funkcjonalnie testami na klawiaturze C18 (parowanie i łączenie, Caps Lock przez BLE, bateria, jasność), ale nie bajt po bajcie. Jeśli potrzebne jest potwierdzenie, najtańsza metoda to podsłuch linii UART (analizator stanów logicznych, 115200 8N1) albo przechwycenie ruchu USB HID ObinsKit.
- Wnioski o fizycznych pinach wynikają z adresów rejestrów GPIO. Schemat C18 z katalogu QMK potwierdził część z nich (PB0/PB1 jako UART do LED) i podważył inne (PC0 jest tam `COL5`, PC3 `ROW5`, a PC10 nie występuje w układzie 48-pinowym). Macierz z kodu zgadza się z QMK (bez PC0 i PC3), a PC10 odpowiada przełącznikowi Bluetooth z instrukcji, więc schemat może nie odpowiadać dokładnie tej rewizji płytki [I].
- Nazwa i wersja obrazów pochodzi z repozytorium zewnętrznego i z ekranu ObinsKit; potwierdzenie względem oficjalnych wydań producenta nie było możliwe.
- ObinsKit 1.2.11 przeanalizowano później statycznie (`07-obserwacje-gatt-i-obinskit.md`, `08-weryfikacja-i-nowe-ustalenia.md`, sekcja 6); aplikacji nie uruchamiano.
