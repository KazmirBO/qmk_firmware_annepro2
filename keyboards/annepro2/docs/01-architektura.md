# Architektura: trzy procesory i ich łącza

Znaczniki i konwencje adresów: `README.md` §2.

## 1. Procesory

| Procesor | Układ | Obraz | Wersja w obrazie | Nazwa w ObinsKit |
|---|---|---|---|---|
| MCU (węzeł 3) | Holtek HT32F52352, Cortex-M0+ (Thumb) | `key-c18-2.36.3.bin` | 0x0224 (2.36): `0x71ec` zapisuje ją pod `0x20001187` [O] | Application Processor |
| LED (węzeł 4) | Holtek HT32F52352, Cortex-M0+ | `rgb-c18-2.33.2.bin` | 0x0221 (2.33): LED wysyła ją w `01 03` (`0x9584`) [O] | Light Processor |
| BLE (węzeł 5) | TI CC2540 albo CC2541 (8051) ze stosem TI BLE | `ap2_c18_0205_ble.bin` | 0x25 (2.05): `1:EF64` i `1:F7B8` wysyłają ją w `01 03` [O] | Bluetooth Processor |

- Rodzina HT32F523x2 wynika z kodu: adresy peryferiów (USART0 `0x40000000`, USART1 `0x40040000`, USB `0x400A8000`, PDMA `0x40090000`, GPIO `0x400B0000`-`0x400B6000`) i numery przerwań (USART0 = 23, USART1 = 24, USB = 29) zgadzają się z nagłówkiem `ht32f523x2.h` z ChibiOS-Contrib [O]. Konkretny układ HT32F52352 w obudowie LQFP64 podają OpenAnnePro (`openannepro.github.io/ap2_revisions`, dla C15 dwa HT32F52342 w LQFP48) i skrypt linkera QMK dla C18 [I, źródła zewnętrzne]. Zgadza się to z kodem, który używa PC10, PC11-PC13 i portu D, niedostępnych w 48 pinach [O].
- Rodzina CC254x wynika z rejestrów (FMAP, RFD, RFST, ENCCS, timer uśpienia, kontroler flash pod XDATA `0x6270`-`0x6273`) i stosu TI BLE; wariantu CC2540 czy CC2541 obraz nie rozstrzyga (`05-ble.md` §1) [N].
- ObinsKit pokazuje wersje jako u16 `główna.poboczna` (0x0224 to „2.36”), a bajt BLE jako półbajty (0x25 to „2.05”) [O, kod ObinsKit]. Wersje 2.36, 2.33 i 2.05 widoczne w ObinsKit pochodzą ze zrzutu ekranu użytkownika i zgadzają się z obrazami.

## 2. Łącza

```
komputer ---- USB HID (EP4, 64 B) ---- MCU (węzeł 3) ---- USART0 115200 8N1 ---- LED (węzeł 4)
                                         |
                                         +------------- USART1 115200 8N1 ---- BLE (węzeł 5) ---- BLE ---- host (węzeł 2)
```

| Łącze | Strona MCU | Strona drugiego układu |
|---|---|---|
| MCU - LED | USART0, PB0 (TX) i PB1 (RX), nadawanie PDMA kanał 1, odbiór w przerwaniu IRQ 23 [O] | UART układu LED |
| MCU - BLE | USART1, PA4 (TX) i PA5 (RX), nadawanie PDMA kanał 3, odbiór w przerwaniu IRQ 24 [O] | USART0 CC254x w lokalizacji 1: P0.2 (RX), P0.3 (TX); 115200 bez kontroli przepływu; odbiór przez DMA kanał 3 [O] |
| komputer - MCU | USB HID: kanał producenta 64 B (EP 0x03/0x84) dla ObinsKit, raporty klawiatury, multimediów, myszy i gamepada (`03-mcu.md` §10) [O] | - |
| host - BLE | raport HID ID 2 (18 B w obie strony) niesie ramki protokołu (`02-protokol.md` §4) [O] | - |

- Obie linie UART zasilają w MCU jedną kolejkę odbiorczą (`0x20000468`), z której ramki czyta proces „AT process” [O].
- Układ BLE nie ma w kodzie żadnej komunikacji z LED: stan wskaźników BLE trafia do LED przez MCU (`20 0b`, `20 0c`, `20 07`) [O]. Brak fizycznego połączenia BLE-LED to wniosek [I].
- Przed długim snem MCU przełącza PA4 (TX do BLE) na GPIO w stanie wysokim, a po wybudzeniu klawiszem ustawia go nisko i wraca do USART (`03-mcu.md` §7) [O]. Czy to linia budzenia BLE, nie wiadomo [N].

