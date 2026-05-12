#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  CryptONN Extension Installer v1.3.0
#  https://github.com/LAICOS-LTD/cryptonn-extension
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
#    --restart-fpm  Restart all PHP-FPM services (Plesk / cPanel / bare)
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

# ── Colours ───────────────────────────────────────────────────────────────────
# printf '\033' produces the actual ESC byte (0x1b) on every system.
# Never use $'...' or echo -e for colour definitions — both have portability gaps.
_E=$(printf '\033')
if [[ -t 1 ]]; then
    CR="${_E}[0;31m"   # red
    CG="${_E}[0;32m"   # green
    CY="${_E}[1;33m"   # yellow
    CC="${_E}[0;36m"   # cyan
    CW="${_E}[1;37m"   # bold white
    CD="${_E}[2m"      # dim
    CB="${_E}[1m"      # bold
    NC="${_E}[0m"      # reset
else
    CR='' CG='' CY='' CC='' CW='' CD='' CB='' NC=''
fi

SYM_OK="${CG}✔${NC}"
SYM_WN="${CY}⚠${NC}"
SYM_ER="${CR}✖${NC}"
SYM_IF="${CC}i${NC}"
SYM_UP="${CC}↑${NC}"

# ── Output helpers ────────────────────────────────────────────────────────────
# Use printf '%s\n' — actual ESC bytes in variables pass through unchanged.
println() { printf '%s\n' "$*"; }
ok()      { printf '     %s  %s\n'   "$SYM_OK" "$*"; }
warn()    { printf '     %s  %b\n'   "$SYM_WN" "${CY}$*${NC}"; }
die()     { printf '\n  %s  %b\n\n'  "$SYM_ER" "${CR}ERROR: $*${NC}" >&2; exit 1; }
info()    { printf '     %s  %b\n'   "$SYM_IF" "${CD}$*${NC}"; }
sec()     { printf '\n  %s┌─ %s%s %s─────────────────────────────────────────%s\n' \
              "$CC" "$CB$CW" "$1" "$CC" "$NC"; }

# ── Globals ───────────────────────────────────────────────────────────────────
RELEASE_BASE="https://github.com/LAICOS-LTD/cryptonn-extension/releases/latest/download"
RELEASE_API="https://api.github.com/repos/LAICOS-LTD/cryptonn-extension/releases/latest"
INSTALL_DIR="/opt/cryptonn"
VERSION_FILE=""
FORCE_PHP=""
PANEL=""
AUTO_YES=0
CMD="auto"
ARCH=""

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
        --restart-fpm|-r) CMD="restart-fpm"; shift ;;
        --help|-h)
            sed -n '2,30p' "$0" | sed 's/^# \?//; s/^#//'
            exit 0 ;;
        *) die "Unknown argument: $1  (use --help)" ;;
    esac
done
VERSION_FILE="${INSTALL_DIR}/.version"

# ── Header ────────────────────────────────────────────────────────────────────
print_header() {
    # SP must be exactly 58 spaces (interior width = border ═ count)
    local SP="                                                          "
    println ""
    println "${CC}╔══════════════════════════════════════════════════════════╗${NC}"
    println "${CC}║${NC}${SP}${CC}║${NC}"
    println "${CC}║${NC}               ${CB}${CW}CryptONN Extension Installer${NC}               ${CC}║${NC}"
    println "${CC}║${NC}          ${CD}v1.3.0  ·  LAICOS-LTD/cryptonn-extension${NC}           ${CC}║${NC}"
    println "${CC}║${NC}${SP}${CC}║${NC}"
    println "${CC}╚══════════════════════════════════════════════════════════╝${NC}"
    println ""
}

# ── PHP helpers ───────────────────────────────────────────────────────────────
php_ver()    { "$1" -r 'echo PHP_MAJOR_VERSION.".".PHP_MINOR_VERSION;' 2>/dev/null; }
php_extdir() { "$1" -r 'echo ini_get("extension_dir");' 2>/dev/null; }
php_ini()    { "$1" --ini 2>/dev/null | awk -F': ' '/Loaded Configuration/{gsub(/ /,"",$2);print $2}'; }
php_ok()     {
    local v major minor
    [[ -x "$1" ]] || return 1
    v=$(php_ver "$1"); IFS='.' read -r major minor <<< "$v"
    (( major > 7 || (major == 7 && minor >= 2) ))
}
ext_installed() { local d; d=$(php_extdir "$1" 2>/dev/null); [[ -f "${d}/cryptonn.so" ]]; }
ext_ver()       { "$1" -r "echo extension_loaded('cryptonn') ? phpversion('cryptonn') : '';" 2>/dev/null || true; }

