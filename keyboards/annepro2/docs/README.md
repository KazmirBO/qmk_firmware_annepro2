# Anne Pro 2 (C18): how Bluetooth works, from reverse engineering the stock firmware

*English summary.* These notes (written in Polish) describe how the Anne Pro 2 C18 works, found by static analysis of the stock firmware images of the main MCU (ARM), the LED MCU (ARM) and the BLE MCU (8051, TI CC254x), first with REA and later with Ghidra 12.1.4 in headless mode, used through Claude Code. They cover the UART frame format between the processors, the command sets of the main and LED MCUs, the pairing slots, the BLE chip internals (GATT, HID report map, bonding, advertising), the battery curve, the USB descriptors, the hidden "Game wheel" mode with its tilt lighting effect, and the ObinsKit protocol names. `08-weryfikacja-i-nowe-ustalenia.md` lists which earlier claims were confirmed or corrected. The QMK changes in this fork (`annepro2.c`, `annepro2_ble.c`, `annepro2_battery.c`) are based on them. Every claim is marked **[O]** observed in the images or on the device, **[I]** inferred, **[N]** unknown; items not yet checked on hardware are listed in the documents.

# Jak działa Bluetooth w Anne Pro 2 (C18): dokumentacja z analizy firmware

Podstawa: statyczna analiza trzech obrazów (`key-c18-2.36.3.bin`, `rgb-c18-2.33.2.bin`, `ap2_c18_0205_ble.bin`). Analiza obrazów była statyczna (nic z nich nie uruchamiano); funkcje dodane do kodu QMK sprawdzono potem na klawiaturze C18 (parowanie i łączenie po BLE, Caps Lock przez BLE, bateria u hosta i na diodach, klawisze jasności). Znaczniki: **[O]** obserwacja, **[I]** inferencja, **[N]** niewiadoma.

## Spis

| Plik | Treść |
|---|---|
| `01-architektura.md` | trzy procesory, łącza, podział odpowiedzialności, mapa RAM |
| `02-protokol-ramek.md` | format ramek, węzły, polecenia MCU do BLE, BLE do MCU, MCU do LED |
| `03-zachowanie-mcu-bluetooth.md` | handshake, wybór USB lub BLE, gniazda parowania, sen |
| `04-uklad-ble-8051.md` | wnętrze układu BLE (8051, rodzina TI CC253x): UART, polecenia, HID, GAP |
| `05-metoda-i-dowody.md` | narzędzia, dekompresja danych, wykaz adresów, ograniczenia |
| `07-obserwacje-gatt-i-obinskit.md` | odczyt GATT żywej klawiatury, mapowanie poleceń ObinsKit na `cmd`/`sub`, procedura nagrania btmon |
| `08-weryfikacja-i-nowe-ustalenia.md` | runda z Ghidrą: status wcześniejszych twierdzeń, pełne podpolecenia MCU, LED, USB, kody klawiszy, efekty, akcelerometr („Game wheel”), sen, pamięć SPI |

## Najważniejsze ustalenia

