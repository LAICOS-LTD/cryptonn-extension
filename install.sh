#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  CryptONN Extension Installer v1.3.0
#  https://github.com/LAICOS-LTD/cryptonn-loader
#
#  Installs, updates, uninstalls and checks the CryptONN PHP extension (.so).
#  Supported environments:
#    Bare Linux  (Debian, Ubuntu, AlmaLinux, RHEL, CentOS)
#    Plesk       (all installed PHP versions detected automatically)
#    cPanel / EasyApache 4
#    DirectAdmin
#
#  Usage:  bash install.sh [command] [options]
#
#  Commands:
#    (none)       Install or update to the latest version  [default]
#    --install    Force reinstall even if already up to date
#    --update     Check for a newer version and upgrade if available
#    --uninstall  Remove the extension from all PHP versions
#    --status     Show installation status for each PHP version
#    --help       Show this help
#
#  Options:
#    --php /path  Force a specific PHP binary
#    --dir /path  Download directory  (default: /opt/cryptonn)
#    --yes        Non-interactive — answer yes to all prompts
#
#  Requirements: bash 4+, curl, sha256sum
#  No license key required — monetisation is on the encoder side.
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ── Colours ($'...' so ESC is a real byte, not a literal backslash) ───────────
if [[ -t 1 ]]; then
    R=$'\033[0;31m'  G=$'\033[0;32m'  Y=$'\033[1;33m'
    C=$'\033[0;36m'  B=$'\033[0;34m'  W=$'\033[1;37m'
    D=$'\033[2m'     L=$'\033[1m'     NC=$'\033[0m'
else
    R='' G='' Y='' C='' B='' W='' D='' L='' NC=''
fi

OK="${G}✔${NC}"
WARN="${Y}⚠${NC}"
ERR="${R}✖${NC}"
INF="${C}i${NC}"

# ── Box / layout helpers ──────────────────────────────────────────────────────
BOX_W=60      # inner width (between the ║ borders)

# strip ANSI codes to measure visible length
_vlen() { printf '%s' "$1" | sed $'s/\033\\[[0-9;]*m//g' | wc -m | tr -d ' '; }

_hline() { # char  count
    local s='' i; for (( i=0; i<$2; i++ )); do s+="$1"; done; printf '%s' "$s"
}

_box_top() { echo -e "${C}╔$(_hline ═ $BOX_W)╗${NC}"; }
_box_bot() { echo -e "${C}╚$(_hline ═ $BOX_W)╝${NC}"; }
_box_sep() { echo -e "${C}╠$(_hline ═ $BOX_W)╣${NC}"; }
_box_empty(){ echo -e "${C}║$(_hline ' ' $BOX_W)║${NC}"; }

# one row — left-aligned, padded to BOX_W
_box_row() {
    local text="${1:-}" vl pad
    vl=$(_vlen "$text")
    pad=$(( BOX_W - vl ))
    (( pad < 0 )) && pad=0
    echo -e "${C}║${NC}${text}$(printf '%*s' $pad '')${C}║${NC}"
}

# one row — centred
_box_mid() {
    local text="${1:-}" vl pad pl pr
    vl=$(_vlen "$text")
    pad=$(( BOX_W - vl ))
    pl=$(( pad / 2 )); pr=$(( pad - pl ))
    echo -e "${C}║${NC}$(printf '%*s' $pl '')${text}$(printf '%*s' $pr '')${C}║${NC}"
}

# section divider
sec() { echo -e "\n  ${C}┌─ ${L}${W}$1${NC} ${C}$(_hline ─ $(( BOX_W - ${#1} - 2 )))${NC}"; }

ok()   { echo -e "     ${OK}  $*"; }
warn() { echo -e "     ${WARN}  ${Y}$*${NC}"; }
err()  { echo -e "     ${ERR}  ${R}$*${NC}"; }
die()  { echo -e "\n  ${ERR}  ${R}ERROR: $*${NC}\n" >&2; exit 1; }
info() { echo -e "     ${INF}  ${D}$*${NC}"; }

# ── Globals ───────────────────────────────────────────────────────────────────
RELEASE_BASE="https://github.com/LAICOS-LTD/cryptonn-loader/releases/latest/download"
RELEASE_API="https://api.github.com/repos/LAICOS-LTD/cryptonn-loader/releases/latest"
INSTALL_DIR="/opt/cryptonn"
VERSION_FILE=""
FORCE_PHP=""
PANEL=""
AUTO_YES=0
CMD="auto"

