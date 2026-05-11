# CryptONN — Protección de Código PHP y Sistema de Licencias

**Idiomas:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **El CryptONN Loader es gratuito y no requiere ninguna clave de licencia.** La gestión de licencias ocurre en la etapa de codificación. Instálelo una vez por servidor y gestionará automáticamente todas las aplicaciones protegidas.

---

## ¿Qué es CryptONN?

CryptONN es una plataforma profesional de protección de código fuente PHP y gestión de licencias de software diseñada para proveedores independientes de software (ISV) y equipos de desarrollo que distribuyen aplicaciones PHP comercialmente. Transforma archivos PHP en un formato binario cifrado, fundamentalmente resistente a la ingeniería inversa, la descompilación y la redistribución no autorizada, manteniendo plena compatibilidad con la infraestructura PHP estándar y sin penalización en el rendimiento.

---

## Problemas que resuelve

| Problema | Solución de CryptONN |
|---|---|
| **Robo de código fuente** | La lógica PHP se transforma en una carga útil binaria cifrada. Incluso con acceso completo al sistema de archivos, el código original no puede reconstruirse. |
| **Despliegue no autorizado** | Cada archivo protegido contiene un identificador de licencia integrado, validado en el servidor. Los archivos copiados a servidores sin licencia se niegan a ejecutarse. |
| **Cumplimiento de términos de licencia** | Los períodos de prueba y las fechas de vencimiento se aplican en el servidor de licencias. No existen verificaciones del lado del cliente que puedan eludirse. |
| **Distribución multi-cliente** | La misma base de código puede licenciarse a múltiples clientes, cada uno con condiciones, límites de uso y restricciones de dominio únicos. |

---

## Compatibilidad con PHP

| Versión PHP | Estado |
|---|---|
| PHP 7.2 | ✅ Totalmente compatible |
| PHP 7.3 | ✅ Totalmente compatible |
| PHP 7.4 | ✅ Totalmente compatible |
| PHP 8.0 | ✅ Totalmente compatible |
| PHP 8.1 | ✅ Totalmente compatible |
| PHP 8.2 | ✅ Totalmente compatible |
| PHP 8.3 | ✅ Totalmente compatible |
| PHP 8.4 | ✅ Totalmente compatible |
| PHP 8.5 | ✅ Totalmente compatible |
| PHP 5.x · 7.0 · 7.1 | ❌ No compatible |

---

## Requisitos del sistema

| Componente | Requisito | Notas |
|---|---|---|
| PHP | 7.2 – 8.5 | Todas las versiones menores compatibles |
| ext-sodium | Cualquier versión | Incluido con PHP 7.2+ |
| ext-openssl | Cualquier versión | Disponible por defecto en prácticamente todos los entornos de hosting |
| HTTPS saliente | Puerto 443 | Requerido para las llamadas a la API de validación de licencias |
| APCu (opcional) | Cualquier versión | Habilita el caché en memoria |

---

## Instalación

### Paso 1 — Descargar el Loader

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
```

### Paso 2 — Configurar PHP

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

**Servidor dedicado — PHP-FPM**
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

### Paso 3 — Verificar la instalación

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Activo\n" : "❌ CryptONN Loader: No cargado\n";
echo "Versión PHP : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ FALTA") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ FALTA") . "\n";
```

---

## Resolución de problemas

### `CryptONN Loader requires ext-sodium`
```bash
dnf install php-sodium          # AlmaLinux / RHEL / CentOS
apt-get install php8.2-sodium   # Ubuntu / Debian
/scripts/install_ea_metapackage ea-php82-php-sodium  # cPanel
```

### `CryptONN Loader requires ext-openssl`
Active `extension=openssl` en `php.ini` o instale el paquete `php-openssl`.

### `Master key could not be retrieved`
El Loader no puede alcanzar la API de licencias.
```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```
Asegúrese de que el puerto TCP 443 saliente está permitido desde el servidor.

### `Invalid magic bytes`
El archivo no es un archivo CryptONN válido o se corrompió durante la transferencia. Retransfiera el archivo `.cryptonn` en modo binario.

### `Incomplete header`
El archivo `.cryptonn` está truncado. Retransfiera el archivo y verifique el espacio en disco disponible.

### `Decryption failed`
La clave devuelta por la API no coincide con los parámetros de cifrado del archivo. Contacte al proveedor del software.

### `Temporary file could not be written`
El proceso PHP no tiene permisos de escritura en el directorio temporal del sistema. Revise las políticas SELinux o AppArmor.

---

## Rendimiento

| Capa de caché | Latencia típica | Duración |
|---|---|---|
| En memoria (APCu) | < 0,1 ms | 1 hora |
| Caché de archivo | < 0,5 ms | 24 horas |
| Llamada API (frío) | 50 – 200 ms | En caso de cache miss |
| Período de gracia | < 0,5 ms | Hasta 72 horas (no prueba) |

---

## Preguntas frecuentes

**P: ¿Es gratuito el Loader?**  
R: Sí. El CryptONN Loader es gratuito y de código abierto, sin clave de licencia ni suscripción.

**P: ¿Funciona con PHP OPcache?**  
R: Sí. OPcache opera sobre el bytecode PHP después de que el Loader descifra y ejecuta el código.

**P: ¿Qué sucede durante una interrupción de la API?**  
R: Las licencias que no son de prueba continúan operando hasta 72 horas desde el caché de archivo. Las licencias de prueba requieren una respuesta exitosa de la API en cada cache miss.

---

## Desinstalación

```bash
# 1. Eliminar la directiva auto_prepend_file y reiniciar PHP
# 2. Eliminar el directorio del loader
rm -rf /opt/cryptonn
```

---

## Soporte

| Canal | Enlace |
|---|---|
| Documentación | [laicos.com.tr](https://laicos.com.tr) |
| Seguimiento de problemas | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN es un producto de LAICOS Technology.*
