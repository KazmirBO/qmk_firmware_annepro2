# Testy na sprzęcie

Testy wykonał użytkownik na jednej klawiaturze Anne Pro 2 C18 z hostem po BLE. Źródłem są jego relacje w trakcie pracy i zrzuty ekranu z hosta; nie są to pomiary ani nagrania ruchu. Kolumna „Wersja” podaje commit forka `qmk_firmware_annepro2`, z którego pochodził wgrany obraz; obraz dla przełącznika Bluetooth ma ten sam SHA-256 co build `6b71633911`, a w pozostałych wierszach commit przypisano na podstawie opisu wersji w relacji („po parserze”, „po refaktoryzacji” itd.).

## 1. Wyniki

| Funkcja | Wersja | Wynik |
|---|---|---|
| Parowanie i łączenie po BLE (`40 01`, `40 04`) | `8b1bc7b9be` (parser i handshake) | działa |
| Caps Lock przez BLE, gaszenie migania po połączeniu, tap/hold slotów, przełączanie USB i BLE, multimedia, stabilność | `8b1bc7b9be` | bez zgłoszonej niezgodności; punkty nie były raportowane osobno |
| Poziom baterii u hosta (`01 05`, ADC na PA7) | `354db088eb` | host pokazał 73 %, później odczyt GATT dał 67 % |
| Poziom baterii po ponownym starcie BLE | `5bf72bff35` | 90 % |
| `KC_AP2_BATT` (`Fn2` + `8`) | `5bf72bff35` | wskaźnik na diodach działa |
| Klawisze jasności przez BLE (`Fn1` + `M`, `Fn1` + `,`) | `099f766757` | działają |
| Wgranie przez `annepro2_tools --boot` i start | `5bf72bff35` | klawiatura działa |
| `annepro2_tools --info` w trybie IAP (tylko odczyt) | Tools `006d480` | tryb 1 (IAP) dla MCU, LED i BLE; trzy rekordy z bazą 0x4000 i rozmiarem 0x1fe00 (`06-obinskit-i-bootloader.md` §6) |
| Wgranie `18355aa8ba` (korekta 0 % baterii) | `18355aa8ba`, 35 808 B | `Flash complete`, klawiatura wstała, po BLE bateria 92 % |
| Przełącznik Bluetooth (`AP2_BT_SWITCH` w `kazmir_bo`), 2026-10-26 | `6b71633911`, 31 500 B | Off daje USB, On daje Bluetooth, zgodnie z analizą oryginału |
| Odczyt GATT połączonej klawiatury (BlueZ przez D-Bus, tylko odczyt) | firmware QMK sprzed `6c03dfe813` | wyniki w `05-ble.md` §12 (numer seryjny jeszcze domyślny) |
| Wgranie `c18:kazmir_bo` z `c0a2cfcccb`, 2026-11-06 | `c0a2cfcccb`, 32 840 B | `annepro2_tools --boot`: 32 840 B zapisane, `Flash complete`; wgrany plik ma tę samą sumę SHA-256 co build w `07-port-qmk-i-narzedzia.md` §5; nowych funkcji nie sprawdzano (§2) |
| Wgranie `c18:kazmir_bo` z `3fad67e4b0`, 2026-11-13 | `3fad67e4b0`, 32 992 B | 32 992 B zapisane, `Flash complete`, klawiatura wstała; tego samego dnia zastąpiony obrazem z `4a0086b5ae`; funkcji nie sprawdzano (§2) |
| Wgranie `c18:kazmir_bo` z `4a0086b5ae`, 2026-11-13 | `4a0086b5ae`, 33 216 B | `annepro2_tools --boot`: 33 216 B zapisane, `Flash complete`; klawiatura zgłosiła się po USB jako `ac20:8009` „Anne Pro 2 C18 (QMK)”; funkcji nie sprawdzano (§2) |

