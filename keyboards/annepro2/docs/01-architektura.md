# Architektura Bluetooth w Anne Pro 2 (rewizja C18)

Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma.

## 1. Trzy procesory

Klawiatura ma trzy osobne procesory, a ObinsKit pokazuje ich wersje jako "Application Processor", "Light Processor" i "Bluetooth Processor". Wersje zgłoszone przez ObinsKit (zrzut ekranu użytkownika): 2.36, 2.33, 2.05.

| Procesor | Plik | Rozmiar | SHA-256 | Rdzeń | Wersja |
|---|---|---|---|---|---|
| Application (MCU) | `key-c18-2.36.3.bin` | 46 779 B | `b9bfa750e8c7ccdbed0c1f6de8aeb4eb1d569dc5cc075afdb2a93fe5e20730de` | ARMv6-M (Thumb), rodzina Holtek HT32F523xx [I] | 2.36 |
| Light (LED) | `rgb-c18-2.33.2.bin` | 31 828 B | `ea1c8e54ac8ca47c7c6d073b2f8c7977a840c1e9013ca4aea5621f0e98d94dc5` | ARMv6-M (Thumb), ta sama rodzina [I] | 2.33 |
| Bluetooth (BLE) | `ap2_c18_0205_ble.bin` | 155 648 B | `52dc5c6542ad9b30915ea07f042b46ef19cd8acf4f2dce286a0dbdf11ce7cb92` | 8051 (wektor `LJMP 0x25CB` pod 0x0000) [O] | 2.05 |

Dopasowanie nazw plików do wersji z ObinsKit opiera się na nazwach plików i na tym, że LED zgłasza wersję `0x0221` (2.33), co jest zgodne z ekranem ObinsKit [O]. Wersji MCU i BLE nie potwierdziłem wprost z zawartości obrazów [N].

Identyfikacja rdzenia MCU i LED [I]: adresy peryferiów w kodzie (`0x40000000` USART0, `0x40040000` USART1, `0x400A8000` USB, `0x40090000` PDMA, `0x400B0000`-`0x400B6000` GPIO) zgadzają się z nagłówkiem HT32F523x2 z repozytorium ChibiOS-Contrib; numery przerwań w tablicy wektorów (USART0 = 23, USART1 = 24, USB = 29) mają w tych slotach sensowne procedury obsługi.

Baza obrazów MCU i LED to **0x4000** [O]: potwierdza ją odpowiedź bootloadera (odczyt `--info` z lokalnie zmodyfikowanego `AnnePro2-Tools`, patrz `07-obserwacje-gatt-i-obinskit.md`, sekcja 5), a pierwotnie wynikała z analizy [I]. Uzasadnienie: wektor reset MCU wskazuje 0xf478, czyli poza plikiem o rozmiarze 0xb6bb liczonym od zera; przy bazie 0x4000 wszystkie wektory wskazują wnętrze obrazu, a po rozpakowaniu danych RW wskaźniki do funkcji i napisów zgadzają się z deasemblacją. Pierwsze 16 KiB flasha to prawdopodobnie bootloader, do którego MCU skacze po ustawieniu sygnatury `0xfab2` w `0x20001ffc` [I].

## 2. Połączenia

```
komputer ---- USB HID (EP4, 64 B) ---- MCU (węzeł 3) ---- USART0 115200 ---- LED (węzeł 4)
                                         |
                                         +--------------- USART1 115200 ---- BLE (węzeł 5) ---- radio BLE ---- host (węzeł 2)
```

- Łącza UART mają parametry 115200 8N1; nadawanie przez PDMA, odbiór w przerwaniu [O].
- BLE nie ma własnego łącza z LED: informacja o stanie wskaźników BLE trafia do LED przez MCU [O].
- Przez MCU przechodzi również komunikacja zdalna (np. konfiguracja) z hosta połączonego po BLE: układ BLE przekazuje takie ramki do MCU z nadawcą 2, a odpowiedzi MCU wracają tą samą drogą [O/I].

Szczegóły ramek: `02-protokol-ramek.md`. Zachowanie MCU: `03-zachowanie-mcu-bluetooth.md`. Układ BLE: `04-uklad-ble-8051.md`. Metoda i dowody: `05-metoda-i-dowody.md`.

## 3. Podział odpowiedzialności za Bluetooth

| Funkcja | Gdzie | Dowód |
|---|---|---|
| Stos BLE, rozgłaszanie, parowanie, przechowywanie kluczy, HID po BLE | układ BLE (8051) | `04-uklad-ble-8051.md` |
| Wybór: raporty przez USB czy przez BLE | MCU | zmienna `0x200011ad` (źródło: wejście PC10, rola pinu nieustalona), `0xe1b2`, `0xe2a4` [O] |
| Obsługa klawiszy gniazd 1-4 (połącz, paruj) | MCU | kody 200-203, `0xa188`, `0xa1b2`, `0xa3d4` [O] |
| Handshake i wersja | MCU i BLE | `02 01`, `01 03` [O] |
| Wskaźnik gniazda na diodach | MCU wysyła `20 0b`, LED rysuje | `0x7eac`, handler LED `20 0b` [I] |
| Sen i wybudzanie | MCU (menedżer zasilania), `11 01` do BLE | `0x705a`, `0x6dde` [I] |

## 4. Pamięć RAM (MCU)

| Zakres | Zawartość |
|---|---|
| `0x20000000` (0x711 B) | dane inicjalizowane, rozpakowane z `0xf4b6` (algorytm LZ Keil), w tym tablica poleceń `0x20000494` i deskryptory zadań |
| `0x20000714` (0xc9c B) | BSS zerowane przy starcie |
| `0x20001144` | struktura konfiguracji (kopia ustawień); w niej m.in. `0x20001182` tryb magistrali, `0x200011a0` stan LED, `0x200011a1` stan BLE, `0x200011ad` wybór wyjścia |
| `0x20000468` | kolejka bajtów z obu UART-ów |
| `0x20000624`, `0x20000630` | kolejki nadawcze (LED, BLE), po 256 B |
| `0x20001db0` | szczyt stosu początkowego |

Układ LED ma analogiczną strukturę: RW rozpakowane do `0x20001000` (6749 B), BSS od `0x20002a60`, stos od `0x20000600`, własna tablica poleceń pod `0x200029b8` z procedurami dla `cmd 0x01` (`0xa652`), `0x02` (`0xa5b4`), `0x20` (`0xa228`) i pustym `0x21` (`0xa224`) [O].