## 3. Podział odpowiedzialności

| Funkcja | Gdzie | Szczegóły |
|---|---|---|
| Stos BLE, rozgłaszanie, parowanie, bondy, HID po BLE | BLE | `05-ble.md` |
| Wybór wyjścia USB albo BLE | MCU, z wejść PC0 (USB) i PC10 (przełącznik Bluetooth) | `03-mcu.md` §4 |
| Klawisze slotów (połącz, paruj) | MCU, kody 200-203 | `03-mcu.md` §5 |
| Handshake i wersje | MCU z BLE i z LED | `03-mcu.md` §3 |
| Wskaźnik slotu na diodach | MCU wysyła `20 0b` po klawiszu i `20 0c` po połączeniu; LED rysuje | `04-led.md` §3 |
| Sen i wybudzanie | MCU (licznik bezczynności, RTC), BLE (własny sen OSAL) | `03-mcu.md` §7, `05-ble.md` §10 |
| Pomiar baterii | MCU (ADC na PA7); BLE ma własną krzywą procentową, ale fabryczny MCU nie wysyła mu pomiaru | `03-mcu.md` §8, `05-ble.md` §9 |
| Podświetlenie i efekty | LED, konfiguracja w pamięci SPI MCU | `04-led.md` |
| Konfiguracja z komputera (ObinsKit) | MCU przyjmuje i przekazuje ramki do LED i BLE | `02-protokol.md`, `06-obinskit-i-bootloader.md` |

## 4. System

- MCU i LED działają na Contiki OS: procesy jako protothready, `process_post`, etimery (`03-mcu.md` §1, `04-led.md` §1) [O].
- Zegar MCU: `SystemCoreClock` = 48 MHz, SysTick ze źródła STCLK, przeładowanie 60000 (`0x76c6`-`0x76ee`) [O]. Według karty katalogowej HT32F52342/52352 (Rev. 1.60, rys. 3) STCLK to `CK_AHB` podzielone przez 8, więc tick wynosi 10 ms [I mocne]. Zgadza się to z instrukcją (5 s przytrzymania slotu). Wszystkie liczniki MCU w tej dokumentacji podane w tickach należy mnożyć przez 10 ms.
- BLE działa na OSAL ze stosem TI BLE; kod jest skompilowany w modelu bankowanym IAR (`05-ble.md` §2) [O].

## 5. Pamięć

### 5.1. MCU

| Obszar | Zawartość |
|---|---|
| flash `0x0000`-`0x3FFF` | poza obrazem; bootloader [I]. Strona `0x3E00` zawiera rekord urządzenia (UDID, PID), który zapisuje aplikacja (`03-mcu.md` §12) [O] |
| flash od `0x4000` | obraz aplikacji (46 779 B); bazę 0x4000 potwierdza odpowiedź bootloadera (`06-obinskit-i-bootloader.md` §6) i to, że przy niej wszystkie wektory wskazują wnętrze obrazu (reset `0xf479`) [O] |
| `0x20000000` (0x711 B) | dane inicjalizowane, rozpakowane z `0xf4b6` (algorytm LZ kompilatora Keil, `09-metoda-i-weryfikacja.md` §5): m.in. tablica poleceń `0x20000494`, struktury procesów, deskryptory USB [O] |
| `0x20000714` (0xc9c B) | BSS zerowane przy starcie [O] |
| `0x20001144`-`0x2000115b` | kopia rekordu urządzenia z flash `0x3E00` [O] |
| `0x2000115c`-`0x20001174` | konfiguracja wczytana z pamięci SPI (`0x7dfa`) [O] |
| od `0x20001175` | stan pracy, m.in. `0x20001182` tryb magistrali, `0x20001187` wersje MCU, LED i BLE, `0x200011a0` stan LED, `0x200011a1` stan BLE, `0x200011ad` wybór wyjścia [O] |
| `0x20000468` | deskryptor kolejki odbiorczej obu UART-ów (bufor 512 B) [O] |
| `0x20000624`, `0x20000630` | deskryptory kolejek nadawczych do LED i do BLE (bufory po 512 B) [O] |
| `0x20001db0` | początkowy wskaźnik stosu [O] |
| `0x20001ffc` | sygnatura `0xfab2`, po której reset zostaje w bootloaderze [O co do zapisu, I co do odczytu] |

### 5.2. LED

