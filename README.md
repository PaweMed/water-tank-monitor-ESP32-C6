ESP32 Water Monitor (ESP32-C6) – Sterownik poziomu cieczy

Krótko o działaniu

Urządzenie na bieżąco monitoruje poziom cieczy w zbiorniku przy pomocy czujników. Włącza lub wyłącza przekaźnik (pompę) zgodnie z aktualnym stanem oraz wybranym trybem pracy. Stan systemu, historia oraz konfiguracja dostępne są przez stronę WWW. O wszystkich istotnych zmianach informuje użytkownika przez powiadomienia PUSH (Pushover). System zapewnia ciągłość pracy i ochronę przed błędami dzięki watchdogowi i mechanizmom bezpieczeństwa.

Opis projektu

Program przeznaczony dla mikrokontrolera ESP32-C6 sterującego poziomem wody w zbiorniku z obsługą przekaźnika, wizualizacją WWW, powiadomieniami i trybem awaryjnym.

Najważniejsze funkcje
Automatyczne sterowanie pompą na podstawie odczytów z czujników poziomu (dolny, górny, opcjonalnie środkowy).

Ręczne sterowanie pompą przez stronę WWW lub fizyczny przycisk.

Tryb testowy (symulacja czujników do diagnostyki systemu).

Zabezpieczenia przed zbyt częstym przełączaniem przekaźnika (licznik przełączeń, odstępy czasowe).

Wizualizacja poziomu wody na stronie WWW (zbiornik, stan czujników, status systemu).

Rejestr zdarzeń – podgląd historii działań i komunikatów systemowych przez WWW.

Konfiguracja sieci WiFi oraz parametrów (piny, powiadomienia, hasła) przez wygodny panel www.

Powiadomienia PUSH (Pushover) o kluczowych zdarzeniach (np. niski poziom wody, wyłączenie pompy, utrata WiFi).

Obsługa OTA – aktualizacja oprogramowania przez sieć (WWW).

Tryb awaryjny AP – gdy brak WiFi, ESP32 wystawia własną sieć do konfiguracji i kontroli.

Watchdog sprzętowy – automatyczny restart w przypadku zawieszenia się systemu.

Automatyczny reconnect WiFi – sterownik samodzielnie odzyskuje połączenie bez przerywania pracy automatyki.


