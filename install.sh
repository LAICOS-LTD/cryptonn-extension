#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  CryptONN Extension Installer v1.2.0
#  https://github.com/LAICOS-LTD/cryptonn-loader
#
#  Downloads, verifies and installs the CryptONN PHP extension (.so).
#  Supported environments:
#    • Bare Linux server  (Debian, Ubuntu, AlmaLinux, RHEL, CentOS)
#    • Plesk              (all installed PHP versions detected automatically)
#    • cPanel / EasyApache 4
#    • DirectAdmin
#
#  Usage:
#    bash install.sh [options]
#
#  Options:
#    --php /usr/bin/php   Use a specific PHP binary
#    --dir /opt/cryptonn  Download directory  (default: /opt/cryptonn)
#    --help               Show this help
#
#  Requirements: bash 4+, curl, sha256sum
#
#  No license key required — the extension is free to install.
#  Monetisation is on the encoder side; the server only needs this extension.
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Colours & symbols ─────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    RED='\033[0;31m'  GRN='\033[0;32m'  YLW='\033[1;33m'
    CYN='\033[0;36m'  BLU='\033[0;34m'  MAG='\033[0;35m'
    WHT='\033[1;37m'  DIM='\033[2m'     BLD='\033[1m'
    NC='\033[0m'
else
    RED='' GRN='' YLW='' CYN='' BLU='' MAG='' WHT='' DIM='' BLD='' NC=''
fi

SYM_OK="✔"
SYM_WARN="⚠"
SYM_ERR="✖"
SYM_ARR="›"
SYM_DOT="•"

# ── Layout helpers ────────────────────────────────────────────────────────────
WIDTH=62

_line() { # char
    local c="${1:- }" out=""
    for (( i=0; i<WIDTH; i++ )); do out+="$c"; done
    echo "$out"
}

box_top()    { echo -e "${CYN}╔$(_line ═)╗${NC}"; }
box_bot()    { echo -e "${CYN}╚$(_line ═)╝${NC}"; }
box_sep()    { echo -e "${CYN}╠$(_line ═)╣${NC}"; }
box_empty()  { echo -e "${CYN}║${NC}$(_line ' ')${CYN}║${NC}"; }

