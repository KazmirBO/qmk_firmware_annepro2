# Anne Pro 2 (C18): how Bluetooth works, from reverse engineering the stock firmware

*English summary.* These notes (written in Polish) describe how the Anne Pro 2 C18 talks Bluetooth, found by static analysis of the stock firmware images of the main MCU (ARM), the LED MCU (ARM) and the BLE MCU (8051, TI CC253x) with REA, used through Claude Code. They cover the UART frame format between the processors, the commands the main MCU sends to and receives from the BLE chip, the pairing slots, the BLE chip internals (GATT, HID report map, bonding, advertising), the battery curve and the ObinsKit protocol names. The QMK changes in this fork (`annepro2.c`, `annepro2_ble.c`, `annepro2_battery.c`) are based on them. Every claim is marked **[O]** observed in the images or on the device, **[I]** inferred, **[N]** unknown; items not yet checked on hardware are listed in the documents.

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

## Najważniejsze ustalenia

1. **Trzy procesory, jedna sieć ramek.** MCU klawiatury (ARM, węzeł 3) łączy się z układem LED (ARM, węzeł 4) przez USART0 i z układem Bluetooth (8051, węzeł 5) przez USART1, oba 115200 8N1. Komputer po USB jest węzłem 1, host połączony po BLE węzłem 2. [O/I]
2. **Wspólny format ramki:** `7B <typ 12|10> <cel:nadawca> <seq> <len16> 00 7D <cmd> <sub> <dane>`. Ten sam format jest po obu stronach łącza BLE i potwierdza go port QMK. Nie znalazłem sumy kontrolnej. [O]
3. **Handshake:** MCU cyklicznie wysyła `02 01` i `01 03` do BLE, aż BLE odpowie `01 03` z wersją `0x25` (2.05) i MCU ustawi stan BLE na 2 (gotowy). [O]
4. **Wybór wyjścia:** raporty HID idą przez USB, gdy USB jest podłączone i zmienna wyboru (czytana z wejścia PC10, rola pinu nieustalona) wskazuje USB; w pozostałych przypadkach MCU wysyła do BLE `10 04` (klawiatura, 8 B) i `10 08` (multimedia, 4 B). USB niesie 37-bajtowy raport NKRO, BLE tylko 8 bajtów. [O]
5. **Gniazda parowania:** cztery klawisze (kody 200-203). Krótkie naciśnięcie wysyła `40 04 <gniazdo>` (połącz), przytrzymanie około 500 jednostek wysyła `40 01 <gniazdo>` (rozgłaszaj, paruj). Układ LED dostaje `20 0b` do podświetlenia wskaźnika. [O/I]
6. **Układ BLE:** TI CC253x ze stosem BLE; usługa HID 0x1812 z raportami ID 1 (klawiatura 8 B), ID 2 (kanał producenta 18 B, najpewniej konfiguracja z hosta po BLE) i ID 3 (multimedia); nazwa rozgłaszana `AnnePro2` w czterech kopiach (gniazda); producent w DIS `HEXCORE`; Caps Lock z hosta wraca do MCU jako `20 07`. [O/I]
7. **Parowanie w układzie BLE:** Just Works (bez MITM, bez PIN), bonding włączony, do 10 bondów, sloty to indeksy bondów (ten sam adres i nazwa we wszystkich slotach). Parowanie (`40 01`) rozgłasza 60 s co 20 ms, połączenie (`40 04`) 5 s, a po limicie MCU nie dostaje żadnej ramki. `40 05` usuwa wszystkie bondy. Po rozłączeniu BLE wznawia rozgłaszanie sam, jeśli ma bondy. Ramki BLE do MCU mają bajt adresowy `0x35`. [O/I]
8. **Bateria:** MCU liczy napięcie z ADC (PA7) i wysyła procent do LED (`20 0f`); układ BLE ma obsługę `01 05` (surowy ADC), której fabryczny MCU nie używa w przeczytanym kodzie. [O/I]
9. **Przykład spójności między obrazami:** wersja 2.05 pokazywana przez ObinsKit pochodzi z bajtu `0x25` wysyłanego przez BLE i składanego w MCU jako 0x0205; wersja 2.33 LED to `0x0221` wysyłane przez LED. [O/I]

## Największe niewiadome

- ewentualne linie zasilania lub resetu układu BLE po stronie MCU (piny USART są ustalone: PA4/PA5 do BLE, PB0/PB1 do LED);
- format kluczy parowania, miejsce ich przechowywania i polityka parowania w układzie BLE;
- czy host faktycznie dostaje `60 04` jako powiadomienie na Battery Level, okres timera baterii w BLE, funkcja wskazywana przez `0x0B5E` oraz kilka drobnych podpoleceń `cmd 0x40` (0x13, 0x16);
- mechanizm aktualizacji układu BLE (bootloader spoza obrazu) i ewentualna suma kontrolna;
- wszystko, co wymagałoby potwierdzenia na urządzeniu (podsłuch UART lub HID).
