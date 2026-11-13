# Anne Pro 2 (C18): analiza fabrycznego firmware i port QMK

Dokumentacja działania klawiatury Anne Pro 2 w rewizji C18, ze szczególnym uwzględnieniem Bluetootha, oparta na statycznej analizie fabrycznego firmware trzech procesorów i aplikacji ObinsKit 1.2.11, oraz opis zmian w porcie QMK, które z niej wynikają. Stan na 2026-10-27; każde twierdzenie zostało tego dnia ponownie sprawdzone u źródła (`09-metoda-i-weryfikacja.md` §6). Uzupełnienie z 2026-11-03 (mapa fabryczna MCU, matryca, wskaźniki i uśpienie LED, suma kontrolna obrazu BLE i kilka rozstrzygniętych niewiadomych) opisuje `09-metoda-i-weryfikacja.md` §8, a uzupełnienie z 2026-11-13 (odroczone kasowanie bondów, zrywanie łącza przez `40 04`, warstwy 3 i 4 mapy MCU, flaga LED bez odczytów, poprawki portu) `09-metoda-i-weryfikacja.md` §9.

## 1. Źródła

| Procesor | Plik | Rozmiar | SHA-256 | Wersja |
|---|---|---|---|---|
| MCU | `key-c18-2.36.3.bin` | 46 779 B | `b9bfa750e8c7ccdbed0c1f6de8aeb4eb1d569dc5cc075afdb2a93fe5e20730de` | 2.36 |
| LED | `rgb-c18-2.33.2.bin` | 31 828 B | `ea1c8e54ac8ca47c7c6d073b2f8c7977a840c1e9013ca4aea5621f0e98d94dc5` | 2.33 |
| BLE | `ap2_c18_0205_ble.bin` | 155 648 B | `52dc5c6542ad9b30915ea07f042b46ef19cd8acf4f2dce286a0dbdf11ce7cb92` | 2.05 |

- Wszystkie trzy obrazy pochodzą z repozytorium społeczności `dchang0/AnnePro2Firmware`, commit `2727333999bf0cf108b6247d3194a6b4679bbb40`, katalog `StockFirmware/` (sumy sprawdzone 2026-10-27). Zgodności z wydaniami producenta nie sprawdzano.
- ObinsKit 1.2.11 dla Linuksa: archiwum `ObinsKit_1.2.11_x64.tar.gz` (SHA-256 `2a10aed5…096cd`), szczegóły w `06-obinskit-i-bootloader.md` §1.
- Schemat: `AnnePro2Schematics.PDF` z `OpenAnnePro/AnnePro2-Hardware`; rysuje wariant 48-pinowy, więc dla C18 nie jest autorytatywny (`01-architektura.md` §7).
- Port QMK: fork `qmk_firmware_annepro2`, katalog `keyboards/annepro2/` (`07-port-qmk-i-narzedzia.md`).
- Do porównania pinów matrycy LED: otwarty firmware LED `annepro2-shine` (commit `0bcaf1c`, wariant C18; `04-led.md` §7).

## 2. Znaczniki i konwencje

- **[O]** obserwacja: wynika wprost z bajtów albo kodu obrazu, z kodu ObinsKit, z odczytu urządzenia albo z kodu portu.
- **[I]** inferencja: wniosek z obserwacji, z podanym uzasadnieniem.
- **[N]** niewiadoma; zbiorcza lista w `10-niewiadome.md`.
- Fakty z dokumentacji producentów, instrukcji i projektów społeczności są oznaczone jako zewnętrzne ([zewn.] albo wprost w tekście).
- Adresy MCU i LED to adresy wirtualne przy bazie 0x4000 (np. `0x59e2`), RAM to `0x2000xxxx`. W BLE `b:AAAA` to adres kodu w banku `b` (0 to obszar wspólny), `X:AAAA` adres XDATA, a `plik+0x…` offset w pliku obrazu.
- Ramki zapisuje się jako `cmd sub dane` szesnastkowo, pełny nagłówek tylko tam, gdzie ma znaczenie. Węzły: 1 komputer po USB, 2 host po BLE, 3 MCU, 4 LED, 5 BLE.
- Liczniki MCU podaje się w tickach systemowych po 10 ms.
- Odnośnik do sekcji to nazwa pliku i numer sekcji po znaku paragrafu; sam numer po znaku paragrafu wskazuje sekcję bieżącego pliku.

