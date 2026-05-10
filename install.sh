#!/usr/bin/env bash
# CryptONN Loader Installer v2.3
# Usage: bash install.sh [--dir /opt/cryptonn] [--php /usr/bin/php] [--vhost /home/user/public_html]
# Supports: cPanel (EasyApache 4), Plesk, DirectAdmin, bare Linux server
# Requirements: PHP 7.2+, curl, bash
# License key NOT required — the loader is free. Monetization is on the encoder side.

set -euo pipefail

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${CYAN}[CryptONN]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ── Defaults ──────────────────────────────────────────────────────────────────
LOADER_URL="https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/cryptonn-loader.php"
LOADER_VERSION="2.3"
INSTALL_DIR="/opt/cryptonn"
PHP_BIN=""
TARGET_VHOST=""

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir|-d)    INSTALL_DIR="$2"; shift 2 ;;
        --php|-p)    PHP_BIN="$2"; shift 2 ;;
        --vhost|-v)  TARGET_VHOST="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--dir /opt/cryptonn] [--php /usr/bin/php] [--vhost /home/user/public_html]"
            echo ""
            echo "No license key required. The loader is free."
            echo "Monetization is on the encoder side (CryptONN GUI)."
            exit 0
            ;;
        *) die "Unknown argument: $1" ;;
    esac
done

# ── Detect PHP binary ─────────────────────────────────────────────────────────
detect_php() {
    if [[ -n "$PHP_BIN" ]]; then
        [[ -x "$PHP_BIN" ]] || die "PHP binary not found: $PHP_BIN"
        return
    fi
    for candidate in php php8.5 php8.4 php8.3 php8.2 php8.1 php8.0 php7.4 php7.3 php7.2; do
        if command -v "$candidate" &>/dev/null; then
            PHP_BIN=$(command -v "$candidate")
            break
        fi
    done
    if [[ -z "$PHP_BIN" ]]; then die "PHP not found. Install PHP 7.2+ or use --php /path/to/php"; fi
}

# ── Detect control panel ──────────────────────────────────────────────────────
detect_panel() {
    PANEL="bare"
    if [[ -d "/usr/local/cpanel" ]]; then
        PANEL="cpanel"
    elif [[ -d "/usr/local/psa" ]] || command -v plesk &>/dev/null 2>&1; then
        PANEL="plesk"
    elif [[ -d "/usr/local/directadmin" ]]; then
        PANEL="directadmin"
    elif [[ -d "/usr/local/cyberpanel" ]]; then
        PANEL="cyberpanel"
    fi
    info "Control panel detected: ${PANEL}"
}

