# CryptONN — Protection du Code PHP et Système de Licences

**Langues :** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **Le CryptONN Loader est gratuit et ne nécessite aucune clé de licence.** La gestion des licences s'effectue à l'étape de l'encodage. Installez-le une seule fois par serveur — il gère automatiquement toutes les applications protégées.

---

## Qu'est-ce que CryptONN ?

CryptONN est une plateforme professionnelle de protection du code source PHP et de gestion des licences logicielles, conçue pour les éditeurs de logiciels indépendants (ISV) et les équipes de développement qui distribuent des applications PHP à titre commercial. Elle transforme les fichiers sources PHP en un format binaire chiffré, fondamentalement résistant à la rétro-ingénierie, à la décompilation et à la redistribution non autorisée — tout en préservant des performances natives et une compatibilité totale avec les infrastructures PHP standard.

---

## Problèmes résolus

| Problème | Solution CryptONN |
|---|---|
| **Vol du code source** | La logique PHP est transformée en charge utile binaire chiffrée. Même avec un accès complet au système de fichiers, le source original ne peut pas être reconstitué. |
| **Déploiement non autorisé** | Chaque fichier protégé contient un identifiant de licence embarqué, validé côté serveur. Les fichiers copiés sur des serveurs non licenciés refusent de s'exécuter. |
| **Application des conditions de licence** | Les périodes d'essai et les dates d'expiration sont appliquées côté serveur. Aucun contournement côté client n'est possible. |
| **Distribution multi-clients** | Une même base de code peut être licenciée à plusieurs clients, chacun avec des conditions, limites d'utilisation et restrictions de domaine uniques. |

---

## Compatibilité PHP

| Version PHP | Statut |
|---|---|
| PHP 7.2 | ✅ Entièrement pris en charge |
| PHP 7.3 | ✅ Entièrement pris en charge |
| PHP 7.4 | ✅ Entièrement pris en charge |
| PHP 8.0 | ✅ Entièrement pris en charge |
| PHP 8.1 | ✅ Entièrement pris en charge |
| PHP 8.2 | ✅ Entièrement pris en charge |
| PHP 8.3 | ✅ Entièrement pris en charge |
| PHP 8.4 | ✅ Entièrement pris en charge |
| PHP 8.5 | ✅ Entièrement pris en charge |
| PHP 5.x · 7.0 · 7.1 | ❌ Non pris en charge |

---

## Configuration requise

| Composant | Prérequis | Notes |
|---|---|---|
| PHP | 7.2 – 8.5 | Toutes les versions mineures prises en charge |
| ext-sodium | Toute version | Inclus avec PHP 7.2+ |
| ext-openssl | Toute version | Disponible par défaut dans pratiquement tous les environnements d'hébergement |
| HTTPS sortant | Port 443 | Requis pour les appels à l'API de validation des licences |
| APCu (optionnel) | Toute version | Active la mise en cache en mémoire |

---

## Installation

### Étape 1 — Télécharger le Loader

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-extension/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
```

### Étape 2 — Configurer PHP

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

**Serveur nu — PHP-FPM**
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

### Étape 3 — Vérifier l'installation

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader : Actif\n" : "❌ CryptONN Loader : Non chargé\n";
echo "Version PHP : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ MANQUANT") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ MANQUANT") . "\n";
```

---

## Résolution des problèmes

### `CryptONN Loader requires ext-sodium`
L'extension `sodium` n'est pas activée pour la version PHP active.
```bash
# AlmaLinux / RHEL / CentOS
dnf install php-sodium
# Ubuntu / Debian
apt-get install php8.2-sodium
# cPanel
/scripts/install_ea_metapackage ea-php82-php-sodium
```

### `CryptONN Loader requires ext-openssl`
Activez `extension=openssl` dans `php.ini` ou installez le paquet `php-openssl`.

### `Master key could not be retrieved`
Le Loader ne peut pas atteindre l'API de licences CryptONN.
```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```
Vérifiez que le port TCP 443 sortant est autorisé depuis le serveur.

### `Invalid magic bytes`
Le fichier n'est pas un fichier CryptONN valide ou a été corrompu lors du transfert. Retransférez le fichier `.cryptonn` en mode binaire.

### `Incomplete header`
Le fichier `.cryptonn` est tronqué. Retransférez le fichier et vérifiez l'espace disque disponible.

### `Decryption failed`
La clé retournée par l'API ne correspond pas aux paramètres de chiffrement du fichier. Contactez l'éditeur du logiciel.

### `Temporary file could not be written`
Le processus PHP n'a pas les droits d'écriture sur le répertoire temporaire système. Vérifiez les politiques SELinux ou AppArmor.

---

## Performances

| Couche de cache | Latence typique | Durée |
|---|---|---|
| En mémoire (APCu) | < 0,1 ms | 1 heure |
| Cache fichier | < 0,5 ms | 24 heures |
| Appel API (froid) | 50 – 200 ms | En cas de cache miss |
| Période de grâce | < 0,5 ms | Jusqu'à 72 heures (hors essai) |

---

## Foire aux questions

**Q : Le Loader est-il gratuit ?**  
R : Oui. Le CryptONN Loader est gratuit et open-source, sans clé de licence ni abonnement.

**Q : Fonctionne-t-il avec PHP OPcache ?**  
R : Oui. OPcache opère sur le bytecode PHP après que le Loader a déchiffré et exécuté le code.

**Q : Que se passe-t-il en cas de panne de l'API ?**  
R : Les licences non-essai continuent de fonctionner jusqu'à 72 heures depuis le cache fichier. Les licences d'essai nécessitent une réponse API réussie à chaque cache miss.

---

## Désinstallation

```bash
# 1. Supprimer la directive auto_prepend_file et redémarrer PHP
# 2. Supprimer le répertoire du loader
rm -rf /opt/cryptonn
```

---

## Support

| Canal | Lien |
|---|---|
| Documentation | [laicos.com.tr](https://laicos.com.tr) |
| Suivi des problèmes | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-extension/issues) |

---

*© 2026 LAICOS Technology. CryptONN est un produit de LAICOS Technology.*