Dane inicjalizowane rozpakowane z `0xbb54` (0x100 B) do `0x20001000` (6749 B), BSS 0xa50 B od `0x20002a60`, początkowy wskaźnik stosu `0x20000600`, tablica poleceń pod `0x200029b8` [O].

### 5.3. BLE

Obraz jest płaskim zrzutem flash od adresu fizycznego 0x2000. Obszar wspólny to CPU `0x2000`-`0x7FFF` (CPU = offset + 0x2000), banki 1-4 leżą w oknie 0x8000-0xFFFF (offset = 0x6000 + (b-1)*0x8000 + (adres - 0x8000)). Okno XDATA 0x8000-0xFFFF jest ustawione na bank 1 (MEMCTR.XBANK = 1 w `0:2537`), więc `X:8xxx` to plik+0x6xxx. Przy starcie kod kopiuje plik+0x92-0x536 do `X:1655`-`X:1AF9` (dane inicjujące, X = plik + 0x15C3) i zeruje `X:0280`-`X:1654`. Treść obrazu kończy się na plik+0x1F480 (128 129 B), dalej są bajty 0xFF. Szczegóły: `05-ble.md` §2 [O].

## 6. Piny MCU

Konfiguracja w `0xb1b8` (funkcja `0xe0c0`: port, maska, numer funkcji; 1 GPIO, 2 ADC, 5 SPI, 6 USART, 7 I2C) [O]:

| Piny | Funkcja | Zastosowanie |
|---|---|---|
| PB0, PB1 | USART (6) | USART0 do LED; zgodne z QMK `c18/config.h` |
| PA4, PA5 | USART (6) | USART1 do BLE; zgodne z QMK |
| PA0, PA1, PA2 | SPI (5) | SPI1 do pamięci SPI NOR (SCK, MOSI, MISO) |
| PA3 | GPIO, wyjście | CS pamięci SPI |
| PA7 | ADC (2) | kanał 7, pomiar baterii |
| PB7, PB8 | I2C (7) | I2C1, akcelerometr LIS3DH |
| PC0 | GPIO, wejście bez podciągania, EXTI0 | detekcja zasilania USB |
| PC10 | GPIO, wejście z podciąganiem w dół, EXTI10 | przełącznik Bluetooth z tyłu klawiatury (stan wysoki to USB) |
| PA6 | GPIO, wejście z podciąganiem w górę | tylko źródło wybudzenia; rola [N] |
| PC3 | GPIO, wyjście | ustawiany na 1 przed snem, zależnie od stanu podświetlenia; najpewniej zasilanie lub uśpienie układu LED [I] |
| PB6 | GPIO, wyjście | ustawiany na 1 przy starcie, poza tym nieużywany; rola [N] |
| PC1 | funkcja 6 | rola [N] |
| PB5, PB4, PB3, PB2, PD1 | GPIO, wejścia z podciąganiem w dół | wiersze macierzy |
| PC4, PC5, PD0, PB15, PC11, PA15, PC12, PC13, PA8, PA10, PA11, PA14, PD2, PD3 | GPIO | kolumny macierzy (14); zgodne z QMK `c18/keyboard.json` |
| PA9, PC8, PC9, PB12, PB13, PB14, PC2 | GPIO, wejścia | nieużywane |

## 7. Schemat płytki

Jedyny znany schemat to `AnnePro2Schematics.PDF` z repozytorium `OpenAnnePro/AnnePro2-Hardware` (commit `73179efb`, 2020-07-02, SHA-256 `05643631…1cd3a`). Arkusz ma notatkę „This page applies only to the C18 revision”, ale rysuje dwa układy HT32F52342 w obudowie LQFP48, czyli wariant opisywany przez OpenAnnePro jako C15. Piny macierzy na schemacie (np. ROW1 = PC2, ROW5 = PC3, COL5 = PC0) nie zgadzają się z firmware C18, a pinu PC10 obudowa 48-pinowa nie ma. Schemat nie jest więc źródłem dla pinów C18; nie zawiera też układu Bluetooth [O, schemat].

Co schemat pokazuje dla swojego wariantu: PB0 (pin 33) z etykietą `UART_LED` idzie do PA5 układu LED (PB1 bez etykiety), PB7 (pin 45) ma etykietę `LED_RESET`, PB8 (pin 46) `LED_WKUP` połączoną z pinem 20 (`RTCOUT`) układu LED, PB6 (pin 44) jest niepodłączony. W firmware C18 PB7 i PB8 pracują jako I2C1 dla akcelerometru, więc te etykiety C18 nie dotyczą [I].