## 3. Najważniejsze ustalenia

1. **Trzy procesory, jedna sieć ramek.** MCU (HT32F52352, węzeł 3) łączy się z LED (węzeł 4) przez USART0 i z układem Bluetooth (CC2540 albo CC2541, 8051, węzeł 5) przez USART1, oba 115200 8N1; komputer po USB i host po BLE (raport HID ID 2) rozmawiają tym samym protokołem (`01-architektura.md`, `02-protokol.md`).
2. **Ramka:** `7B <typ 12|10> <cel:nadawca> <seq> <len16> 00 7D <cmd> <sub> <dane>`, bez sumy kontrolnej; MCU przekazuje ramki dla innych węzłów bez zmian (`02-protokol.md` §1-2).
3. **Handshake:** MCU wysyła do BLE `02 01 02` i `01 03 02` przy starcie, 5 razy co 0,5 s i dalej co 5 s, aż BLE odpowie `01 03 25` (wersja 2.05); potem zawsze odsyła UDID jako 32 znaki hex (`01 10`), które układ BLE odrzuca, więc numer seryjny DIS zostaje domyślny (`03-mcu.md` §3).
4. **Wybór wyjścia:** przełącznik Bluetooth z tyłu klawiatury to PC10 (Off: USB, On: Bluetooth, sprawdzone na C18), detekcja USB to PC0; po USB raport NKRO 37 B, po BLE 8 B (`03-mcu.md` §4).
5. **Sloty:** klawisze 200-203; krótkie naciśnięcie wysyła `40 04 <slot>` (łącz), przytrzymanie 5 s `40 01 <slot>` (paruj); połączenie (`20 0c` od BLE) gasi wskaźnik na diodach (`03-mcu.md` §5).
6. **Układ BLE:** parowanie Just Works z bondingiem, do 10 bondów, sloty to indeksy bondów z tą samą nazwą i adresem; parowanie rozgłasza 60 s, łączenie 5 s, co 20 ms; po rozłączeniu rozgłaszanie nie wraca samo, wznawia je dopiero naciśnięcie klawisza; `40 05` kasuje bondy wszystkich slotów, przy połączonym hoście dopiero po zakończeniu łącza (`05-ble.md` §7-8).
7. **Bateria:** fabryczny MCU mierzy baterię (ADC na PA7), ale nie wysyła pomiaru do BLE, więc host widzi stałe 100 %; układ BLE ma własną krzywą z interpolacją i poziom tylko obniża; port QMK wysyła pomiar (`03-mcu.md` §8, `05-ble.md` §9, `07-port-qmk-i-narzedzia.md` §1).
8. **Sen:** po około 1 s bezczynności (bez USB, z włączonym „Auto Sleep”) MCU przechodzi w DeepSleep; po upływie limitu snu RTC budzi go do długiego snu i dopiero wtedy wysyła do BLE `11 01` (`03-mcu.md` §7).
9. **System:** MCU i LED działają na Contiki OS z tickiem 10 ms, BLE na OSAL i stosie TI BLE w kodzie bankowanym IAR (`01-architektura.md` §4).
10. **Funkcje ukryte i martwe:** tryb kierownicy „Game wheel” (akcelerometr LIS3DH jako gamepad USB), efekty podświetlenia 22, 26 (poziomica) i 27 nieobecne w ObinsKit, kody 0xA5-0xA7 bez działania, mysz wirtualna ignorowana przez BLE, wizualizacja dźwięku bez obsługi w LED (`03-mcu.md` §6, `03-mcu.md` §11, `04-led.md` §4, `05-ble.md` §4.3).
11. **Mapa fabryczna:** MCU ma we flash pięć warstw ANSI (baza, FN1, FN2 ze slotami BLE, podświetleniem i myszą na IJKL, warstwa strzałek i warstwa gamepada); `10 06` przełącza między nią a mapą z SPI, a tablica ISO jest w obrazie, ale nieużywana; warstwa strzałek działa jako tap-key po `10 05`, a warstwa gamepada tylko w trybie kierownicy (`03-mcu.md` §6.1).
12. **LED:** piny matrycy 5 × 14 RGB są te same co w wariancie C18 `annepro2-shine`; wskaźniki slotu, Caps Lock, kierownicy i baterii (klawisze 1-0 jako pasek co 10 %) mają ustaloną kolejność i zatrzymują efekt; `20 1f` wybiera tabelę jasności 0-15 albo 0-36; układ LED zasypia na sygnał z PB12, najpewniej z PC3 w MCU (`04-led.md` §6-8).
13. **Uwagi o bezpieczeństwie:** odczyt raportu wyjściowego ID 2 po BLE zwraca 15 B niezapisanej pamięci, `40 13` i `40 31` zwracają bajty stosu, a przez UART można zmienić adres BD i skasować bondy (`05-ble.md` §13).