declare -a V_FRESH=()
declare -a V_UPDATED=()
declare -a V_FAILED=()
declare -a V_SKIPPED=()
declare -a V_REMOVED=()

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --install|-i)  CMD="install";   shift ;;
        --update)      CMD="update";    shift ;;
        --uninstall)   CMD="uninstall"; shift ;;
        --status|-s)   CMD="status";    shift ;;
        --php|-p)      FORCE_PHP="$2";  shift 2 ;;
        --dir|-d)      INSTALL_DIR="$2";shift 2 ;;
        --yes|-y)      AUTO_YES=1;      shift ;;
        --help|-h)
            sed -n '2,30p' "$0" | sed 's/^# \?//; s/^#//'
            exit 0 ;;
        *) die "Unknown argument: $1  (use --help)" ;;
    esac
done
VERSION_FILE="${INSTALL_DIR}/.version"

# ── Header ────────────────────────────────────────────────────────────────────
print_header() {
    local title="CryptONN Extension Installer"
    local sub="v1.3.0  ·  LAICOS-LTD/cryptonn-loader"
    echo ""
    _box_top
    _box_empty
    _box_mid "${L}${W}${title}${NC}"
    _box_mid "${D}${sub}${NC}"
    _box_empty
    _box_bot
    echo ""
}

# ── PHP helpers ───────────────────────────────────────────────────────────────
php_ver()    { "$1" -r 'echo PHP_MAJOR_VERSION.".".PHP_MINOR_VERSION;' 2>/dev/null; }
php_extdir() { "$1" -r 'echo ini_get("extension_dir");' 2>/dev/null; }
php_ini()    { "$1" --ini 2>/dev/null | awk -F': ' '/Loaded Configuration/{gsub(/ /,"",$2);print $2}'; }
php_ok() {
    local v major minor
    [[ -x "$1" ]] || return 1
    v=$(php_ver "$1"); IFS='.' read -r major minor <<< "$v"
    (( major > 7 || (major == 7 && minor >= 2) ))
}
ext_installed() { local d; d=$(php_extdir "$1"); [[ -f "${d}/cryptonn.so" ]]; }
ext_ver()       { "$1" -r "echo extension_loaded('cryptonn') ? phpversion('cryptonn') : '';" 2>/dev/null || true; }

# ── Version helpers ───────────────────────────────────────────────────────────
ver_read()   { [[ -f "$VERSION_FILE" ]] && cat "$VERSION_FILE" || echo ""; }
ver_write()  { echo "$1" > "$VERSION_FILE"; }
ver_delete() { rm -f "$VERSION_FILE"; }

get_latest() {
    curl -fsSL --connect-timeout 10 "$RELEASE_API" 2>/dev/null \
        | grep '"tag_name"' | head -1 \
        | sed 's/.*"v*\([0-9][^"]*\)".*/\1/'
}

ver_gt() {   # $1 > $2 ?
    local IFS=.
    read -ra A <<< "$1"; read -ra B <<< "$2"
    local i
    for i in 0 1 2; do
        local a=${A[$i]:-0} b=${B[$i]:-0}
        (( a > b )) && return 0
        (( a < b )) && return 1
    done
    return 1
}

# ── Architecture & panel ──────────────────────────────────────────────────────
ARCH=""
detect_arch() {
    case "$(uname -m)" in
        x86_64)        echo "x86_64"  ;;
        aarch64|arm64) echo "aarch64" ;;
        *) die "Unsupported architecture: $(uname -m)" ;;
    esac
}
detect_panel() {
    if   [[ -d "/usr/local/cpanel" ]];                                   then echo "cpanel"
    elif [[ -d "/usr/local/psa" ]] || command -v plesk &>/dev/null 2>&1; then echo "plesk"
    elif [[ -d "/usr/local/directadmin" ]];                              then echo "directadmin"
    else                                                                       echo "bare"
    fi
}