box_row() {  # text  [color]
    local txt="$1" col="${2:-$NC}"
    local visible
    # strip ANSI for length calculation
    visible=$(echo -e "$txt" | sed 's/\x1b\[[0-9;]*m//g')
    local pad=$(( WIDTH - ${#visible} ))
    (( pad < 0 )) && pad=0
    printf "${CYN}║${NC}${col}%s${NC}%${pad}s${CYN}║${NC}\n" "$txt" ""
}

box_center() { # text [color]
    local txt="$1" col="${2:-$NC}"
    local visible
    visible=$(echo -e "$txt" | sed 's/\x1b\[[0-9;]*m//g')
    local pad_total=$(( WIDTH - ${#visible} ))
    local pad_l=$(( pad_total / 2 ))
    local pad_r=$(( pad_total - pad_l ))
    printf "${CYN}║${NC}%${pad_l}s${col}%s${NC}%${pad_r}s${CYN}║${NC}\n" "" "$txt" ""
}

section() { # title
    echo ""
    echo -e "${CYN}  ┌─ ${BLD}${WHT}$1${NC}${CYN} $(printf '─%.0s' $(seq 1 $(( WIDTH - ${#1} - 5 ))))${NC}"
}

ok()   { echo -e "  ${GRN}${SYM_OK}${NC}  $*"; }
warn() { echo -e "  ${YLW}${SYM_WARN}${NC}  $*"; }
die()  { echo -e "\n  ${RED}${SYM_ERR}  ERROR: $*${NC}\n" >&2; exit 1; }
info() { echo -e "  ${DIM}${SYM_ARR}${NC}  $*"; }
step() { echo -e "  ${BLU}${SYM_DOT}${NC}  ${BLD}$*${NC}"; }

# ── Header banner ─────────────────────────────────────────────────────────────
print_header() {
    echo ""
    box_top
    box_empty
    box_center "${BLD}${WHT}CryptONN Extension Installer${NC}"
    box_center "${DIM}v1.2.0  —  https://cryptonn.com${NC}"
    box_empty
    box_bot
    echo ""
}

# ── Defaults ──────────────────────────────────────────────────────────────────
RELEASE_BASE="https://github.com/LAICOS-LTD/cryptonn-loader/releases/latest/download"
INSTALL_DIR="/opt/cryptonn"
FORCE_PHP=""
PANEL=""
declare -a INSTALLED_VERSIONS=()
declare -a FAILED_VERSIONS=()

# ── Arguments ─────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --php|-p)  FORCE_PHP="$2";   shift 2 ;;
        --dir|-d)  INSTALL_DIR="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,26p' "$0" | sed 's/^# \?//; s/^#//'
            exit 0 ;;
        *) die "Unknown argument: $1  (use --help)" ;;
    esac
done

# ── Pre-flight checks ─────────────────────────────────────────────────────────
preflight() {
    section "Pre-flight checks"
    [[ $EUID -eq 0 ]] || die "Must be run as root (or with sudo)."
    ok "Running as root"

    command -v curl      &>/dev/null || die "curl is not installed.  Run: apt/yum install curl"
    ok "curl found:      $(curl --version | head -1 | awk '{print $2}')"

    command -v sha256sum &>/dev/null || die "sha256sum not found."
    ok "sha256sum found"

    mkdir -p "$INSTALL_DIR" || die "Cannot create directory: $INSTALL_DIR"
    ok "Download dir:    ${INSTALL_DIR}"

    ARCH=$(detect_arch)
    ok "Architecture:    ${ARCH}"

    PANEL=$(detect_panel)
    ok "Control panel:   ${PANEL}"
    echo ""
}

# ── Architecture ──────────────────────────────────────────────────────────────
detect_arch() {
    case "$(uname -m)" in
        x86_64)        echo "x86_64"  ;;
        aarch64|arm64) echo "aarch64" ;;
        *) die "Unsupported architecture: $(uname -m)  (x86_64 and aarch64 supported)" ;;
    esac
}

# ── PHP helpers ───────────────────────────────────────────────────────────────
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

# ── Download + install for one PHP binary ─────────────────────────────────────
install_for() {
    local php="$1"
    [[ -x "$php" ]] || { warn "Not executable, skipping: $php"; return 0; }

    if ! php_ok "$php"; then
        warn "PHP $(php_ver "$php") < 7.2 — not supported, skipping."
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
    echo -e "  ${CYN}┌─────────────────────────────────────────────────────${NC}"
    echo -e "  ${CYN}│${NC}  ${BLD}${WHT}PHP ${ver}${NC}  ${DIM}${php}${NC}"
    echo -e "  ${CYN}│${NC}  ${DIM}ext dir : ${extdir}${NC}"
    echo -e "  ${CYN}│${NC}  ${DIM}php.ini : ${ini:-<not found>}${NC}"
    echo -e "  ${CYN}└─────────────────────────────────────────────────────${NC}"

    # Extension dir check
    if [[ -z "$extdir" || ! -d "$extdir" ]]; then
        warn "Extension directory not found (${extdir}), skipping PHP $ver."
        FAILED_VERSIONS+=("$ver")
        return 0
    fi

    # ── Download ──────────────────────────────────────────────────────────────
    printf "     %-14s" "Downloading..."
    if curl -fsSL --connect-timeout 20 --retry 3 \
            "${RELEASE_BASE}/${so_name}" -o "$so_path" 2>/dev/null; then
        local size
        size=$(du -sh "$so_path" 2>/dev/null | cut -f1)
        echo -e " ${GRN}${SYM_OK}${NC} ${so_name}  ${DIM}(${size})${NC}"
    else
        echo -e " ${RED}${SYM_ERR}${NC} Download failed"
        warn "URL: ${RELEASE_BASE}/${so_name}"
        FAILED_VERSIONS+=("$ver")
        return 0
    fi

    # ── SHA-256 verify ────────────────────────────────────────────────────────
    printf "     %-14s" "Verifying..."
    if curl -fsSL --connect-timeout 10 "${RELEASE_BASE}/${so_name}.sha256" \
            -o "$chk_path" 2>/dev/null; then
        local expected actual
        expected=$(cat "$chk_path" | tr -d '[:space:]')
        actual=$(sha256sum "$so_path" | awk '{print $1}')
        if [[ "$expected" == "$actual" ]]; then
            echo -e " ${GRN}${SYM_OK}${NC} SHA-256 OK  ${DIM}${actual:0:16}…${NC}"
            rm -f "$chk_path"
        else
            echo -e " ${RED}${SYM_ERR}${NC} Checksum mismatch!"
            rm -f "$so_path" "$chk_path"
            warn "Expected : ${expected}"
            warn "Got      : ${actual}"
            FAILED_VERSIONS+=("$ver")
            return 0
        fi
    else
        echo -e " ${YLW}${SYM_WARN}${NC} Checksum unavailable — skipped"
    fi

    # ── Install .so ───────────────────────────────────────────────────────────
    printf "     %-14s" "Installing..."
    if cp "$so_path" "$target" && chmod 644 "$target"; then
        echo -e " ${GRN}${SYM_OK}${NC} ${target}"
    else
        echo -e " ${RED}${SYM_ERR}${NC} Copy failed: ${so_path} → ${target}"
        FAILED_VERSIONS+=("$ver")
        return 0
    fi

    # ── php.ini ───────────────────────────────────────────────────────────────
    printf "     %-14s" "php.ini..."
    if [[ -z "$ini" ]]; then
        echo -e " ${YLW}${SYM_WARN}${NC} Not found — add manually:"
        warn "  echo 'extension=cryptonn' >> /path/to/php.ini"
    elif [[ ! -w "$ini" ]]; then
        echo -e " ${YLW}${SYM_WARN}${NC} Not writable: ${ini}"
        warn "  echo 'extension=cryptonn' >> ${ini}"
    else
        if grep -qE "^;*[[:space:]]*extension=cryptonn" "$ini" 2>/dev/null; then
            sed -i 's|^;*[[:space:]]*extension=cryptonn.*|extension=cryptonn|' "$ini"
            echo -e " ${GRN}${SYM_OK}${NC} Enabled in existing ini"
        else
            echo "extension=cryptonn" >> "$ini"
            echo -e " ${GRN}${SYM_OK}${NC} Added to ${ini}"
        fi
    fi

    INSTALLED_VERSIONS+=("$ver")
}

# ── Panel detection ───────────────────────────────────────────────────────────
detect_panel() {
    if   [[ -d "/usr/local/cpanel" ]];                                   then echo "cpanel"
    elif [[ -d "/usr/local/psa" ]] || command -v plesk &>/dev/null 2>&1; then echo "plesk"
    elif [[ -d "/usr/local/directadmin" ]];                              then echo "directadmin"
    else                                                                       echo "bare"
    fi
}

# ── Panel-specific installs ───────────────────────────────────────────────────
install_cpanel() {
    section "cPanel / EasyApache 4 — scanning PHP versions"
    local found=0
    for php in /opt/cpanel/ea-php*/root/usr/bin/php; do
        [[ -x "$php" ]] || continue
        install_for "$php"; (( found++ )) || true
    done
    (( found )) || { warn "No EasyApache PHP binaries found."; install_for "$FORCE_PHP"; }
}

install_plesk() {
    section "Plesk — scanning all installed PHP versions"
    local found=0
    for php in /opt/plesk/php/*/bin/php; do
        [[ -x "$php" ]] || continue
        install_for "$php"; (( found++ )) || true
    done
    (( found )) || { warn "No Plesk PHP binaries found."; install_for "$FORCE_PHP"; }

    section "Restarting Plesk PHP-FPM services"
    local restarted=0
    while IFS= read -r svc; do
        [[ -n "$svc" ]] || continue
        printf "     %-46s" "$svc"
        if systemctl restart "$svc" 2>/dev/null; then
            echo -e " ${GRN}${SYM_OK}${NC}"
            (( restarted++ )) || true
        else
            echo -e " ${YLW}${SYM_WARN}${NC} failed"
        fi
    done < <(systemctl list-units --type=service --state=active --no-legend 2>/dev/null \
             | awk '{print $1}' | grep -E 'plesk-php.*-fpm' || true)

    (( restarted == 0 )) && warn "No active plesk-php*-fpm services found — restart PHP-FPM manually."
}

install_bare() {
    section "Detecting PHP binary"
    if [[ -z "$FORCE_PHP" ]]; then
        for c in php php8.5 php8.4 php8.3 php8.2 php8.1 php8.0 php7.4 php7.3 php7.2; do
            if command -v "$c" &>/dev/null; then
                FORCE_PHP=$(command -v "$c")
                info "Found: ${FORCE_PHP}  (PHP $(php_ver "$FORCE_PHP"))"
                break
            fi
        done
    fi
    [[ -n "$FORCE_PHP" ]] \
        || die "No PHP binary found. Install PHP 7.2+ or use --php /path/to/php"
    install_for "$FORCE_PHP"
}

# ── Verification ──────────────────────────────────────────────────────────────
verify_all() {
    section "Verification"
    local php_list=()
    case "$PANEL" in
        cpanel)           php_list=( /opt/cpanel/ea-php*/root/usr/bin/php ) ;;
        plesk)            php_list=( /opt/plesk/php/*/bin/php ) ;;
        directadmin|bare) [[ -n "$FORCE_PHP" ]] && php_list=( "$FORCE_PHP" ) || php_list=() ;;
    esac

    echo ""
    echo -e "  ${CYN}┌──────────────┬───────────────────────────────────────┐${NC}"
    echo -e "  ${CYN}│${NC}  ${BLD}PHP Version ${NC}  ${CYN}│${NC}  ${BLD}Status${NC}                                  ${CYN}│${NC}"
    echo -e "  ${CYN}├──────────────┼───────────────────────────────────────┤${NC}"

    for php in "${php_list[@]}"; do
        [[ -x "$php" ]] || continue
        local ver result
        ver=$(php_ver "$php")
        result=$("$php" -r "echo extension_loaded('cryptonn') ? 'LOADED' : 'FAIL';" 2>/dev/null) \
            || result="ERROR"
        if [[ "$result" == "LOADED" ]]; then
            printf "  ${CYN}│${NC}  %-12s  ${CYN}│${NC}  ${GRN}${SYM_OK} Loaded successfully${NC}%-21s${CYN}│${NC}\n" "PHP $ver" ""
        else
            printf "  ${CYN}│${NC}  %-12s  ${CYN}│${NC}  ${YLW}${SYM_WARN} %-37s${NC}${CYN}│${NC}\n" "PHP $ver" "Restart web server / FPM"
        fi
    done
    echo -e "  ${CYN}└──────────────┴───────────────────────────────────────┘${NC}"
    echo ""
}

# ── Final summary ─────────────────────────────────────────────────────────────
print_summary() {
    local n_ok=${#INSTALLED_VERSIONS[@]}
    local n_fail=${#FAILED_VERSIONS[@]}

    echo ""
    box_top
    box_empty

    if (( n_ok > 0 )); then
        box_center "${GRN}${BLD}Installation Complete!${NC}"
    else
        box_center "${YLW}${BLD}Installation finished with warnings${NC}"
    fi

    box_empty
    box_sep

    if (( n_ok > 0 )); then
        box_row "  ${GRN}${SYM_OK}${NC}  Installed for ${BLD}${n_ok}${NC} PHP version(s):"
        for v in "${INSTALLED_VERSIONS[@]}"; do
            box_row "       ${DIM}${SYM_DOT}${NC}  PHP ${v}"
        done
    fi

    if (( n_fail > 0 )); then
        box_empty
        box_row "  ${YLW}${SYM_WARN}${NC}  Skipped ${BLD}${n_fail}${NC} PHP version(s):"
        for v in "${FAILED_VERSIONS[@]}"; do
            box_row "       ${DIM}${SYM_DOT}${NC}  PHP ${v}  (see warnings above)"
        done
    fi

    box_sep
    box_empty
    box_row "  ${DIM}No license key is required on this server.${NC}"
    box_row "  ${DIM}The extension reads the license ID from each${NC}"
    box_row "  ${DIM}encoded file and fetches the key from the API.${NC}"
    box_empty
    box_row "  Encoded files keep their ${BLD}.php${NC} extension:"
    box_row ""
    box_row "    ${CYN}require 'path/to/encoded_file.php';${NC}"
    box_empty
    box_row "  Verify at any time:"
    box_row "    ${CYN}php -r \"echo extension_loaded('cryptonn');\"${NC}"
    box_empty
    box_bot
    echo ""
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    print_header
    preflight
    case "$PANEL" in
        cpanel)           install_cpanel ;;
        plesk)            install_plesk  ;;
        directadmin|bare) install_bare   ;;
    esac
    verify_all
    print_summary
}

main "$@"