## 4. Pliki

| Plik | Treść |
|---|---|
| `01-architektura.md` | procesory, łącza, podział odpowiedzialności, system, pamięć, piny MCU, schemat |
| `02-protokol.md` | format ramki, węzły i routing, fragmentacja, kanał przez BLE, polecenia wszystkich odbiorców, ruch wysyłany samodzielnie |
| `03-mcu.md` | MCU 2.36: procesy, czas, handshake, wybór wyjścia, sloty, kody klawiszy i mapa fabryczna, sen, bateria, pamięć SPI, USB, akcelerometr, rekord urządzenia, wykaz adresów |
| `04-led.md` | LED 2.33: system, polecenia, efekty, wskaźniki, matryca i piny, jasność, uśpienie |
| `05-ble.md` | BLE 2.05: obraz i banki, UART, polecenia, GATT i HID, rozgłaszanie i sloty, bondy, bateria, zasilanie, aktualizacja, odczyt z urządzenia, bezpieczeństwo |
| `06-obinskit-i-bootloader.md` | ObinsKit 1.2.11: transport, nazwy poleceń, aktualizacja; bootloader i odczyt `--info` |
| `07-port-qmk-i-narzedzia.md` | port QMK i AnnePro2-Tools względem oryginału, keymapa `kazmir_bo`, buildy, historia zmian |
| `08-testy-na-sprzecie.md` | testy na klawiaturze, czego nie sprawdzono, procedury (po wgraniu, `btmon`, podsłuch UART) |
| `09-metoda-i-weryfikacja.md` | narzędzia, powtórzenie analizy, import w Ghidrze, dane RW, raport weryfikacji |
| `10-niewiadome.md` | otwarte pytania i co by je rozstrzygnęło |

## 5. Poza tym katalogiem

- `analiza/`: skrypty importu do Ghidry i kontroli, eksporty pseudokodu i listingów, wyniki weryfikacji (`analiza/wyniki/`) i rejestr twierdzeń (`analiza/rejestr-twierdzen.md`). Od 2026-11-04 katalog leży w forku jako `keyboards/annepro2/lokalne/analiza/` (poza kontrolą wersji), a obrazy fabryczne w `lokalne/obrazy-fabryczne/`; ścieżki `analiza/...` w tej dokumentacji oznaczają to miejsce.
- `propozycje/`: archiwalne patche z 2026-10-14 i kopie plików sprzed zmian; ich treść jest dziś w commitach forka.
- `do-wgrania/` (obraz portu dla keymapy `kazmir_bo`, narzędzie do wgrywania i instrukcja) i `docs.bak-2026-10-27/` (dokumentacja sprzed przebudowy, do której odwołują się wyniki weryfikacji) zostały usunięte 2026-11-04 i nie ma ich już w projekcie.