# ── Pre-flight ────────────────────────────────────────────────────────────────
preflight() {
    sec "Pre-flight checks"
    [[ $EUID -eq 0 ]] || die "Must be run as root (or with sudo)."
    ok "Running as root"
    command -v curl      &>/dev/null || die "curl not found.  Install: apt/yum install curl"
    ok "curl        $(curl --version 2>/dev/null | head -1 | awk '{print $2}')"
    command -v sha256sum &>/dev/null || die "sha256sum not found."
    ok "sha256sum   present"
    ARCH=$(detect_arch);  ok "Architecture ${ARCH}"
    PANEL=$(detect_panel);ok "Panel        ${PANEL}"
    if [[ "$CMD" != "status" ]]; then
        mkdir -p "$INSTALL_DIR" || die "Cannot create: $INSTALL_DIR"
        ok "Download dir ${INSTALL_DIR}"
    fi
}

# ── PHP iterator ──────────────────────────────────────────────────────────────
each_php() {
    case "$PANEL" in
        plesk)
            for p in /opt/plesk/php/*/bin/php; do [[ -x "$p" ]] && echo "$p"; done ;;
        cpanel)
            for p in /opt/cpanel/ea-php*/root/usr/bin/php; do [[ -x "$p" ]] && echo "$p"; done ;;
        *)
            if [[ -n "$FORCE_PHP" ]]; then
                echo "$FORCE_PHP"
            else
                for c in php php8.5 php8.4 php8.3 php8.2 php8.1 php8.0 php7.4 php7.3 php7.2; do
                    command -v "$c" &>/dev/null && command -v "$c" && break
                done
            fi ;;
    esac
}

# ══════════════════════════════════════════════════════════════════════════════
#  INSTALL (one PHP)
# ══════════════════════════════════════════════════════════════════════════════
_pline() { # label  result_string
    printf "     %-16s %s\n" "$1" "$2"
}

install_for() {
    local php="$1" force="${2:-0}"
    [[ -x "$php" ]] || return 0
    php_ok "$php"   || { warn "PHP $(php_ver "$php") < 7.2 — skipping."; return 0; }

    local ver slug extdir ini so so_path target
    ver=$(php_ver "$php"); slug="${ver/./}"
    extdir=$(php_extdir "$php"); ini=$(php_ini "$php")
    so="cryptonn-php${slug}-${ARCH}.so"
    so_path="${INSTALL_DIR}/${so}"
    target="${extdir}/cryptonn.so"

    echo ""
    echo -e "  ${C}┌─ ${L}${W}PHP ${ver}${NC}"
    echo -e "  ${C}│${NC}  ${D}${php}${NC}"
    echo -e "  ${C}│${NC}  ${D}ext › ${extdir}${NC}"
    echo -e "  ${C}│${NC}  ${D}ini › ${ini:-not found}${NC}"
    echo -e "  ${C}├─────────────────────────────────────────────────────${NC}"

    [[ -d "$extdir" ]] || {
        echo -e "  ${C}│${NC}  ${ERR}  Extension dir not found — skipping."
        echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
        V_FAILED+=("$ver"); return 0
    }

    # Already up to date?
    if (( force == 0 )) && ext_installed "$php"; then
        local cur; cur=$(ext_ver "$php")
        local lat; lat=$(ver_read)
        if [[ -n "$cur" && -n "$lat" && "$cur" == "$lat" ]]; then
            echo -e "  ${C}│${NC}  ${INF}  Already at ${G}v${cur}${NC} — up to date"
            echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
            V_SKIPPED+=("$ver@${cur}"); return 0
        fi
    fi

    # Download
    printf "  ${C}│${NC}  %-17s" "Downloading..."
    if curl -fsSL --connect-timeout 20 --retry 3 \
            "${RELEASE_BASE}/${so}" -o "$so_path" 2>/dev/null; then
        local sz; sz=$(du -sh "$so_path" 2>/dev/null | cut -f1)
        echo -e "  ${OK}  ${D}${so} (${sz})${NC}"
    else
        echo -e "  ${ERR}  Failed"
        echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
        V_FAILED+=("$ver"); return 0
    fi

    # SHA-256
    printf "  ${C}│${NC}  %-17s" "SHA-256..."
    local chk="${so_path}.sha256"
    if curl -fsSL --connect-timeout 10 "${RELEASE_BASE}/${so}.sha256" \
            -o "$chk" 2>/dev/null; then
        local exp act
        exp=$(tr -d '[:space:]' < "$chk")
        act=$(sha256sum "$so_path" | awk '{print $1}')
        if [[ "$exp" == "$act" ]]; then
            echo -e "  ${OK}  ${D}${act:0:24}…${NC}"
            rm -f "$chk"
        else
            echo -e "  ${ERR}  Mismatch — aborting PHP ${ver}"
            echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
            rm -f "$so_path" "$chk"; V_FAILED+=("$ver"); return 0
        fi
    else
        echo -e "  ${WARN}  Unavailable — skipped"
        rm -f "$chk"
    fi

    # Copy
    local was=0; [[ -f "$target" ]] && was=1
    printf "  ${C}│${NC}  %-17s" "Installing..."
    if cp "$so_path" "$target" && chmod 644 "$target"; then
        echo -e "  ${OK}  ${D}${target}${NC}"
    else
        echo -e "  ${ERR}  Copy failed"
        echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
        V_FAILED+=("$ver"); return 0
    fi

    # php.ini
    printf "  ${C}│${NC}  %-17s" "php.ini..."
    if [[ -z "$ini" ]]; then
        echo -e "  ${WARN}  Not found — add manually: extension=cryptonn"
    elif [[ ! -w "$ini" ]]; then
        echo -e "  ${WARN}  Not writable: ${ini}"
    elif grep -qE "^;*[[:space:]]*extension=cryptonn" "$ini" 2>/dev/null; then
        sed -i 's|^;*[[:space:]]*extension=cryptonn.*|extension=cryptonn|' "$ini"
        echo -e "  ${OK}  Enabled in ${D}${ini}${NC}"
    else
        echo "extension=cryptonn" >> "$ini"
        echo -e "  ${OK}  Added to ${D}${ini}${NC}"
    fi

    echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
    (( was )) && V_UPDATED+=("$ver") || V_FRESH+=("$ver")
}