# ── Version helpers ───────────────────────────────────────────────────────────
ver_read()   { [[ -f "$VERSION_FILE" ]] && cat "$VERSION_FILE" || echo ""; }
ver_write()  { mkdir -p "$(dirname "$VERSION_FILE")"; echo "$1" > "$VERSION_FILE"; }
ver_delete() { rm -f "$VERSION_FILE"; }
get_latest() {
    curl -fsSL --connect-timeout 10 "$RELEASE_API" 2>/dev/null \
        | grep '"tag_name"' | head -1 \
        | sed 's/.*"v*\([0-9][^"]*\)".*/\1/'
}

# TLS-safe download: try default first, fall back to --tls-max 1.2 on failure.
# Some servers (AlmaLinux 8 / OpenSSL 1.1.1) have TLS 1.3 issues with GitHub CDN.
safe_dl() {
    local dst="$1"; shift
    curl -fsSL --connect-timeout 20 --retry 2 "$@" -o "$dst" 2>/dev/null && return 0
    curl -fsSL --connect-timeout 20 --retry 2 --tls-max 1.2 "$@" -o "$dst" 2>/dev/null
}
ver_gt() {
    # Guard: non-numeric strings (e.g. "?") compare as lowest possible version
    [[ "$1" =~ ^[0-9] ]] || return 1
    [[ "$2" =~ ^[0-9] ]] || return 0
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
#  INSTALL — one PHP version
# ══════════════════════════════════════════════════════════════════════════════
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

    println ""
    printf '  %s┌─ %sPHP %-3s%s ───────────────────────────────────────────%s\n' \
        "$CC" "$CB$CW" "$ver" "$CC" "$NC"
    printf '  %s│%s  %s%s%s\n'                 "$CC" "$NC" "$CD" "$php" "$NC"
    printf '  %s│%s  ext › %s%s%s\n'           "$CC" "$NC" "$CD" "$extdir" "$NC"
    printf '  %s│%s  ini › %s%s%s\n'           "$CC" "$NC" "$CD" "${ini:-not found}" "$NC"
    printf '  %s├─────────────────────────────────────────────────────%s\n' "$CC" "$NC"

    if [[ ! -d "$extdir" ]]; then
        printf '  %s│%s  %s  Extension dir not found — skipping\n' "$CC" "$NC" "$SYM_ER"
        printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
        V_FAILED+=("$ver"); return 0
    fi

    # Skip if already up to date
    if (( force == 0 )) && ext_installed "$php"; then
        local cur lat
        cur=$(ext_ver "$php"); lat=$(ver_read)
        if [[ -n "$cur" && -n "$lat" && "$cur" == "$lat" ]]; then
            printf '  %s│%s  %s  Already at %sv%s%s — up to date\n' \
                "$CC" "$NC" "$SYM_IF" "$CG" "$cur" "$NC"
            printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
            V_SKIPPED+=("$ver@${cur}"); return 0
        fi
    fi

    # Download
    printf '  %s│%s  %-18s' "$CC" "$NC" "Downloading..."
    if safe_dl "$so_path" "${RELEASE_BASE}/${so}"; then
        local sz; sz=$(du -sh "$so_path" 2>/dev/null | cut -f1)
        printf '%s  %s%s %s(%s)%s\n' "$SYM_OK" "$CD" "$so" "$CD" "$sz" "$NC"
    else
        printf '%s  Download failed\n' "$SYM_ER"
        printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
        V_FAILED+=("$ver"); return 0
    fi

    # SHA-256
    local chk="${so_path}.sha256"
    printf '  %s│%s  %-18s' "$CC" "$NC" "SHA-256..."
    if safe_dl "$chk" "${RELEASE_BASE}/${so}.sha256"; then
        local exp act
        exp=$(awk 'NR==1{print $1}' "$chk")
        act=$(sha256sum "$so_path" | awk '{print $1}')
        rm -f "$chk"
        if [[ "$exp" == "$act" ]]; then
            printf '%s  %s%s…%s\n' "$SYM_OK" "$CD" "${act:0:28}" "$NC"
        else
            printf '%s  Checksum mismatch — aborting PHP %s\n' "$SYM_ER" "$ver"
            printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
            rm -f "$so_path"; V_FAILED+=("$ver"); return 0
        fi
    else
        rm -f "$chk"
        printf '%s  Unavailable — skipped\n' "$SYM_WN"
    fi

    # Install
    local was=0; [[ -f "$target" ]] && was=1
    printf '  %s│%s  %-18s' "$CC" "$NC" "Installing..."
    if cp "$so_path" "$target" && chmod 644 "$target"; then
        printf '%s  %s%s%s\n' "$SYM_OK" "$CD" "$target" "$NC"
    else
        printf '%s  Copy failed\n' "$SYM_ER"
        printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
        V_FAILED+=("$ver"); return 0
    fi

    # php.ini
    printf '  %s│%s  %-18s' "$CC" "$NC" "php.ini..."
    if [[ -z "$ini" ]]; then
        printf '%s  Not found — add manually: extension=cryptonn\n' "$SYM_WN"
    elif [[ ! -w "$ini" ]]; then
        printf '%s  Not writable: %s\n' "$SYM_WN" "$ini"
    elif grep -qE "^;*[[:space:]]*extension=cryptonn" "$ini" 2>/dev/null; then
        sed -i 's|^;*[[:space:]]*extension=cryptonn.*|extension=cryptonn|' "$ini"
        printf '%s  %sEnabled%s in %s%s%s\n' "$SYM_OK" "$CG" "$NC" "$CD" "$ini" "$NC"
    else
        echo "extension=cryptonn" >> "$ini"
        printf '%s  %sAdded%s to %s%s%s\n' "$SYM_OK" "$CG" "$NC" "$CD" "$ini" "$NC"
    fi

    printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
    (( was )) && V_UPDATED+=("$ver") || V_FRESH+=("$ver")
}

