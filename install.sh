#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  CryptONN Extension Installer v1.1.0
#  https://github.com/LAICOS-LTD/cryptonn-loader
#
#  Bu script CryptONN PHP extension (.so) dosyasını indirir, doğrular ve
#  kurulumunu gerçekleştirir.  Aşağıdaki ortamları destekler:
#
#    • Bare Linux server (Debian, Ubuntu, AlmaLinux, RHEL, CentOS)
#    • Plesk  (tüm kurulu PHP versiyonları otomatik bulunur)
#    • cPanel / EasyApache 4
#    • DirectAdmin
#
#  Kullanım:
#    bash install.sh [seçenekler]
#
#  Seçenekler:
#    --php /usr/bin/php   Belirli bir PHP binary'si kullan
#    --dir /opt/cryptonn  İndirme klasörü (varsayılan: /opt/cryptonn)
#    --help               Bu yardım metnini göster
#
#  Gereksinimler: bash 4+, curl, sha256sum
#
#  Lisans anahtarı GEREKMİYOR — extension tamamen ücretsizdir.
#  Monetizasyon encoder tarafındadır; sunucuya sadece bu extension kurulur.
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Terminal renkleri ─────────────────────────────────────────────────────────
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[1;33m'
CYN='\033[0;36m'
BLD='\033[1m'
NC='\033[0m'

banner() { echo -e "\n${BLD}${CYN}── $* ──────────────────────────────────────${NC}"; }
info()   { echo -e "  ${CYN}▸${NC} $*"; }
ok()     { echo -e "  ${GRN}✔${NC} $*"; }
warn()   { echo -e "  ${YLW}⚠${NC}  $*"; }
die()    { echo -e "\n  ${RED}✖  HATA:${NC} $*\n" >&2; exit 1; }
step()   { echo -e "  ${BLD}→${NC} $*"; }

# ── Varsayılanlar ─────────────────────────────────────────────────────────────
RELEASE_BASE="https://github.com/LAICOS-LTD/cryptonn-loader/releases/latest/download"
INSTALL_DIR="/opt/cryptonn"
FORCE_PHP=""
PANEL=""

# ── Argüman ayrıştırma ────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --php|-p)  FORCE_PHP="$2";   shift 2 ;;
        --dir|-d)  INSTALL_DIR="$2"; shift 2 ;;
        --help|-h)
            head -30 "$0" | grep -E "^#" | sed 's/^# \?//; s/^#//'
            exit 0 ;;
        *) die "Bilinmeyen argüman: $1  (--help ile kullanımı görün)" ;;
    esac
done

# ── Ön koşullar ───────────────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || die "Bu script root ya da sudo ile çalıştırılmalıdır."
command -v curl      &>/dev/null || die "curl kurulu değil. Kurun: apt/yum install curl"
command -v sha256sum &>/dev/null || die "sha256sum bulunamadı."
mkdir -p "$INSTALL_DIR" || die "Klasör oluşturulamadı: $INSTALL_DIR"

# ── CPU mimarisi ──────────────────────────────────────────────────────────────
detect_arch() {
    case "$(uname -m)" in
        x86_64)        echo "x86_64"  ;;
        aarch64|arm64) echo "aarch64" ;;
        *) die "Desteklenmeyen mimari: $(uname -m)  (x86_64 ve aarch64 desteklenir)" ;;
    esac
}
ARCH=$(detect_arch)

# ── PHP yardımcı fonksiyonları ────────────────────────────────────────────────
php_ver()    { "$1" -r 'echo PHP_MAJOR_VERSION.".".PHP_MINOR_VERSION;' 2>/dev/null; }
php_extdir() { "$1" -r 'echo ini_get("extension_dir");' 2>/dev/null; }
php_ini()    { "$1" --ini 2>/dev/null | awk -F': ' '/Loaded Configuration/{gsub(/ /,"",$2); print $2}'; }

php_ok() {
    local php="$1" ver major minor
    [[ -x "$php" ]] || return 1
    ver=$(php_ver "$php")
    IFS='.' read -r major minor <<< "$ver"
    (( major > 7 || (major == 7 && minor >= 2) ))
}