# ── Restart Plesk FPM ─────────────────────────────────────────────────────────
restart_plesk_fpm() {
    sec "Restarting Plesk PHP-FPM services"
    local n=0
    while IFS= read -r svc; do
        [[ -n "$svc" ]] || continue
        printf "     %-50s" "${svc}"
        if systemctl restart "$svc" 2>/dev/null; then
            echo -e " ${OK}"; (( n++ )) || true
        else
            echo -e " ${WARN} failed"
        fi
    done < <(systemctl list-units --type=service --state=active --no-legend 2>/dev/null \
             | awk '{print $1}' | grep -E 'plesk-php.*-fpm' || true)
    (( n == 0 )) && warn "No active plesk-php*-fpm found — restart FPM manually."
}

# ══════════════════════════════════════════════════════════════════════════════
#  UNINSTALL
# ══════════════════════════════════════════════════════════════════════════════
uninstall_for() {
    local php="$1"
    [[ -x "$php" ]] || return 0
    php_ok "$php"   || return 0

    local ver extdir ini target
    ver=$(php_ver "$php"); extdir=$(php_extdir "$php")
    ini=$(php_ini "$php"); target="${extdir}/cryptonn.so"

    echo ""
    echo -e "  ${C}┌─ ${L}${W}PHP ${ver}${NC}"
    echo -e "  ${C}├─────────────────────────────────────────────────────${NC}"

    if [[ ! -f "$target" ]]; then
        echo -e "  ${C}│${NC}  ${INF}  Not installed — skipping"
        echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
        return 0
    fi

    printf "  ${C}│${NC}  %-17s" "Removing .so..."
    if rm -f "$target"; then
        echo -e "  ${OK}  ${D}${target}${NC}"
    else
        echo -e "  ${ERR}  Failed to remove ${target}"
        echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
        return 0
    fi

    printf "  ${C}│${NC}  %-17s" "php.ini..."
    if [[ -n "$ini" && -w "$ini" ]]; then
        sed -i '/^;*[[:space:]]*extension=cryptonn/d' "$ini"
        echo -e "  ${OK}  Line removed from ${D}${ini}${NC}"
    else
        echo -e "  ${WARN}  Remove manually: extension=cryptonn from ${ini:-php.ini}"
    fi

    echo -e "  ${C}└─────────────────────────────────────────────────────${NC}"
    V_REMOVED+=("$ver")
}