Co to potwierdza z analizy oryginału: format ramek, polecenia `40 01`, `40 04`, `10 04`, `10 08` i zdarzenia `20 07`, `20 0c` (funkcjonalnie, nie bajt po bajcie); format `01 05` (surowy odczyt ADC, 16 bitów, little-endian) i konfigurację ADC (kanał 7, `HCLK/64`); bity jasności w raporcie multimediów (0x40, 0x80); kierunek przełącznika Bluetooth na PC10; układ odpowiedzi bootloadera na `02 03`.

## 2. Czego nie sprawdzono

Według zapisów w tej dokumentacji ostatni obraz wgrany na klawiaturę to `c18:kazmir_bo` z `4a0086b5ae` (2026-11-13, §1); wgrania późniejszego obrazu nie zapisano.

- Ramki budzącej BLE w formacie oryginału, numeru seryjnego DIS z UDID (`AP2_BLE_SERIAL`) i wskaźnika baterii z krzywą BLE (`6c03dfe813`, 2026-10-27): samego obrazu z `6c03dfe813` nie wgrywano, ale commit jest w obrazach wgranych 2026-11-06 i 2026-11-13; wyniku sprawdzenia tych funkcji nie zapisano. Procedura w §3.1.
- Limitów czasu połączenia i unpair z klawiatury (C18; są w obrazie z `c0a2cfcccb` wgranym 2026-11-06 i w późniejszych, funkcji jeszcze nie sprawdzano): tap slotu bez sparowanego hosta wraca na USB po około 6 s; tap slotu ze znanym, włączonym hostem zostaje na BLE; `FN2` + `5` przytrzymane 3 s kasuje bondy (krótkie naciśnięcie nic nie robi), a miganie na czerwono potwierdza; po opóźnionym starcie układu BLE numer seryjny i bateria nadal są wysyłane.
- Zmian z PR #2, #3 i #4 (od 2026-10-24 do 2026-11-02, `07-port-qmk-i-narzedzia.md` §6), m.in. utwardzenia łącza BLE, działania Bluetooth przy uśpionym hoście USB, parowania, które nie przenosi raportów na BLE przed połączeniem hosta, UART-u układu LED, który nie blokuje skanu, filtra pierwszego poziomu baterii i rzadszego skanu w bezczynności (`AP2_IDLE_SCAN_MS`): są w obrazach wgranych od 2026-11-06, ale wyników ich sprawdzenia nie zapisano.
- Poprawek z 2026-11-13 (`07-port-qmk-i-narzedzia.md` §6; są też w obrazie wgranym 2026-11-13, funkcji jeszcze nie sprawdzano):
  - `FN2` + `5` przytrzymane 3 s przy połączonym hoście BLE: host ma zostać rozłączony od razu (`40 02`), a po ponownym tapnięciu slotu nie może się już połączyć bez nowego parowania;
  - anulowanie migania slotu nie gasi koloru warstwy na Esc: przy trzymanym `FN2` naciśnięcie `6` (`KC_AP2_USB`) zostawia Esc w kolorze warstwy;
  - dwa tapnięcia slotu w ciągu 6 s bez dostępnego hosta, przy aktywnym hoście USB: raporty wracają na USB około 6 s po drugim tapnięciu;
  - tap slotu, a przed połączeniem przytrzymanie slotu (parowanie), przy aktywnym hoście USB: pisanie po USB działa od razu;
  - wskaźnik baterii (`FN2` + `8`) nadal zapala się w całości, także przy animowanym efekcie.