# ── Tek PHP versiyonu için indirme + kurulum ──────────────────────────────────
install_for() {
    local php="$1"

    [[ -x "$php" ]] || { warn "Yürütülebilir değil, atlanıyor: $php"; return 0; }

    if ! php_ok "$php"; then
        warn "PHP $(php_ver "$php") < 7.2 desteklenmiyor, atlanıyor."
        return 0
    fi

    local ver slug extdir ini so_name so_path chk_path target
    ver=$(php_ver "$php")
    slug="${ver/./}"
    extdir=$(php_extdir "$php")
    ini=$(php_ini "$php")
    so_name="cryptonn-php${slug}-${ARCH}.so"
    so_path="${INSTALL_DIR}/${so_name}"
    chk_path="${so_path}.sha256"
    target="${extdir}/cryptonn.so"

    echo ""
    step "PHP ${BLD}${ver}${NC} — ${php}"
    info "Extension dizini : ${extdir}"
    info "php.ini          : ${ini:-<bulunamadı>}"

    # Extension dizini kontrolü
    if [[ -z "$extdir" || ! -d "$extdir" ]]; then
        warn "Extension dizini bulunamadı (${extdir}), PHP $ver atlanıyor."
        return 0
    fi

    # ── .so indir ─────────────────────────────────────────────────────────────
    info "İndiriliyor: ${so_name} ..."
    if ! curl -fsSL --connect-timeout 20 --retry 3 \
            "${RELEASE_BASE}/${so_name}" -o "$so_path"; then
        warn "${so_name} indirilemedi — bu PHP versiyonu için build mevcut olmayabilir."
        warn "URL: ${RELEASE_BASE}/${so_name}"
        return 0
    fi
    ok "İndirildi: ${so_name} ($(du -sh "$so_path" | cut -f1))"

    # ── SHA-256 doğrulama ─────────────────────────────────────────────────────
    if curl -fsSL --connect-timeout 10 "${RELEASE_BASE}/${so_name}.sha256" \
            -o "$chk_path" 2>/dev/null; then
        local expected actual
        expected=$(cat "$chk_path" | tr -d '[:space:]')
        actual=$(sha256sum "$so_path" | awk '{print $1}')
        if [[ "$expected" == "$actual" ]]; then
            ok "SHA-256 doğrulandı"
        else
            rm -f "$so_path" "$chk_path"
            warn "SHA-256 uyuşmazlığı! ${so_name} silindi, PHP $ver atlanıyor."
            warn "  Beklenen : ${expected}"
            warn "  Gerçek   : ${actual}"
            return 0
        fi
        rm -f "$chk_path"
    else
        warn "Checksum dosyası indirilemedi — doğrulama atlandı."
    fi

    # ── Extension dizinine kopyala ────────────────────────────────────────────
    if cp "$so_path" "$target" && chmod 644 "$target"; then
        ok "Kuruldu: ${target}"
    else
        warn "Kopyalama başarısız: ${so_path} → ${target}"
        return 0
    fi

    # ── php.ini yapılandırması ────────────────────────────────────────────────
    if [[ -z "$ini" ]]; then
        warn "php.ini bulunamadı. Manuel ekleyin:"
        warn "  echo 'extension=cryptonn' >> /path/to/php.ini"
    elif [[ ! -w "$ini" ]]; then
        warn "php.ini yazılamıyor: ${ini}"
        warn "  Manuel ekleyin: echo 'extension=cryptonn' >> ${ini}"
    else
        if grep -qE "^;*\s*extension=cryptonn" "$ini" 2>/dev/null; then
            # Zaten var — yorum satırını kaldır, aktif et
            sed -i 's|^;*[[:space:]]*extension=cryptonn.*|extension=cryptonn|' "$ini"
            ok "php.ini güncellendi (zaten vardı, aktif edildi): ${ini}"
        else
            echo "extension=cryptonn" >> "$ini"
            ok "php.ini yapılandırıldı: ${ini}"
        fi
    fi
}

# ── Kontrol paneli tespiti ────────────────────────────────────────────────────
detect_panel() {
    if   [[ -d "/usr/local/cpanel" ]];                                    then echo "cpanel"
    elif [[ -d "/usr/local/psa" ]] || command -v plesk &>/dev/null 2>&1;  then echo "plesk"
    elif [[ -d "/usr/local/directadmin" ]];                               then echo "directadmin"
    else                                                                        echo "bare"
    fi
}

# ── cPanel kurulumu ───────────────────────────────────────────────────────────
install_cpanel() {
    banner "cPanel / EasyApache 4"
    info "EasyApache PHP versiyonları taranıyor..."
    local found=0
    for php in /opt/cpanel/ea-php*/root/usr/bin/php; do
        [[ -x "$php" ]] || continue
        install_for "$php"
        (( found++ )) || true
    done
    if (( found == 0 )); then
        warn "EasyApache PHP binary bulunamadı."
        [[ -n "$FORCE_PHP" ]] || die "PHP binary belirtin: --php /usr/bin/php"
        install_for "$FORCE_PHP"
    fi
}