# ── Restart PHP-FPM (all panels) ─────────────────────────────────────────────
restart_fpm() {
    sec "Restarting PHP-FPM services"
    local pattern n=0
    case "$PANEL" in
        plesk)       pattern='plesk-php.*-fpm' ;;
        cpanel)      pattern='ea-php.*-php-fpm' ;;
        directadmin) pattern='php.*-fpm' ;;
        *)           pattern='php.*-fpm' ;;
    esac

    while IFS= read -r svc; do
        [[ -n "$svc" ]] || continue
        printf '     %-48s' "$svc"
        if systemctl restart "$svc" 2>/dev/null; then
            printf '%s\n' "$SYM_OK"; (( n++ )) || true
        else
            printf '%s failed\n' "$SYM_WN"
        fi
    done < <(systemctl list-units --type=service --state=active --no-legend 2>/dev/null \
             | awk '{print $1}' | grep -E "$pattern" || true)
    (( n == 0 )) && warn "No active PHP-FPM services matched — restart FPM manually."
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

    println ""
    printf '  %s┌─ %sPHP %-3s%s ───────────────────────────────────────────%s\n' \
        "$CC" "$CB$CW" "$ver" "$CC" "$NC"
    printf '  %s│%s  %s%s%s\n'                 "$CC" "$NC" "$CD" "$php" "$NC"

    if [[ ! -f "$target" ]]; then
        printf '  %s│%s  %s  Not installed — nothing to do\n' "$CC" "$NC" "$SYM_IF"
        printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
        return 0
    fi

    printf '  %s│%s  %-18s' "$CC" "$NC" "Removing .so..."
    if rm -f "$target"; then
        printf '%s  %s%s%s\n' "$SYM_OK" "$CD" "$target" "$NC"
    else
        printf '%s  Failed to remove %s\n' "$SYM_ER" "$target"
        printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
        return 0
    fi

    printf '  %s│%s  %-18s' "$CC" "$NC" "php.ini..."
    if [[ -n "$ini" && -w "$ini" ]]; then
        sed -i '/^;*[[:space:]]*extension=cryptonn/d' "$ini"
        printf '%s  %sLine removed%s from %s%s%s\n' "$SYM_OK" "$CG" "$NC" "$CD" "$ini" "$NC"
    else
        printf '%s  Remove manually: extension=cryptonn\n' "$SYM_WN"
    fi

    printf '  %s└─────────────────────────────────────────────────────%s\n' "$CC" "$NC"
    V_REMOVED+=("$ver")
}

