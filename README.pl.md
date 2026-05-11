# CryptONN — Ochrona Kodu PHP i System Licencjonowania

**Języki:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **CryptONN Loader jest bezpłatny i nie wymaga klucza licencyjnego.** Licencjonowanie odbywa się na etapie kodowania, nie na poziomie loadera. Zainstaluj raz na serwerze — automatycznie obsługuje wszystkie chronione aplikacje.

---

## Czym jest CryptONN?

CryptONN to profesjonalna platforma do ochrony kodu źródłowego PHP i zarządzania licencjami oprogramowania, stworzona dla niezależnych dostawców oprogramowania (ISV) i zespołów deweloperskich dystrybuujących aplikacje PHP komercyjnie. Transformuje pliki PHP w nieprzezroczysty, zaszyfrowany format binarny, który jest fundamentalnie odporny na inżynierię wsteczną, dekompilację i nieautoryzowaną redystrybucję — zachowując pełną wydajność środowiska uruchomieniowego i kompatybilność ze standardową infrastrukturą PHP.

System składa się z dwóch komponentów: **CryptONN Encoder** (aplikacja desktopowa używana przez dewelopera do ochrony plików PHP) oraz **CryptONN Loader** (to repozytorium — pojedynczy plik PHP instalowany na serwerze klienta końcowego do przezroczystego wykonywania chronionych plików).

---

## Rozwiązywane problemy

| Problem | Jak CryptONN to rozwiązuje |
|---|---|
| **Kradzież kodu źródłowego** | Logika PHP jest transformowana w zaszyfrowany payload binarny. Nawet przy pełnym dostępie do systemu plików oryginalny kod źródłowy nie może zostać odtworzony. |
| **Nieautoryzowane wdrożenie** | Każdy chroniony plik zawiera wbudowany identyfikator licencji weryfikowany po stronie serwera. Pliki skopiowane na nielicencjonowane serwery odmawiają wykonania. |
| **Egzekwowanie warunków licencji** | Okresy próbne i daty wygaśnięcia są wymuszane na serwerze licencyjnym. Nie ma żadnych sprawdzeń po stronie klienta, które można obejść. |
| **Dystrybucja wielodostępna** | Ta sama baza kodu może być licencjonowana wielu klientom, każdemu z unikalnymi warunkami, limitami użytkowania i ograniczeniami domenowymi. |
| **Nieautoryzowana redystrybucja** | Chronione pliki są powiązane z konkretnymi identyfikatorami licencji — są bezużyteczne bez aktywnej, ważnej licencji. |

---

## Jak to działa

**Przy kodowaniu** (maszyna dewelopera):
Plik PHP jest przetwarzany przez CryptONN Encoder. Wynikiem jest plik binarny `.cryptonn` zawierający zaszyfrowany payload i wbudowany identyfikator licencji. Oryginalny kod źródłowy PHP nie jest obecny w danych wyjściowych w żadnej formie.

**Podczas wykonywania** (serwer klienta):
1. PHP próbuje wykonać plik `.cryptonn`
2. Loader przechwytuje wykonanie poprzez mechanizm `auto_prepend_file` PHP
3. Loader odczytuje wbudowany identyfikator licencji z nagłówka pliku
4. Loader kontaktuje się z API licencjonowania CryptONN, przedstawiając identyfikator licencji i unikalny odcisk palca wyprowadzony z tożsamości sieciowej serwera
5. API weryfikuje licencję i zwraca klucz deszyfrujący, zaszyfrowany specyficznie dla tego serwera
6. Loader deszyfruje payload PHP całkowicie w pamięci
7. Odszyfrowany kod PHP wykonuje się natywnie — żadne pliki tymczasowe zawierające kod źródłowy nie są przechowywane na dysku

---

## Model bezpieczeństwa