1. **Trzy procesory, jedna sieć ramek.** MCU klawiatury (ARM, węzeł 3) łączy się z układem LED (ARM, węzeł 4) przez USART0 i z układem Bluetooth (8051, węzeł 5) przez USART1, oba 115200 8N1. Komputer po USB jest węzłem 1, host połączony po BLE węzłem 2. [O/I]
2. **Wspólny format ramki:** `7B <typ 12|10> <cel:nadawca> <seq> <len16> 00 7D <cmd> <sub> <dane>`. Ten sam format jest po obu stronach łącza BLE i potwierdza go port QMK. Nie znalazłem sumy kontrolnej. [O]
3. **Handshake:** MCU cyklicznie wysyła `02 01` i `01 03` do BLE, aż BLE odpowie `01 03` z wersją `0x25` (2.05) i MCU ustawi stan BLE na 2 (gotowy). [O]
4. **Wybór wyjścia:** raporty HID idą przez USB, gdy USB jest podłączone i zmienna wyboru (czytana z wejścia PC10, czyli najpewniej przełącznika Bluetooth z tyłu klawiatury) wskazuje USB; w pozostałych przypadkach MCU wysyła do BLE `10 04` (klawiatura, 8 B) i `10 08` (multimedia, 4 B). USB niesie 37-bajtowy raport NKRO, BLE tylko 8 bajtów. [O]
5. **Gniazda parowania:** cztery klawisze (kody 200-203). Krótkie naciśnięcie wysyła `40 04 <gniazdo>` (połącz), przytrzymanie 5 s wysyła `40 01 <gniazdo>` (rozgłaszaj, paruj). Układ LED dostaje `20 0b` do podświetlenia wskaźnika, a po połączeniu `20 0c 00`, które go gasi. [O/I]
6. **Układ BLE:** TI CC253x ze stosem BLE; usługa HID 0x1812 z raportami ID 1 (klawiatura 8 B), ID 2 (kanał producenta 18 B, najpewniej konfiguracja z hosta po BLE) i ID 3 (multimedia); nazwa rozgłaszana `AnnePro2` w czterech kopiach (gniazda); producent w DIS `HEXCORE`; Caps Lock z hosta wraca do MCU jako `20 07`. [O/I]
7. **Parowanie w układzie BLE:** Just Works (bez MITM, bez PIN), bonding włączony, do 10 bondów, sloty to indeksy bondów (ten sam adres i nazwa we wszystkich slotach). Parowanie (`40 01`) rozgłasza 60 s co 20 ms, połączenie (`40 04`) 5 s, a po limicie MCU nie dostaje żadnej ramki. `40 05` usuwa wszystkie bondy. Po rozłączeniu BLE wznawia rozgłaszanie sam, jeśli ma bondy. Ramki BLE do MCU mają bajt adresowy `0x35`. [O/I]
8. **Bateria:** MCU liczy napięcie z ADC (PA7) i wysyła procent do LED (`20 0f`); układ BLE ma obsługę `01 05` (surowy ADC), której fabryczny MCU nie używa w przeczytanym kodzie. [O/I]
9. **System i funkcje ukryte (runda Ghidry):** MCU i LED działają na Contiki OS z tickiem 10 ms. Fabryczny MCU ma tryb kierownicy do gier (`KC_FN_WHEEL`, „Game wheel”) i przesyła przechył z akcelerometru LIS3DH jako gamepad USB. LED ma efekt poziomicy (nr 26) oraz efekty 22 i 27, których ObinsKit 1.2.11 nie pokazuje. Szczegóły i lista poprawek: `08-weryfikacja-i-nowe-ustalenia.md`. [O/I]
10. **Przykład spójności między obrazami:** wersja 2.05 pokazywana przez ObinsKit pochodzi z bajtu `0x25` wysyłanego przez BLE i składanego w MCU jako 0x0205; wersja 2.33 LED to `0x0221` wysyłane przez LED. [O/I]

## Największe niewiadome

- ewentualne linie zasilania lub resetu układu BLE po stronie MCU (piny USART są ustalone: PA4/PA5 do BLE, PB0/PB1 do LED);
- format kluczy parowania, miejsce ich przechowywania i polityka parowania w układzie BLE;
- czy host faktycznie dostaje `60 04` jako powiadomienie na Battery Level, okres timera baterii w BLE, funkcja wskazywana przez `0x0B5E` oraz kilka drobnych podpoleceń `cmd 0x40` (0x13, 0x16);
- mechanizm aktualizacji układu BLE (bootloader spoza obrazu) i ewentualna suma kontrolna;
- czy akcelerometr jest obsadzony na płytkach C18 i jakie role mają PA6, PC3, PB6;
- wszystko, co wymagałoby potwierdzenia na urządzeniu (podsłuch UART lub HID).
