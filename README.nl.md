# CryptONN — PHP-codebeveiliging en licentiesysteem

**Talen:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **De CryptONN Loader is gratis en vereist geen licentiesleutel.** Licentieverlening vindt plaats op het coderingsniveau, niet op het loaderniveau. Installeer eenmalig per server — hij beheert automatisch alle beveiligde applicaties.

---

## Wat is CryptONN?

CryptONN is een professioneel platform voor PHP-broncodebeveiliging en softwarelicentieverlening, ontwikkeld voor onafhankelijke softwareleveranciers (ISV's) en ontwikkelteams die PHP-applicaties commercieel distribueren. Het transformeert PHP-bronbestanden naar een ondoorzichtig, versleuteld binair formaat dat fundamenteel bestand is tegen reverse engineering, decompilatie en ongeautoriseerde verspreiding — met behoud van volledige runtimeprestaties en compatibiliteit met standaard PHP-infrastructuur.

Het systeem bestaat uit twee componenten: de **CryptONN Encoder** (een desktopapplicatie die de ontwikkelaar gebruikt om PHP-bestanden te beveiligen) en de **CryptONN Loader** (deze repository — een enkel PHP-bestand dat op de server van de eindklant wordt geïnstalleerd om beveiligde bestanden transparant uit te voeren).

---

## Opgeloste problemen

| Probleem | Hoe CryptONN het aanpakt |
|---|---|
| **Diefstal van broncode** | PHP-logica wordt getransformeerd naar een versleutelde binaire payload. Zelfs met volledige toegang tot het bestandssysteem kan de originele broncode niet worden gereconstrueerd. |
| **Ongeautoriseerde implementatie** | Elk beveiligd bestand bevat een ingebedde licentie-identificator die server-side wordt gevalideerd. Bestanden gekopieerd naar niet-gelicenseerde servers weigeren uit te voeren. |
| **Handhaving van licentievoorwaarden** | Proefperioden en vervaldatums worden afgedwongen op de licentieserver. Er zijn geen client-side controles die omzeild kunnen worden. |
| **Multi-tenant distributie** | Dezelfde codebasis kan worden gelicenseerd aan meerdere klanten, elk met unieke voorwaarden, gebruikslimieten en domeinbeperkingen. |
| **Ongeautoriseerde verspreiding** | Beveiligde bestanden zijn gebonden aan specifieke licentie-identificatoren — ze zijn waardeloos zonder een actieve, geldige licentie. |

---

## Hoe het werkt

**Bij codering** (machine van de ontwikkelaar):
Een PHP-bestand wordt verwerkt door de CryptONN Encoder. De uitvoer is een `.cryptonn`-binair bestand met een versleutelde payload en een ingebedde licentie-identificator. De originele PHP-broncode is in geen enkele vorm aanwezig in de uitvoer.

**Tijdens uitvoering** (server van de klant):
1. PHP probeert een `.cryptonn`-bestand uit te voeren
2. De Loader onderschept de uitvoering via het PHP `auto_prepend_file`-mechanisme
3. De Loader leest de ingebedde licentie-identificator uit de bestandsheader
4. De Loader neemt contact op met de CryptONN-licentie-API, met de licentie-identificator en een unieke vingerafdruk afgeleid van de netwerkidentiteit van de server
5. De API valideert de licentie en retourneert een decryptiesleutel, versleuteld specifiek voor deze server
6. De Loader decrypteert de PHP-payload volledig in het geheugen
7. De gedecrypteerde PHP-code wordt natively uitgevoerd — er worden geen tijdelijke bestanden met broncode op schijf bewaard

---

## Beveiligingsmodel

| Eigenschap | Detail |
|---|---|
| **Sleutels opgeslagen op server** | Geen. Er worden geen cryptografische sleutels opgeslagen in het bestandssysteem of de omgeving. |
| **Serverbinding** | Elke server heeft een unieke vingerafdruk afgeleid van zijn netwerkidentiteit. Een decryptiebundel geldig voor één server is cryptografisch nutteloos op elke andere. |
| **API-communicatie** | Alle sleutelaflevering vindt plaats via versleutelde HTTPS-kanalen. Sleutels worden aanvullend versleuteld met een serverspecifieke omhullingssleutel vóór transmissie. |
| **Offline tolerantie** | Een drielaagse cache (in-process → bestandsgebaseerd 24u → respijtperiode 72u) zorgt voor werking tijdens tijdelijke API-storingen. |
| **Handhaving van proefversie** | Proeflicenties handhaven strikte server-side vervaldatum. Geen offline respijtperiode is van toepassing — de API moet de geldigheid bevestigen voor proeflicenties bij elke cachemisser. |
| **Tamperdetectie** | Afgeknotte, gewijzigde of beschadigde `.cryptonn`-bestanden worden gedetecteerd en afgewezen vóór elke decryptiepoging. |
| **Geen leesbare tekst op schijf** | Gedecrypteerde PHP-code wordt nooit naar een permanente locatie geschreven. Tijdelijke uitvoeringsbestanden worden direct na gebruik verwijderd. |

---

## PHP-compatibiliteit

| PHP-versie | Status |
|---|---|
| PHP 7.2 | ✅ Volledig ondersteund |
| PHP 7.3 | ✅ Volledig ondersteund |
| PHP 7.4 | ✅ Volledig ondersteund |
| PHP 8.0 | ✅ Volledig ondersteund |
| PHP 8.1 | ✅ Volledig ondersteund |
| PHP 8.2 | ✅ Volledig ondersteund |
| PHP 8.3 | ✅ Volledig ondersteund |
| PHP 8.4 | ✅ Volledig ondersteund |
| PHP 8.5 | ✅ Volledig ondersteund |
| PHP 5.x · 7.0 · 7.1 | ❌ Niet ondersteund |

---

## Systeemvereisten

| Component | Vereiste | Opmerkingen |
|---|---|---|
| PHP | 7.2 – 8.5 | Alle minor versies ondersteund |
| ext-sodium | Elke versie | Meegeleverd met PHP 7.2+ — geen afzonderlijke installatie vereist |
| ext-openssl | Elke versie | Standaard beschikbaar in vrijwel alle hostingomgevingen |
| Uitgaand HTTPS | Poort 443 | Vereist voor API-aanroepen voor licentievalidatie |
| Schijf (cache) | ~10 KB per actieve licentie | Tijdelijke bestanden in systeem-tempmap |
| APCu (optioneel) | Elke versie | Maakt in-process caching mogelijk; vermindert cold-start latentie aanzienlijk |

---

## Installatie

### Stap 1 — Loader downloaden

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
sudo chown root:root /opt/cryptonn/cryptonn-loader.php
```

### Stap 2 — PHP configureren (kies uw omgeving)

**cPanel / EasyApache 4**
```bash
echo "auto_prepend_file = /opt/cryptonn/cryptonn-loader.php" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache && /scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — `.user.ini`**
```ini
auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
```

**Dedicated server — PHP-FPM**
```ini
php_admin_value[auto_prepend_file] = /opt/cryptonn/cryptonn-loader.php
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess`**
```apache
php_value auto_prepend_file /opt/cryptonn/cryptonn-loader.php
```

### Stap 3 — Installatie verifiëren

Sla het volgende op als `/tmp/cnn-verify.php` en voer het uit:

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Actief\n" : "❌ CryptONN Loader: Niet geladen\n";
echo "PHP-versie  : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ ONTBREEKT") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ ONTBREEKT") . "\n";
echo "APCu        : " . (function_exists('apcu_store') ? "✅ Beschikbaar" : "— Niet beschikbaar (optioneel)") . "\n";
```

```bash
php /tmp/cnn-verify.php
```

---

## Probleemoplossing

### `CryptONN Loader requires ext-sodium`
**Oorzaak:** De `sodium`-extensie is niet ingeschakeld voor de actieve PHP-versie.

```bash
# cPanel / EasyApache 4
/scripts/install_ea_metapackage ea-php82-php-sodium