| Właściwość | Szczegóły |
|---|---|
| **Klucze przechowywane na serwerze** | Żadne. Żadne klucze kryptograficzne nie są przechowywane w systemie plików ani środowisku. |
| **Powiązanie z serwerem** | Każdy serwer ma unikalny odcisk palca wyprowadzony z jego tożsamości sieciowej. Pakiet deszyfrowania ważny dla jednego serwera jest kryptograficznie bezużyteczny na jakimkolwiek innym. |
| **Komunikacja z API** | Cała dostawa kluczy odbywa się przez zaszyfrowane kanały HTTPS. Klucze są dodatkowo szyfrowane kluczem owijającym specyficznym dla serwera przed transmisją. |
| **Tolerancja offline** | Trójwarstwowy cache (w procesie → plikowy 24h → okres karencji 72h) zapewnia działanie podczas tymczasowych awarii API. |
| **Egzekwowanie wersji próbnej** | Licencje próbne mają rygorystyczne wygaśnięcie po stronie serwera. Żaden okres karencji offline nie ma zastosowania — API musi potwierdzić ważność licencji próbnych przy każdym nietrafieniu cache. |
| **Wykrywanie manipulacji** | Skrócone, zmodyfikowane lub uszkodzone pliki `.cryptonn` są wykrywane i odrzucane przed jakąkolwiek próbą deszyfrowania. |
| **Brak tekstu jawnego na dysku** | Odszyfrowany kod PHP nigdy nie jest zapisywany w trwałej lokalizacji. Tymczasowe pliki wykonania są natychmiast usuwane po użyciu. |

---

## Kompatybilność z PHP

| Wersja PHP | Status |
|---|---|
| PHP 7.2 | ✅ W pełni obsługiwana |
| PHP 7.3 | ✅ W pełni obsługiwana |
| PHP 7.4 | ✅ W pełni obsługiwana |
| PHP 8.0 | ✅ W pełni obsługiwana |
| PHP 8.1 | ✅ W pełni obsługiwana |
| PHP 8.2 | ✅ W pełni obsługiwana |
| PHP 8.3 | ✅ W pełni obsługiwana |
| PHP 8.4 | ✅ W pełni obsługiwana |
| PHP 8.5 | ✅ W pełni obsługiwana |
| PHP 5.x · 7.0 · 7.1 | ❌ Nieobsługiwana |

---

## Wymagania systemowe

| Komponent | Wymaganie | Uwagi |
|---|---|---|
| PHP | 7.2 – 8.5 | Wszystkie wersje podrzędne obsługiwane |
| ext-sodium | Dowolna wersja | Dołączony do PHP 7.2+ — oddzielna instalacja nie jest wymagana |
| ext-openssl | Dowolna wersja | Dostępny domyślnie praktycznie we wszystkich środowiskach hostingowych |
| Wychodzący HTTPS | Port 443 | Wymagany do wywołań API weryfikacji licencji |
| Dysk (cache) | ~10 KB na aktywną licencję | Pliki tymczasowe w katalogu systemowym temp |
| APCu (opcjonalnie) | Dowolna wersja | Włącza buforowanie w procesie; znacznie zmniejsza opóźnienie zimnego startu |

---

## Instalacja

### Krok 1 — Pobieranie loadera

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh \
     -o /opt/cryptonn/
sudo chmod 644 /opt/cryptonn/
sudo chown root:root /opt/cryptonn/
```

### Krok 2 — Konfiguracja PHP (wybierz środowisko)

**cPanel / EasyApache 4**
```bash
echo "extension=cryptonn" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache && /scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — `.user.ini`**
```ini
extension=cryptonn
```

**Serwer dedykowany — PHP-FPM**
```ini
php_admin_value[auto_prepend_file] = /opt/cryptonn/
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess`**
```apache
php_value auto_prepend_file /opt/cryptonn/
```

### Krok 3 — Weryfikacja instalacji

Zapisz poniższy kod jako `/tmp/cnn-verify.php` i uruchom:

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Aktywny\n" : "❌ CryptONN Loader: Niezaładowany\n";
echo "Wersja PHP  : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ BRAK") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ BRAK") . "\n";
echo "APCu        : " . (function_exists('apcu_store') ? "✅ Dostępny" : "— Niedostępny (opcjonalnie)") . "\n";
```

```bash
php /tmp/cnn-verify.php
```

---

## Rozwiązywanie problemów

### `CryptONN Loader requires ext-sodium`
**Przyczyna:** Rozszerzenie `sodium` nie jest włączone dla aktywnej wersji PHP.

```bash
# cPanel / EasyApache 4
/scripts/install_ea_metapackage ea-php82-php-sodium

# AlmaLinux / RHEL / CentOS 8+
dnf install php-sodium

# Ubuntu / Debian
apt-get install php8.2-sodium

# Weryfikacja
php -m | grep sodium
```

---

### `CryptONN Loader requires ext-openssl`
**Przyczyna:** Rozszerzenie `openssl` nie jest włączone.

Włącz `extension=openssl` w odpowiednim `php.ini` lub zainstaluj pakiet `php-openssl` przez menedżer pakietów.

---

### `Master key could not be retrieved`
**Przyczyna:** Loader nie może dotrzeć do API licencjonowania CryptONN. Może to być spowodowane regułą zapory blokującą wychodzący HTTPS, błędem rozwiązywania DNS lub tymczasowym problemem sieciowym.

```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