cmd_uninstall() {
    sec "Uninstall CryptONN extension"
    println ""

    if (( AUTO_YES == 0 )); then
        printf '  %s%s  This will remove cryptonn.so from ALL PHP versions.%s\n' "$CY" "$CB" "$NC"
        printf '  %sContinue? [y/N]%s ' "$CW" "$NC"
        read -r ans
        [[ "$ans" =~ ^[Yy]$ ]] || { println "  ${CD}Aborted.${NC}"; exit 0; }
    fi

    local n=0
    while IFS= read -r php; do
        uninstall_for "$php"; (( n++ )) || true
    done < <(each_php)
    (( n == 0 )) && warn "No PHP binaries found."
    ver_delete
    restart_fpm

    println ""
    println "${CC}╔══════════════════════════════════════════════════════════╗${NC}"
    if (( ${#V_REMOVED[@]} > 0 )); then
        println "${CC}║${NC}  ${CG}${CB}Uninstall complete!${NC}                                     ${CC}║${NC}"
        println "${CC}╠══════════════════════════════════════════════════════════╣${NC}"
        printf  "${CC}║${NC}  %s  Removed from %s%d%s PHP version(s):%-21s${CC}║${NC}\n" \
            "$SYM_OK" "$CB" "${#V_REMOVED[@]}" "$NC" ""
        for v in "${V_REMOVED[@]}"; do
            printf "${CC}║${NC}     ${CD}•  PHP %-3s${NC}%-43s${CC}║${NC}\n" "$v" ""
        done
    else
        println "${CC}║${NC}  ${CY}Nothing was removed${NC}                                     ${CC}║${NC}"
    fi
    println "${CC}╚══════════════════════════════════════════════════════════╝${NC}"
    println ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  STATUS
# ══════════════════════════════════════════════════════════════════════════════
cmd_status() {
    sec "Installation status"
    println ""
    printf '     %-22s' "Checking GitHub..."
    local lat; lat=$(get_latest 2>/dev/null) || lat=""
    if [[ -n "$lat" ]]; then
        printf '%s  Latest release: %s%sv%s%s\n' "$SYM_OK" "$CG" "$CB" "$lat" "$NC"
    else
        printf '%s  Could not reach GitHub\n' "$SYM_WN"
    fi
    println ""
    printf '  %s┌────────────────┬────────────────┬──────────────────────────┐%s\n' "$CC" "$NC"
    printf '  %s│%s  %sPhP Version%s   %s│%s  %sInstalled%s     %s│%s  %sStatus%s                  %s│%s\n' \
        "$CC" "$NC" "$CB" "$NC" "$CC" "$NC" "$CB" "$NC" "$CC" "$NC" "$CB" "$NC" "$CC" "$NC"
    printf '  %s├────────────────┼────────────────┼──────────────────────────┤%s\n' "$CC" "$NC"

    local any=0
    while IFS= read -r php; do
        [[ -x "$php" ]] || continue; php_ok "$php" || continue
        local ver cur_v stat_str stat_col
        ver=$(php_ver "$php")
        if ext_installed "$php"; then
            cur_v=$(ext_ver "$php"); [[ -z "$cur_v" ]] && cur_v="?"
            if [[ -n "$lat" ]] && ver_gt "$lat" "$cur_v"; then
                stat_str="Update → v${lat}"; stat_col="$CY"
            else
                stat_str="Up to date"; stat_col="$CG"
            fi
            printf '  %s│%s  %-12s  %s│%s  %sv%-12s %s%s│%s  %s%-24s%s%s│%s\n' \
                "$CC" "$NC" "PHP $ver" "$CC" "$NC" "$CG" "$cur_v" "$NC" "$CC" "$NC" \
                "$stat_col" "$stat_str" "$NC" "$CC" "$NC"
        else
            printf '  %s│%s  %-12s  %s│%s  %-14s%s│%s  %-24s%s│%s\n' \
                "$CC" "$NC" "PHP $ver" "$CC" "$NC" "—" "$CC" "$NC" "Not installed" "$CC" "$NC"
        fi
        (( any++ )) || true
    done < <(each_php)

    printf '  %s└────────────────┴────────────────┴──────────────────────────┘%s\n' "$CC" "$NC"
    (( any == 0 )) && warn "No PHP binaries found."
    println ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  INSTALL / UPDATE — orchestration
# ══════════════════════════════════════════════════════════════════════════════
cmd_install() {
    local force="${1:-0}"

    sec "Fetching latest release"
    printf '     %-22s' "GitHub API..."
    local lat; lat=$(get_latest 2>/dev/null) || lat=""
    if [[ -n "$lat" ]]; then
        printf '%s  %s%sv%s%s\n' "$SYM_OK" "$CG" "$CB" "$lat" "$NC"
    else
        printf '%s  Unreachable — installing cached download\n' "$SYM_WN"
    fi

    local cur; cur=$(ver_read)
    if [[ -n "$cur" && -n "$lat" && "$cur" == "$lat" && "$force" == "0" ]]; then
        println ""
        ok "Already on ${CG}v${cur}${NC} — nothing to do."
        info "Use --install to force reinstall, or --status to inspect."
        println ""; return 0
    fi
    if [[ -n "$cur" && -n "$lat" ]] && ver_gt "$lat" "$cur"; then
        printf '     %s  Upgrading %sv%s%s → %s%sv%s%s\n' \
            "$SYM_UP" "$CD" "$cur" "$NC" "$CG" "$CB" "$lat" "$NC"
    fi

    sec "Installing extension"
    local n=0
    while IFS= read -r php; do
        install_for "$php" "$force"; (( n++ )) || true
    done < <(each_php)
    (( n == 0 )) && die "No PHP binaries found. Use --php /path/to/php"

    [[ -n "$lat" ]] && ver_write "$lat"
    restart_fpm
}

# ── Verification table ────────────────────────────────────────────────────────
verify_all() {
    sec "Verification"
    println ""
    printf '  %s┌────────────────┬────────────────┬──────────────────────────┐%s\n' "$CC" "$NC"
    printf '  %s│%s  %sPHP Version%s   %s│%s  %sVersion%s       %s│%s  %sStatus%s                  %s│%s\n' \
        "$CC" "$NC" "$CB" "$NC" "$CC" "$NC" "$CB" "$NC" "$CC" "$NC" "$CB" "$NC" "$CC" "$NC"
    printf '  %s├────────────────┼────────────────┼──────────────────────────┤%s\n' "$CC" "$NC"

    while IFS= read -r php; do
        [[ -x "$php" ]] || continue; php_ok "$php" || continue
        local ver v
        ver=$(php_ver "$php"); v=$(ext_ver "$php")
        if [[ -n "$v" ]]; then
            printf '  %s│%s  %-12s  %s│%s  %sv%-12s %s%s│%s  %s%-24s%s%s│%s\n' \
                "$CC" "$NC" "PHP $ver" "$CC" "$NC" "$CG" "$v" "$NC" "$CC" "$NC" \
                "$CG" "Loaded  ✔" "$NC" "$CC" "$NC"
        else
            printf '  %s│%s  %-12s  %s│%s  %-14s%s│%s  %s%-24s%s%s│%s\n' \
                "$CC" "$NC" "PHP $ver" "$CC" "$NC" "—" "$CC" "$NC" \
                "$CY" "Restart FPM / Apache" "$NC" "$CC" "$NC"
        fi
    done < <(each_php)

    printf '  %s└────────────────┴────────────────┴──────────────────────────┘%s\n' "$CC" "$NC"
    println ""
}

# ── Final summary ─────────────────────────────────────────────────────────────
print_summary() {
    local n_ok=$(( ${#V_FRESH[@]} + ${#V_UPDATED[@]} ))
    local n_skip=${#V_SKIPPED[@]}
    local n_fail=${#V_FAILED[@]}

    println ""
    println "${CC}╔══════════════════════════════════════════════════════════╗${NC}"

    if   (( n_ok   > 0 )); then
        println "${CC}║${NC}  ${CG}${CB}Installation complete!${NC}                                   ${CC}║${NC}"
    elif (( n_skip > 0 )); then
        println "${CC}║${NC}  ${CC}${CB}Already up to date${NC}                                       ${CC}║${NC}"
    else
        println "${CC}║${NC}  ${CY}${CB}Completed with warnings${NC}                                  ${CC}║${NC}"
    fi

    println "${CC}╠══════════════════════════════════════════════════════════╣${NC}"
    println "${CC}║${NC}                                                          ${CC}║${NC}"

    if (( ${#V_FRESH[@]} > 0 )); then
        printf  "${CC}║${NC}  %s  %sInstalled%s on %s%d%s PHP version(s):%-21s${CC}║${NC}\n" \
            "$SYM_OK" "$CG" "$NC" "$CB" "${#V_FRESH[@]}" "$NC" ""
        for v in "${V_FRESH[@]}"; do
            printf "${CC}║${NC}     ${CD}•  PHP %-3s${NC}%-43s${CC}║${NC}\n" "$v" ""
        done
        println "${CC}║${NC}                                                          ${CC}║${NC}"
    fi
    if (( ${#V_UPDATED[@]} > 0 )); then
        printf  "${CC}║${NC}  %s  %sUpdated%s on %s%d%s PHP version(s):%-23s${CC}║${NC}\n" \
            "$SYM_UP" "$CC" "$NC" "$CB" "${#V_UPDATED[@]}" "$NC" ""
        for v in "${V_UPDATED[@]}"; do
            printf "${CC}║${NC}     ${CD}•  PHP %-3s${NC}%-43s${CC}║${NC}\n" "$v" ""
        done
        println "${CC}║${NC}                                                          ${CC}║${NC}"
    fi
    if (( n_skip > 0 )); then
        printf  "${CC}║${NC}  %s  Already up to date on %s%d%s version(s):%-15s${CC}║${NC}\n" \
            "$SYM_IF" "$CB" "$n_skip" "$NC" ""
        for v in "${V_SKIPPED[@]}"; do
            printf "${CC}║${NC}     ${CD}•  PHP %-3s${NC}%-43s${CC}║${NC}\n" "${v%%@*}" ""
        done
        println "${CC}║${NC}                                                          ${CC}║${NC}"
    fi
    if (( n_fail > 0 )); then
        printf  "${CC}║${NC}  %s  Failed on %s%d%s version(s) — see warnings above:%-5s${CC}║${NC}\n" \
            "$SYM_WN" "$CB" "$n_fail" "$NC" ""
        for v in "${V_FAILED[@]}"; do
            printf "${CC}║${NC}     ${CD}•  PHP %-3s${NC}%-43s${CC}║${NC}\n" "$v" ""
        done
        println "${CC}║${NC}                                                          ${CC}║${NC}"
    fi

    println "${CC}╠══════════════════════════════════════════════════════════╣${NC}"
    println "${CC}║${NC}                                                          ${CC}║${NC}"
    println "${CC}║${NC}  ${CD}No license key required on this server.${NC}               ${CC}║${NC}"
    println "${CC}║${NC}  ${CD}License ID is read from each encoded file header.${NC}     ${CC}║${NC}"
    println "${CC}║${NC}                                                          ${CC}║${NC}"
    println "${CC}║${NC}  Include encoded files normally:                          ${CC}║${NC}"
    println "${CC}║${NC}    ${CC}require 'path/to/encoded_file.php';${NC}                  ${CC}║${NC}"
    println "${CC}║${NC}                                                          ${CC}║${NC}"
    println "${CC}║${NC}  Check for updates:                                       ${CC}║${NC}"
    println "${CC}║${NC}    ${CC}bash install.sh --status${NC}                              ${CC}║${NC}"
    println "${CC}║${NC}                                                          ${CC}║${NC}"
    println "${CC}╚══════════════════════════════════════════════════════════╝${NC}"
    println ""
}

# ── Standalone FPM restart ────────────────────────────────────────────────────
cmd_restart_fpm() {
    [[ $EUID -eq 0 ]] || die "Must be run as root (or with sudo)."
    PANEL=$(detect_panel)
    restart_fpm
    println ""
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    print_header
    if [[ "$CMD" == "restart-fpm" ]]; then
        cmd_restart_fpm
        return
    fi
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