# ── Detect active PHP versions (cPanel EasyApache 4) ─────────────────────────
detect_cpanel_php_versions() {
    PHP_VERSIONS=()
    for ver_dir in /opt/cpanel/ea-php*/root/usr/bin/php; do
        if [[ -x "$ver_dir" ]]; then PHP_VERSIONS+=("$ver_dir"); fi
    done
    if [[ ${#PHP_VERSIONS[@]} -eq 0 ]]; then PHP_VERSIONS=("$PHP_BIN"); fi
}

# ── Check PHP requirements ─────────────────────────────────────────────────────
check_php() {
    local php="$1"
    local ver
    ver=$("$php" -r 'echo PHP_MAJOR_VERSION . "." . PHP_MINOR_VERSION;' 2>/dev/null) || die "PHP error"

    local major minor
    IFS='.' read -r major minor <<< "$ver"
    if (( major < 7 || (major == 7 && minor < 2) )); then
        die "PHP $ver is not supported. CryptONN Loader requires PHP 7.2+."
    fi

    local has_sodium
    has_sodium=$("$php" -r 'echo extension_loaded("sodium") ? "yes" : "no";' 2>/dev/null)
    if [[ "$has_sodium" != "yes" ]]; then
        warn "ext-sodium not found for PHP $ver. Attempting to install..."
        install_sodium "$php" "$ver"
    fi

    local has_openssl
    has_openssl=$("$php" -r 'echo extension_loaded("openssl") ? "yes" : "no";' 2>/dev/null)
    if [[ "$has_openssl" != "yes" ]]; then die "ext-openssl is required but not found for PHP $ver"; fi

    success "PHP $ver — sodium: ok, openssl: ok"
}

install_sodium() {
    local php="$1"
    local ver="$2"
    if command -v apt-get &>/dev/null; then
        apt-get install -y "php${ver}-sodium" &>/dev/null && return
    elif command -v yum &>/dev/null; then
        yum install -y "php-sodium" &>/dev/null && return
    fi
    die "Could not install ext-sodium automatically. Please install it manually for PHP $ver."
}

# ── Download loader ────────────────────────────────────────────────────────────
download_loader() {
    info "Downloading CryptONN Loader ${LOADER_VERSION}..."
    mkdir -p "$INSTALL_DIR"
    chmod 755 "$INSTALL_DIR"

    curl -fsSL "$LOADER_URL" -o "${INSTALL_DIR}/cryptonn-loader.php" \
        || die "Failed to download loader from ${LOADER_URL}"

    chmod 644 "${INSTALL_DIR}/cryptonn-loader.php"
    success "Loader downloaded to ${INSTALL_DIR}/cryptonn-loader.php"
}

# ── Configure auto_prepend_file ────────────────────────────────────────────────
configure_user_ini() {
    local ini_file="$1"
    local ini_dir
    ini_dir=$(dirname "$ini_file")
    mkdir -p "$ini_dir"

    if [[ -f "$ini_file" ]] && grep -q "auto_prepend_file" "$ini_file"; then
        sed -i "s|.*auto_prepend_file.*|auto_prepend_file = ${INSTALL_DIR}/cryptonn-loader.php|" "$ini_file"
    else
        echo "auto_prepend_file = ${INSTALL_DIR}/cryptonn-loader.php" >> "$ini_file"
    fi
    success "Configured: ${ini_file}"
}

configure_php_ini() {
    local ini_file="$1"
    if grep -q "auto_prepend_file" "$ini_file"; then
        sed -i "s|.*auto_prepend_file.*|auto_prepend_file = ${INSTALL_DIR}/cryptonn-loader.php|" "$ini_file"
    else
        echo "auto_prepend_file = ${INSTALL_DIR}/cryptonn-loader.php" >> "$ini_file"
    fi
    success "Configured: ${ini_file}"
}

configure_cpanel() {
    info "Configuring cPanel (EasyApache 4)..."
    local home_dir
    home_dir=$(eval echo "~${SUDO_USER:-$USER}")
    local user_ini="${home_dir}/.user.ini"
    if [[ -n "$TARGET_VHOST" ]]; then user_ini="${TARGET_VHOST}/.user.ini"; fi
    configure_user_ini "$user_ini"

    detect_cpanel_php_versions
    for php_ver in "${PHP_VERSIONS[@]}"; do
        check_php "$php_ver"
    done
}

configure_plesk() {
    info "Configuring Plesk (all PHP versions)..."
    local configured=0
    for php_ini in /opt/plesk/php/*/etc/php.ini; do
        [[ -f "$php_ini" ]] || continue
        configure_php_ini "$php_ini"
        configured=$((configured + 1))
    done
    if [[ $configured -eq 0 ]]; then warn "No Plesk PHP versions found under /opt/plesk/php/"; fi

    # Add /opt/cryptonn to open_basedir in every FPM pool config
    info "Updating open_basedir in Plesk FPM pool configs..."
    find /opt/plesk/php/*/etc/php-fpm.d/ -name "*.conf" 2>/dev/null | while read -r pool; do
        if grep -q 'php_value\[open_basedir\]' "$pool"; then
            sed -i 's|php_value\[open_basedir\]\s*=\s*"\(.*\)"|php_value[open_basedir] = "\1:'"${INSTALL_DIR}"'/"|g' "$pool"
        fi
    done
    success "open_basedir updated in all pool configs"

    # Restart active FPM services
    info "Restarting Plesk PHP-FPM services..."
    for svc in $(systemctl list-units --type=service --state=active --no-legend | awk '{print $1}' | grep 'plesk-php.*-fpm' || true); do
        systemctl restart "$svc" 2>/dev/null && success "Restarted ${svc}" || warn "Could not restart ${svc}"
    done
}

configure_directadmin() {
    info "Configuring DirectAdmin..."
    configure_user_ini "${TARGET_VHOST:-/home/*/domains/*/public_html}/.user.ini"
}

configure_bare() {
    info "Configuring bare server..."
    local php_ini
    php_ini=$("$PHP_BIN" -i 2>/dev/null | grep "Loaded Configuration File" | awk '{print $NF}')

    if [[ -n "$php_ini" && -w "$php_ini" ]]; then
        configure_php_ini "$php_ini"
    else
        local ini_dir="${TARGET_VHOST:-/var/www/html}"
        configure_user_ini "${ini_dir}/.user.ini"
        warn "Could not write to php.ini. Configured user.ini in ${ini_dir}."
        warn "If using Apache, add to .htaccess:"
        warn "  php_value auto_prepend_file ${INSTALL_DIR}/cryptonn-loader.php"
    fi
}

# ── Verify installation ────────────────────────────────────────────────────────
verify_install() {
    info "Verifying installation..."
    local result
    result=$("$PHP_BIN" -r "
        require '${INSTALL_DIR}/cryptonn-loader.php';
        echo function_exists('__cnn_load') ? 'ok' : 'fail';
    " 2>&1) || result="error"

    if [[ "$result" == "ok" ]]; then
        success "Loader loaded successfully."
    else
        warn "Loader verification: ${result}"
        warn "This may be normal if PHP CLI has different extensions."
    fi
}

# ── Print post-install instructions ───────────────────────────────────────────
print_instructions() {
    echo ""
    echo -e "${GREEN}══════════════════════════════════════════════════${NC}"
    echo -e "${GREEN} CryptONN Loader ${LOADER_VERSION} installed successfully!${NC}"
    echo -e "${GREEN}══════════════════════════════════════════════════${NC}"
    echo ""
    echo "  Loader: ${INSTALL_DIR}/cryptonn-loader.php"
    echo ""
    echo "  No license key required on this server."
    echo "  The loader identifies files by the encoder's license ID"
    echo "  embedded in each .cryptonn file header."
    echo ""
    echo "  Encoded files keep their .php extension — include normally:"
    echo "    include 'path/to/encoded_file.php';"
    echo ""
    echo "  If auto_prepend_file is not set automatically, add to php.ini:"
    echo "    auto_prepend_file = ${INSTALL_DIR}/cryptonn-loader.php"
    echo ""
    echo "  Or add to your entry point (index.php):"
    echo "    <?php require '/opt/cryptonn/cryptonn-loader.php';"
    echo ""
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    echo ""
    info "CryptONN Loader Installer v${LOADER_VERSION}"
    echo ""

    detect_php
    detect_panel
    check_php "$PHP_BIN"

    download_loader

    case "$PANEL" in
        cpanel)      configure_cpanel ;;
        plesk)       configure_plesk ;;
        directadmin) configure_directadmin ;;
        *)           configure_bare ;;
    esac

    verify_install
    print_instructions
}

main "$@"