- Timera dla `wait_us()` (PR #26573, `07-port-qmk-i-narzedzia.md` §6; obraz wgrany 2026-11-13, klawiatura wstała, funkcji jeszcze nie sprawdzano): czy klawisze nie powtarzają się i nie pojawiają się klawisze widma przy kilku wciśniętych naraz (opóźnienie macierzy 30 µs zamiast 1 ms na wiersz), czy W nadal nie drga przy debounce 20 ms, czy pisanie po USB i BLE, uśpienie hosta USB i bezczynność na baterii działają jak przedtem. Opóźnienie macierzy jest teraz odliczane aktywnie, a nie przez uśpienie wątku, więc MCU nie zasypia między wierszami i czas pracy na baterii może się skrócić [I]. W tym obrazie pętla czeka po skanie (`AP2_IDLE_SCAN_MS`, 10 ms) tylko w bezczynności: gdy żaden host USB nie jest w użyciu, przez `AP2_IDLE_AFTER_MS` (10 s) nie było naciśnięcia ani zwolnienia klawisza, a LED nie świecą. Czekanie `AP2_AWAKE_SCAN_MS` (1 ms) po każdym skanie doszło dopiero w `66a979c586` (2026-11-16, niżej). Czasu pracy na baterii nie mierzono.
- Poprawek z 2026-11-16 (`07-port-qmk-i-narzedzia.md` §6.1; jeszcze niewgrane, kod zbudowano i sprawdzono tylko testami na komputerze, bez klawiatury):
  - `FN2` + `Esc` krótko (poniżej 1 s): nic się nie dzieje; `FN2` + `Esc` przytrzymane 1 s (po wciśnięciu `Esc` wystarczy trzymać sam `Esc`): klawiatura zgłasza się jako `04d9:8009` (IAP); najlepiej sprawdzić to przy następnym wgrywaniu obrazu zamiast wejścia przez `Esc` przy podłączaniu kabla, bo z IAP wychodzi się wgraniem obrazu (`annepro2_tools --boot`);
  - na baterii przez Bluetooth, z podświetleniem: szybkie pisanie i trzymanie klawiszy bez brakujących, podwójnych ani spóźnionych znaków; jeśli się da, porównanie prądu z zapalonymi LED i bez klawisza przed zmianą i po niej;
  - na baterii, LED wyłączone (`FN2` + `0`): `Shift` trzymany dłużej niż 10 s (`AP2_IDLE_AFTER_MS`), potem kilka liter z nim i zwolnienie: wielkie litery i zwolnienie działają normalnie (opóźnienia 10 ms nie widać; test sprawdza tylko, że nic się nie psuje);
  - przy połączonym hoście Bluetooth: kabel USB wyjęty podczas pisania po USB, przełącznik na Bluetooth, pisanie, LED wyłączone (`FN2` + `0`), 10 s przerwy: pierwszy klawisz nadal dociera do hosta Bluetooth;
  - uśpienie i wybudzenie komputera dwa razy przy klawiaturze na USB: LED gasną i wracają za każdym razem;
  - przełącznik Bluetooth na Off z hostem USB, tap slotu bez dostępnego hosta Bluetooth, przełącznik na On w ciągu 6 s, po 10 s pisanie: na hoście USB nic się nie pojawia;
  - poziom baterii wysyłany po restarcie układu BLE (`01 03` od BLE wywołuje `01 05`): sprawdzono tylko na komputerze (przypadek CFG-4 w `tests/test_fix_usbble.c`), bo właściciel nie może wymusić resetu układu BLE;
  - `FN2` + `7` przy włączonym Caps Lock i efekcie stałym, potem puszczenie `FN2`: wszystkie klawisze, także Caps Lock, są od razu ciemniejsze;
  - po starcie klawiatury (po wgraniu obrazu) przy Caps Lock włączonym na hoście: dioda Caps Lock świeci, a kolor warstwy pojawia się przy trzymanych `FN1` i `FN2` (konfiguracja układu LED powtórzona przy pierwszym sprawdzeniu, około 1 s po starcie);
  - tap slotu przy animowanym efekcie (zmiana efektu: `FN2` + `9`), host łączy się w trakcie: w chwili końca niebieskiego migania nie miga żaden wiersz w błędnych kolorach;
  - podświetlenie zgaszone po 3 minutach bez klawisza, potem naciśnięcie klawisza, około dziesięciu razy przy efekcie stałym: każdy wiersz zapala się od razu we właściwym kolorze.
- Zmian z 2026-11-19 (`07-port-qmk-i-narzedzia.md` §6.2; obrazu jeszcze nie wgrywano, kod zbudowano i sprawdzono tylko testami na komputerze, bez klawiatury):
  - podświetlenie po usunięciu tablicy wierszy w `rgb_driver.c`: efekt stały i animowany (`FN2` + `9`), dioda Caps Lock, wskaźnik baterii (`FN2` + `8`), miganie slotu i `FN2` + `7` działają jak przedtem;
  - po zapisaniu warunku czekania przy uśpionym hoście USB w jednej funkcji (`usb_sleep_pace()` w `annepro2.c`): uśpienie i wybudzenie komputera przy klawiaturze na USB (LED gasną i wracają) i bezczynność na baterii działają jak przedtem;
  - `KC_AP_LED_ON` i `KC_AP_LED_TOG` po poprawce: przy LED wyłączonych (`FN2` + `0`) i świecącej diodzie Caps Lock klawisz włącza LED; żadna keymapa w repozytorium nie wiąże tych klawiszy, więc w `kazmir_bo` trzeba by go najpierw dopisać.
- Budzenia uśpionego hosta USB klawiszem (remote wakeup) na tym porcie nie sprawdzano, ani pierwszego sygnału, ani ponawianego co 500 ms.
- Korekty 0 % baterii (`AP2_BATTERY_EMPTY_MV`) przy niskim napięciu.
- Zgodności procentu baterii z napięciem zmierzonym multimetrem (napięcie odniesienia 3,1 V i dzielnik 2:1 pochodzą z analizy firmware).
- Wersji układu BLE odebranej przez port: `annepro2_ble_version()` nie jest nigdzie wyświetlana.
- Parametrów połączenia i dystrybucji kluczy (nagranie `btmon`).
- Ruchu UART bajt po bajcie.
- Wgrywania obrazu BLE.

## 3. Procedury

### 3.1. Po wgraniu nowego obrazu

1. Caps Lock przez BLE (dioda) i gaszenie migania slotu po połączeniu.
2. Klawisze slotów: krótkie naciśnięcie łączy, przytrzymanie (`AP2_BLE_SLOT_HOLD_MS`) paruje.
3. Przełącznik Bluetooth, jeśli włączony: Off daje USB, On Bluetooth, powrót na Off gasi wskaźnik slotu; start z przełącznikiem na On pisze od razu przez Bluetooth.
4. Multimedia i jasność po BLE, `KC_AP2_BATT`, poziom baterii u hosta; liczba zapalonych klawiszy wskaźnika ma odpowiadać procentowi u hosta (jeden klawisz na 10 %, zaokrąglenie do najbliższego).
5. Z `AP2_BLE_SERIAL`: Device Information (Serial Number) po ponownym połączeniu pokazuje 32 znaki hex UDID zamiast `0123456789ABCDEF0123456789ABCDEF`; host może trzymać stare dane GATT w pamięci podręcznej, wtedy pomaga ponowne sparowanie.

### 3.2. Nagranie btmon

Cel: parametry połączenia, które host faktycznie dostaje (PPCP w GATT kontra żądanie aktualizacji z kodu BLE, `05-ble.md` §6 i §7), oraz przebieg parowania (Just Works, rozdzielane klucze). `btmon` wymaga uprawnień roota.

1. W jednym terminalu: `sudo btmon -w /tmp/annepro2.btsnoop`.
2. W drugim: odłącz i podłącz klawiaturę po BLE (krótkie naciśnięcie slotu). Do sprawdzenia parowania użyj slotu, którego nie używasz na tym komputerze: najpierw `bluetoothctl remove <adres>` dla starego wpisu, potem przytrzymanie slotu i parowanie od nowa.
3. Zatrzymaj `btmon` (Ctrl+C) i zachowaj plik do analizy.

Parowanie na slocie zastępuje bond tego slotu i nie rusza pozostałych. `40 05` (`KC_AP2_BT_UNPAIR`, `deleteBleHost` w ObinsKit) kasuje bondy wszystkich slotów, więc w tej procedurze go nie używaj.

### 3.3. Podsłuch UART

Najtańsze potwierdzenie ramek bajt po bajcie: analizator stanów logicznych na liniach MCU-BLE (PA4/PA5, USART1) albo MCU-LED (PB0/PB1, USART0), 115200 8N1, i porównanie z `02-protokol.md`. Alternatywą dla poleceń konfiguracji jest przechwycenie ruchu USB HID ObinsKit.