# ── Plesk kurulumu ────────────────────────────────────────────────────────────
install_plesk() {
    banner "Plesk — Tüm PHP versiyonları"
    info "/opt/plesk/php/ dizini taranıyor..."
    local found=0
    for php in /opt/plesk/php/*/bin/php; do
        [[ -x "$php" ]] || continue
        install_for "$php"
        (( found++ )) || true
    done
    if (( found == 0 )); then
        warn "Plesk PHP binary bulunamadı."
        [[ -n "$FORCE_PHP" ]] || die "PHP binary belirtin: --php /opt/plesk/php/8.3/bin/php"
        install_for "$FORCE_PHP"
    fi

    # Plesk PHP-FPM servislerini yeniden başlat
    banner "Plesk PHP-FPM servisleri yeniden başlatılıyor"
    local restarted=0
    while IFS= read -r svc; do
        [[ -n "$svc" ]] || continue
        if systemctl restart "$svc" 2>/dev/null; then
            ok "Yeniden başlatıldı: ${svc}"
            (( restarted++ )) || true
        else
            warn "Yeniden başlatılamadı: ${svc}"
        fi
    done < <(systemctl list-units --type=service --state=active --no-legend 2>/dev/null \
             | awk '{print $1}' | grep -E 'plesk-php.*-fpm' || true)

    if (( restarted == 0 )); then
        warn "Aktif plesk-php*-fpm servisi bulunamadı."
        warn "FPM'i manuel yeniden başlatın:"
        warn "  systemctl restart plesk-php83-fpm"
    fi
}

# ── Bare / DirectAdmin kurulumu ───────────────────────────────────────────────
install_bare() {
    banner "PHP binary aranıyor"
    if [[ -z "$FORCE_PHP" ]]; then
        for candidate in php php8.5 php8.4 php8.3 php8.2 php8.1 php8.0 \
                         php7.4 php7.3 php7.2; do
            if command -v "$candidate" &>/dev/null; then
                FORCE_PHP=$(command -v "$candidate")
                info "Bulundu: ${FORCE_PHP}  (PHP $(php_ver "$FORCE_PHP"))"
                break
            fi
        done
    fi
    [[ -n "$FORCE_PHP" ]] \
        || die "PHP binary bulunamadı. Kurun (PHP 7.2+) ya da --php argümanı kullanın."
    install_for "$FORCE_PHP"
}

# ── Doğrulama ─────────────────────────────────────────────────────────────────
verify_all() {
    banner "Kurulum doğrulanıyor"
    local php_list=()
    case "$PANEL" in
        cpanel)          php_list=( /opt/cpanel/ea-php*/root/usr/bin/php ) ;;
        plesk)           php_list=( /opt/plesk/php/*/bin/php ) ;;
        directadmin|bare)
            [[ -n "$FORCE_PHP" ]] && php_list=( "$FORCE_PHP" ) || php_list=() ;;
    esac

    local pass=0 fail=0
    for php in "${php_list[@]}"; do
        [[ -x "$php" ]] || continue
        local ver result
        ver=$(php_ver "$php")
        result=$("$php" -r "echo extension_loaded('cryptonn') ? 'OK' : 'FAIL';" 2>/dev/null) \
            || result="ERROR"
        if [[ "$result" == "OK" ]]; then
            ok "PHP ${ver}: extension yüklendi"
            (( pass++ )) || true
        else
            warn "PHP ${ver}: ${result}"
            warn "  → Web sunucusunu (Apache/Nginx) veya PHP-FPM'i yeniden başlatın."
            (( fail++ )) || true
        fi
    done

    (( pass + fail == 0 )) && warn "Doğrulanacak PHP binary bulunamadı."
}

# ── Özet ekranı ───────────────────────────────────────────────────────────────
print_summary() {
    echo ""
    echo -e "${BLD}${GRN}╔══════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLD}${GRN}║   CryptONN Extension başarıyla kuruldu!              ║${NC}"
    echo -e "${BLD}${GRN}╚══════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "  ${BLD}Önemli notlar:${NC}"
    echo "  • Sunucuda lisans anahtarı gerekmez."
    echo "  • Extension, her encoded dosyanın başlığından license ID'yi"
    echo "    okur ve API'den şifre çözme anahtarını otomatik alır."
    echo "  • Encoded dosyalar .php uzantısıyla include edilir:"
    echo ""
    echo -e "      ${CYN}require 'path/to/encoded_file.php';${NC}"
    echo ""
    echo "  Kurulumu doğrulamak için:"
    echo -e "      ${CYN}php -r \"echo extension_loaded('cryptonn') ? 'OK' : 'Yüklü değil';\"${NC}"
    echo ""
    echo "  phpinfo() çıktısında 'CryptONN support => enabled' görünmelidir."
    echo ""
}

# ── Ana akış ──────────────────────────────────────────────────────────────────
main() {
    echo ""
    echo -e "${BLD}${CYN}CryptONN Extension Installer v1.1.0${NC}"
    echo -e "${CYN}══════════════════════════════════════${NC}"
    echo ""
    info "Mimari      : ${ARCH}"
    PANEL=$(detect_panel)
    info "Panel       : ${PANEL}"
    info "İndirme dir : ${INSTALL_DIR}"

    case "$PANEL" in
        cpanel)           install_cpanel ;;
        plesk)            install_plesk  ;;
        directadmin|bare) install_bare   ;;
    esac

    verify_all
    print_summary
}

main "$@"