# AlmaLinux / RHEL / CentOS 8+
dnf install php-sodium

# Ubuntu / Debian
apt-get install php8.2-sodium

# Verifiëren
php -m | grep sodium
```

---

### `CryptONN Loader requires ext-openssl`
**Oorzaak:** De `openssl`-extensie is niet ingeschakeld.

Schakel `extension=openssl` in in de relevante `php.ini`, of installeer het `php-openssl`-pakket via uw pakketbeheerder.

---

### `Master key could not be retrieved`
**Oorzaak:** De Loader kan de CryptONN-licentie-API niet bereiken. Dit kan worden veroorzaakt door een firewallregel die uitgaand HTTPS blokkeert, een DNS-resolutiefout of een tijdelijk netwerkprobleem.

```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

**Oplossingen:**
- Zorg ervoor dat uitgaande TCP-poort 443 is toegestaan vanaf de server
- Controleer of de server externe DNS-namen kan oplossen
- Als u achter een uitgaande proxy zit, configureer dan de omgevingsvariabele `CRYPTONN_API_URL`

---

### `Invalid magic bytes`
**Oorzaak:** Het bestand is geen geldig CryptONN-gecodeerd bestand, of het bestand is beschadigd tijdens de overdracht (bijv. overgedragen in tekstmodus in plaats van binaire modus).