cmd_uninstall() {
    sec "Uninstall CryptONN extension"
    echo ""
    if (( AUTO_YES == 0 )); then
        echo -e "  ${Y}${L}This will remove cryptonn.so from ALL PHP versions.${NC}"
        echo -ne "  ${W}Continue? [y/N] ${NC}"
        read -r ans
        [[ "$ans" =~ ^[Yy]$ ]] || { echo -e "  ${D}Aborted.${NC}"; exit 0; }
    fi

    local n=0
    while IFS= read -r php; do uninstall_for "$php"; (( n++ )) || true; done < <(each_php)
    (( n == 0 )) && warn "No PHP binaries found."

    ver_delete
    case "$PANEL" in plesk) restart_plesk_fpm ;; esac

    echo ""
    _box_top; _box_empty
    if (( ${#V_REMOVED[@]} > 0 )); then
        _box_mid "${G}${L}Uninstall complete!${NC}"
        _box_empty; _box_sep; _box_empty
        _box_row "  ${G}${OK}${NC}  Removed from ${L}${#V_REMOVED[@]}${NC} PHP version(s):"
        for v in "${V_REMOVED[@]}"; do _box_row "       ${D}•  PHP ${v}${NC}"; done
    else
        _box_mid "${Y}${L}Nothing was removed${NC}"
    fi
    _box_empty; _box_bot; echo ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  STATUS
# ══════════════════════════════════════════════════════════════════════════════
cmd_status() {
    sec "Installation status"
    echo ""
    printf "     %-20s" "Checking GitHub..."
    local lat; lat=$(get_latest 2>/dev/null) || lat=""
    if [[ -n "$lat" ]]; then
        echo -e " ${OK}  Latest: ${G}${L}v${lat}${NC}"
    else
        echo -e " ${WARN}  Could not reach GitHub"
    fi
    echo ""

    echo -e "  ${C}┌──────────────┬───────────────┬─────────────────────┐${NC}"
    echo -e "  ${C}│${NC}  ${L}PHP Version${NC}  ${C}│${NC}  ${L}Installed${NC}      ${C}│${NC}  ${L}Status${NC}              ${C}│${NC}"
    echo -e "  ${C}├──────────────┼───────────────┼─────────────────────┤${NC}"

    local any=0
    while IFS= read -r php; do
        [[ -x "$php" ]] || continue; php_ok "$php" || continue
        local ver cur_ver status_str status_col
        ver=$(php_ver "$php")
        if ext_installed "$php"; then
            cur_ver=$(ext_ver "$php"); [[ -z "$cur_ver" ]] && cur_ver="?"
            if [[ -n "$lat" ]] && ver_gt "$lat" "$cur_ver"; then
                status_str="Update → v${lat}"
                status_col="${Y}"
            else
                status_str="Up to date"
                status_col="${G}"
            fi
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${G}v%-13s${NC}${C}│${NC}  ${status_col}%-21s${NC}${C}│${NC}\n" \
                "PHP $ver" "$cur_ver" "$status_str"
        else
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${D}%-15s${NC}${C}│${NC}  ${D}%-21s${NC}${C}│${NC}\n" \
                "PHP $ver" "—" "Not installed"
        fi
        (( any++ )) || true
    done < <(each_php)

    echo -e "  ${C}└──────────────┴───────────────┴─────────────────────┘${NC}"
    (( any == 0 )) && warn "No PHP binaries found."
    echo ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  INSTALL / UPDATE — orchestration
# ══════════════════════════════════════════════════════════════════════════════
cmd_install() {
    local force="${1:-0}"

    sec "Fetching latest release"
    printf "     %-20s" "GitHub API..."
    local lat; lat=$(get_latest 2>/dev/null) || lat=""
    if [[ -n "$lat" ]]; then
        echo -e " ${OK}  ${G}${L}v${lat}${NC}"
    else
        echo -e " ${WARN}  Unreachable — will install cached download"
    fi

    local cur; cur=$(ver_read)
    if [[ -n "$cur" && -n "$lat" && "$cur" == "$lat" && "$force" == "0" ]]; then
        echo ""
        ok "Already on ${G}v${cur}${NC} — nothing to do."
        info "Use --install to force reinstall, or --status to check versions."
        echo ""; return 0
    fi
    if [[ -n "$cur" && -n "$lat" ]] && ver_gt "$lat" "$cur"; then
        echo -e "     ${C}↑${NC}  Upgrading ${D}v${cur}${NC} → ${G}${L}v${lat}${NC}"
    fi

    sec "Installing extension"
    local n=0
    while IFS= read -r php; do
        install_for "$php" "$force"; (( n++ )) || true
    done < <(each_php)
    (( n == 0 )) && die "No PHP binaries found. Use --php /path/to/php"

    [[ -n "$lat" ]] && ver_write "$lat"
    case "$PANEL" in plesk) restart_plesk_fpm ;; esac
}

# ── Verification table ────────────────────────────────────────────────────────
verify_all() {
    sec "Verification"
    echo ""
    echo -e "  ${C}┌──────────────┬───────────────┬─────────────────────┐${NC}"
    echo -e "  ${C}│${NC}  ${L}PHP Version${NC}  ${C}│${NC}  ${L}Version${NC}        ${C}│${NC}  ${L}Status${NC}              ${C}│${NC}"
    echo -e "  ${C}├──────────────┼───────────────┼─────────────────────┤${NC}"
    while IFS= read -r php; do
        [[ -x "$php" ]] || continue; php_ok "$php" || continue
        local ver v
        ver=$(php_ver "$php")
        v=$(ext_ver "$php")
        if [[ -n "$v" ]]; then
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${G}v%-13s${NC}${C}│${NC}  ${G}%-21s${NC}${C}│${NC}\n" \
                "PHP $ver" "$v" "Loaded  ✔"
        else
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${D}%-15s${NC}${C}│${NC}  ${Y}%-21s${NC}${C}│${NC}\n" \
                "PHP $ver" "—" "Restart FPM / Apache"
        fi
    done < <(each_php)
    echo -e "  ${C}└──────────────┴───────────────┴─────────────────────┘${NC}"
    echo ""
}

# ── Final summary box ─────────────────────────────────────────────────────────
print_summary() {
    local n_ok=$(( ${#V_FRESH[@]} + ${#V_UPDATED[@]} ))
    local n_skip=${#V_SKIPPED[@]}
    local n_fail=${#V_FAILED[@]}
    echo ""
    _box_top; _box_empty

    if   (( n_ok   > 0 )); then _box_mid "${G}${L}Installation complete!${NC}"
    elif (( n_skip > 0 )); then _box_mid "${C}${L}Already up to date${NC}"
    else                        _box_mid "${Y}${L}Completed with warnings${NC}"
    fi

    _box_empty; _box_sep; _box_empty

    if (( ${#V_FRESH[@]} > 0 )); then
        _box_row "  ${G}${OK}${NC}  Installed on ${L}${#V_FRESH[@]}${NC} PHP version(s):"
        for v in "${V_FRESH[@]}";   do _box_row "       ${D}•  PHP ${v}${NC}"; done
        _box_empty
    fi
    if (( ${#V_UPDATED[@]} > 0 )); then
        _box_row "  ${C}↑${NC}   Updated on ${L}${#V_UPDATED[@]}${NC} PHP version(s):"
        for v in "${V_UPDATED[@]}"; do _box_row "       ${D}•  PHP ${v}${NC}"; done
        _box_empty
    fi
    if (( n_skip > 0 )); then
        _box_row "  ${INF}  Already up to date on ${L}${n_skip}${NC} version(s):"
        for v in "${V_SKIPPED[@]}"; do _box_row "       ${D}•  PHP ${v}${NC}"; done
        _box_empty
    fi
    if (( n_fail > 0 )); then
        _box_row "  ${WARN}  Failed on ${L}${n_fail}${NC} version(s)  (see above):"
        for v in "${V_FAILED[@]}";  do _box_row "       ${D}•  PHP ${v}${NC}"; done
        _box_empty
    fi

    _box_sep; _box_empty
    _box_row "  ${D}No license key required on this server.${NC}"
    _box_row "  ${D}License ID is read from each encoded file header.${NC}"
    _box_empty
    _box_row "  Include encoded files normally:"
    _box_row "    ${C}require 'path/to/encoded_file.php';${NC}"
    _box_empty
    _box_row "  Check for updates:  ${C}bash install.sh --status${NC}"
    _box_empty
    _box_bot; echo ""
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    print_header
    preflight
    case "$CMD" in
        status)    cmd_status ;;
        uninstall) cmd_uninstall ;;
        install)   cmd_install 1; verify_all; print_summary ;;
        update)    cmd_install 0; verify_all; print_summary ;;
        auto)      cmd_install 0; verify_all; print_summary ;;
    esac
}

main "$@"
