Rozwiązanie zadania drugiego

Grzegorz Kołakowski, gk291583

I Pliki
=======
    - crcdev.c - implementacja sterownik
    - crcdev_structs.h - definicje pomocniczych struktur
    - crcdev.h
    - crcdev_ioctl.h
    - Makefile
    - Kbuild
    - README

II Implementacja
================

Sterownik używa bloku wczytywania danych i wykorzystuje wszystkie konteksty
sprzętowe.
Sterownik NIE używa bloku wczytywania poleceń.

Działanie sterownika
--------------------

W momencie otwarcia pliku tworzony jest kontekst.
W momencie wywołania write, czekamy na wolny kontekst urządzenia. Gdy
dostaniemy kontekst, ustawiamy odpowiednio rejestry kontekstu, potem kopiujemy
dane (możliwe że tylko część, gdy bufor jest za mały) do bufora DMA. Następnie
czekamy, aż blok wczytywania danych będzie wolny i gdy go dostaniemy na
wyłączność, wykonują się operację CRC. Jeśli nie wszystkie dane zostały
przetworzone, "zwalniamy" blok wczytywania danych i wracamy na początek
pętli - kopiujemy dane do bufora i ponownie czekamy na dostęp do bloku
wczytywania danych.
Gdy przetworzymy wszystkie dane, kopiujemy dane z urządzenia do odpowiednich
struktur, a następnie "oddajemy" kontekst.

Usuwanie urządzenia
-------------------

Urządzenie czeka, aż wszystkie otwarte pliki zostaną zamknięte, dopiero wówczas
urządzenie jest odłączane. W momencie wywołania funkcji usuwającej, ustawiana
jest flaga odrzucająca wszystkie dalsze próby otwarcia nowego pliku. Zwalniane
są zasoby zajmowane do obsługi urządzenia, przywracane są domyślne wartości
rejestrów ENABLE, INTR_ENABLE.

Usuwanie sterownika
-------------------

Zanim sterownik zostanie usunięty, czekamy, aż wszystkie urządzenia zakończą
swoją pracę (jak wyżej).

III Ocena
=========

Testy
-----

simple          16/16 [1p]
long            16/16 [1p]
thread          16/16 [1p]
mux             16/16 [1p]
rmux            16/16 [1p]

Jakość rozwiązania
------------------

1. Rozwiązanie używa bloku wczytywania danych [2.5p]
2. Rozwiązanie używa 4 kontekstów sprzętowych [1p]

Sprawdzenie kodu
----------------

1. Sterownik wypełnia pole .ioctl struktury file_operations zamiast
   unlocked_ioctl + compat_ioctl, tym samym używając Big Kernel Locka [-0.1p]
2. crcdev_irq_handler:
  - funkcja zwraca IRQ_HANDLED zawsze, gdy w CRCDEV_INTR są włączone bity -
    nawet, jeśli nie są włączone w INTR_ENABLE. W połączeniu z faktem, że
    np. CMD_IDLE jest cały czas włączone, gdy urządzenie nie jest w użyciu,
    sprawia że sterownik jest całkowicie nieużywalny w przypadku wspólnej
    linii przerwań [-1p]
3. crcdev_open:
  - funkcja zwraca 1 zamiast kodu błędu [-0.2p]
4. crcdev_write:
  - funkcja zwraca 0 zamiast kodu błędu, w przypadku błędu przed zapisaniem
    czegokolwiek [-0.2p]
5. crcdev_ioctl:
  - funkcja zwraca -EINTR zamiast -ERESTARTSYS [-0.2p]
6. crcdev_probe:
  - device_create wywoływana przed wpisaniem urządzenia do crc_devices,
    pozwalając użytkownikowi na otwarcie go zanim będzie gotowe [-0.3p]

Suma: 6.5/10 