**Oplossing:** Draag het `.cryptonn`-bestand opnieuw over in binaire modus. Open of bewerk het bestand niet met een teksteditor.

---

### `Incomplete header`
**Oorzaak:** Het `.cryptonn`-bestand is afgeknot — het is niet volledig overgedragen.

**Oplossing:** Draag het bestand opnieuw over. Controleer de beschikbare schijfruimte op zowel de bron als de bestemming.

---

### `Decryption failed`
**Oorzaak:** De decryptiesleutel die door de API is geretourneerd, komt niet overeen met de versleutelingsparameters van het bestand.

**Oplossing:** Bevestig bij de softwareleverancier dat de juiste licentie-identificator is gebruikt bij het coderen van het bestand.

---

### `Temporary file could not be written`
**Oorzaak:** Het PHP-proces heeft geen schrijfrechten voor de systeem-tijdelijke map.

**Oplossing:** Zorg ervoor dat de webservergebruiker schrijftoegang heeft tot `sys_get_temp_dir()` (doorgaans `/tmp`). Controleer SELinux- of AppArmor-beleid op geharde systemen.

---

## Prestaties

| Cachelaag | Typische latentie | Duur |
|---|---|---|
| In-process (APCu) | < 0,1 ms | 1 uur |
| Bestandscache | < 0,5 ms | 24 uur |
| API-aanroep (koud) | 50 – 200 ms | Bij cachemisser |
| Respijtperiode | < 0,5 ms | Tot 72 uur (niet-proef) |

APCu wordt automatisch gebruikt wanneer beschikbaar. Er is geen extra configuratie vereist.

---

## Veelgestelde vragen

**V: Is de Loader gratis?**  
A: Ja. De CryptONN Loader is gratis en open-source. Er is geen licentiesleutel, abonnement of vergoeding verbonden aan de installatie op een willekeurig aantal servers.

**V: Werkt het met PHP OPcache?**  
A: Ja. OPcache werkt op de PHP-bytecode nadat de Loader de code heeft gedecrypteerd en uitgevoerd. De interactie is volledig transparant en correct.

**V: Kan één Loader-installatie meerdere applicaties bedienen?**  
A: Ja. Eén Loader-installatie verwerkt alle `.cryptonn`-bestanden op de server, in alle applicaties en PHP-versies die ernaar verwijzen via `auto_prepend_file`.

**V: Wat gebeurt er tijdens een API-storing?**  
A: Niet-proeflicenties blijven normaal werken gedurende maximaal 72 uur met behulp van de bestandscache. Proeflicenties vereisen een succesvolle API-reactie bij elke cachemisser — ze profiteren niet van de respijtperiode.

**V: Vormen gecachte gegevens een beveiligingsrisico?**  
A: Nee. De gecachte bundel is versleuteld met een sleutel afgeleid van de unieke vingerafdruk van de server. Het kan niet worden gedecrypteerd op een andere machine en bevat de ruwe decryptiesleutel niet in enige bruikbare vorm.

---

## Loader verwijderen

1. Verwijder de `auto_prepend_file`-instructie uit `php.ini`, `.user.ini` of `.htaccess`
2. Herstart PHP-FPM of Apache
3. Verwijder de loader-map:
```bash
rm -rf /opt/cryptonn
```

Dit heeft geen invloed op `.cryptonn`-bestanden — ze worden eenvoudigweg niet-uitvoerbaar totdat een Loader opnieuw is geïnstalleerd.

---

## Ondersteuning

| Kanaal | Link |
|---|---|
| Documentatie | [laicos.com.tr](https://laicos.com.tr) |
| Issue-tracker | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN is een product van LAICOS Technology.*