**Rozwiązania:**
- Upewnij się, że wychodzący port TCP 443 jest dozwolony z serwera
- Sprawdź, czy serwer może rozwiązywać zewnętrzne nazwy DNS
- Jeśli za proxy wychodzącym, skonfiguruj zmienną środowiskową `CRYPTONN_API_URL`

---

### `Invalid magic bytes`
**Przyczyna:** Plik nie jest prawidłowym plikiem zakodowanym przez CryptONN lub został uszkodzony podczas transferu (np. przesłany w trybie tekstowym zamiast binarnego).

**Rozwiązanie:** Ponownie prześlij plik `.cryptonn` w trybie binarnym. Nie otwieraj ani nie edytuj pliku edytorem tekstu.

---

### `Incomplete header`
**Przyczyna:** Plik `.cryptonn` jest skrócony — nie został przesłany całkowicie.

**Rozwiązanie:** Ponownie prześlij plik. Sprawdź dostępne miejsce na dysku zarówno na źródle, jak i miejscu docelowym.

---

### `Decryption failed`
**Przyczyna:** Klucz deszyfrowania zwrócony przez API nie odpowiada parametrom szyfrowania pliku.

**Rozwiązanie:** Potwierdź z dostawcą oprogramowania, że przy kodowaniu pliku użyto właściwego identyfikatora licencji.

---

### `Temporary file could not be written`
**Przyczyna:** Proces PHP nie ma uprawnień do zapisu w systemowym katalogu tymczasowym.

**Rozwiązanie:** Upewnij się, że użytkownik serwera WWW ma dostęp do zapisu w `sys_get_temp_dir()` (zwykle `/tmp`). Sprawdź zasady SELinux lub AppArmor na zabezpieczonych systemach.

---

## Wydajność

| Warstwa cache | Typyczne opóźnienie | Czas trwania |
|---|---|---|
| W procesie (APCu) | < 0,1 ms | 1 godzina |
| Cache plikowy | < 0,5 ms | 24 godziny |
| Wywołanie API (zimne) | 50 – 200 ms | Przy nietrafieniu cache |
| Okres karencji | < 0,5 ms | Do 72 godzin (nietrialowe) |

APCu jest używany automatycznie, gdy jest dostępny. Nie jest wymagana żadna dodatkowa konfiguracja.

---

## Często zadawane pytania

**P: Czy Loader jest bezpłatny?**  
O: Tak. CryptONN Loader jest bezpłatny i open-source. Nie ma klucza licencyjnego, subskrypcji ani opłaty związanej z instalacją na dowolnej liczbie serwerów.

**P: Czy działa z PHP OPcache?**  
O: Tak. OPcache działa na kodzie bajtowym PHP po tym, jak Loader odszyfrował i wykonał kod. Interakcja jest w pełni przezroczysta i prawidłowa.

**P: Czy jedna instalacja Loadera może obsługiwać wiele aplikacji?**  
O: Tak. Jedna instalacja Loadera obsługuje wszystkie pliki `.cryptonn` na serwerze, we wszystkich aplikacjach i wersjach PHP, które odwołują się do niego przez `auto_prepend_file`.

**P: Co się dzieje podczas awarii API?**  
O: Licencje nietrialowe kontynuują normalne działanie przez do 72 godzin, korzystając z cache plikowego. Licencje próbne wymagają pomyślnej odpowiedzi API przy każdym nietrafieniu cache — nie korzystają z okresu karencji.

**P: Czy dane w cache stanowią zagrożenie bezpieczeństwa?**  
O: Nie. Zbuforowany pakiet jest zaszyfrowany kluczem wyprowadzonym z unikalnego odcisku palca serwera. Nie może zostać odszyfrowany na żadnej innej maszynie i nie zawiera surowego klucza deszyfrowania w żadnej użytecznej formie.

---

## Usuwanie loadera

1. Usuń dyrektywę `auto_prepend_file` z `php.ini`, `.user.ini` lub `.htaccess`
2. Uruchom ponownie PHP-FPM lub Apache
3. Usuń katalog loadera:
```bash
rm -rf /opt/cryptonn
```

Nie ma to wpływu na żadne pliki `.cryptonn` — po prostu stają się niewykonywalne do ponownej instalacji Loadera.

---

## Wsparcie

| Kanał | Link |
|---|---|
| Dokumentacja | [laicos.com.tr](https://laicos.com.tr) |
| Śledzenie problemów | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN jest produktem LAICOS Technology.*
