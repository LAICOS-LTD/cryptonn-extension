#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
#  CryptONN Extension Installer v1.3.0
#  https://github.com/LAICOS-LTD/cryptonn-loader
#
#  Installs, updates, uninstalls and checks the status of the CryptONN PHP
#  extension (.so).  Supported environments:
#    • Bare Linux server  (Debian, Ubuntu, AlmaLinux, RHEL, CentOS)
#    • Plesk              (all installed PHP versions detected automatically)
#    • cPanel / EasyApache 4
#    • DirectAdmin
#
#  Usage:
#    bash install.sh [command] [options]
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

# ── Colours & symbols ─────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    R='\033[0;31m'  G='\033[0;32m'  Y='\033[1;33m'
    C='\033[0;36m'  B='\033[0;34m'  M='\033[0;35m'
    W='\033[1;37m'  D='\033[2m'     L='\033[1m'
    NC='\033[0m'
else
    R='' G='' Y='' C='' B='' M='' W='' D='' L='' NC=''
fi
OK="${G}✔${NC}";  WARN="${Y}⚠${NC}";  ERR="${R}✖${NC}"
ARR="${C}›${NC}"; DOT="${D}•${NC}";   INF="${B}ℹ${NC}"

# ── Box drawing ───────────────────────────────────────────────────────────────
W_BOX=62
_rep() { local s=""; for((i=0;i<$2;i++)); do s+="$1"; done; echo "$s"; }
_vis() { echo -e "$1" | sed 's/\x1b\[[0-9;]*m//g'; }
_pad() { local v; v=$(echo -e "$1" | sed 's/\x1b\[[0-9;]*m//g'); echo $(( $2 - ${#v} )); }

box_top()  { echo -e "${C}╔$(_rep ═ $W_BOX)╗${NC}"; }
box_bot()  { echo -e "${C}╚$(_rep ═ $W_BOX)╝${NC}"; }
box_sep()  { echo -e "${C}╠$(_rep ═ $W_BOX)╣${NC}"; }
box_nil()  { echo -e "${C}║$(_rep ' ' $W_BOX)║${NC}"; }
box_row()  {
    local txt="${1:-}" pad
    pad=$(_pad "$txt" $W_BOX)
    (( pad < 0 )) && pad=0
    printf "${C}║${NC}${txt}%${pad}s${C}║${NC}\n" ""
}
box_mid()  {
    local txt="${1:-}" col="${2:-$NC}" vis pad pl pr
    vis=$(echo -e "$txt" | sed 's/\x1b\[[0-9;]*m//g')
    pad=$(( W_BOX - ${#vis} ))
    pl=$(( pad / 2 )); pr=$(( pad - pl ))
    printf "${C}║${NC}%${pl}s${col}%s${NC}%${pr}s${C}║${NC}\n" "" "$txt" ""
}

sec()  { echo -e "\n  ${C}┌─ ${L}${W}$1${NC}${C} $(_rep ─ $(( W_BOX - ${#1} - 3 )))${NC}"; }
row()  { echo -e "  ${C}│${NC}  $*"; }
ok()   { echo -e "     ${OK}  $*"; }
warn() { echo -e "     ${WARN}  ${Y}$*${NC}"; }
die()  { echo -e "\n     ${ERR}  ${R}ERROR: $*${NC}\n" >&2; exit 1; }
info() { echo -e "     ${INF}  ${D}$*${NC}"; }

# ── Globals ───────────────────────────────────────────────────────────────────
RELEASE_BASE="https://github.com/LAICOS-LTD/cryptonn-loader/releases/latest/download"
RELEASE_API="https://api.github.com/repos/LAICOS-LTD/cryptonn-loader/releases/latest"
INSTALL_DIR="/opt/cryptonn"
VERSION_FILE=""                 # set after INSTALL_DIR is finalised
FORCE_PHP=""
PANEL=""
AUTO_YES=0
CMD="auto"                      # auto | install | update | uninstall | status

declare -a INSTALLED_OK=()
declare -a INSTALLED_UPDATED=()
declare -a INSTALLED_FAIL=()
declare -a UNINSTALLED_OK=()

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --install|-i)    CMD="install";   shift ;;
        --update|-u)     CMD="update";    shift ;;
        --uninstall)     CMD="uninstall"; shift ;;
        --status|-s)     CMD="status";    shift ;;
        --php|-p)        FORCE_PHP="$2";  shift 2 ;;
        --dir|-d)        INSTALL_DIR="$2";shift 2 ;;
        --yes|-y)        AUTO_YES=1;      shift ;;
        --help|-h)
            sed -n '2,29p' "$0" | sed 's/^# \?//; s/^#//'
            exit 0 ;;
        *) die "Unknown argument: $1  (use --help)" ;;
    esac
done

VERSION_FILE="${INSTALL_DIR}/.version"

# ── Header ────────────────────────────────────────────────────────────────────
print_header() {
    echo ""
    box_top
    box_nil
    box_mid "${L}${W}CryptONN Extension Installer${NC}"
    box_mid "${D}v1.3.0  —  github.com/LAICOS-LTD/cryptonn-loader${NC}"
    box_nil
    box_bot
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

ext_installed() {   # php_bin → 0 if cryptonn.so exists in extdir
    local extdir; extdir=$(php_extdir "$1")
    [[ -f "${extdir}/cryptonn.so" ]]
}

ext_loaded_ver() {  # php_bin → prints version string or "not loaded"
    "$1" -r "echo extension_loaded('cryptonn') ? phpversion('cryptonn') : '';" 2>/dev/null || true
}

# ── Version helpers ───────────────────────────────────────────────────────────
ver_file_read()   { [[ -f "$VERSION_FILE" ]] && cat "$VERSION_FILE" || echo ""; }
ver_file_write()  { echo "$1" > "$VERSION_FILE"; }
ver_file_delete() { rm -f "$VERSION_FILE"; }

get_latest_ver() {
    curl -fsSL --connect-timeout 10 "$RELEASE_API" 2>/dev/null \
        | grep '"tag_name"' | head -1 \
        | sed 's/.*"v\?\([0-9][^"]*\)".*/\1/'
}

ver_gt() {  # returns 0 if $1 > $2 (simple dot-compare, 3 parts)
    local IFS=.
    local a=($1) b=($2)
    for i in 0 1 2; do
        local ai=${a[$i]:-0} bi=${b[$i]:-0}
        (( ai > bi )) && return 0
        (( ai < bi )) && return 1
    done
    return 1   # equal
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
ARCH=""
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

# ══════════════════════════════════════════════════════════════════════════════
#  INSTALL
# ══════════════════════════════════════════════════════════════════════════════
install_for() {
    local php="$1" force="${2:-0}"
    [[ -x "$php" ]] || { warn "Not executable, skipping: $php"; return 0; }
    php_ok "$php"   || { warn "PHP $(php_ver "$php") < 7.2 — skipping."; return 0; }

    local ver slug extdir ini so_name so_path chk_path target
    ver=$(php_ver "$php"); slug="${ver/./}"
    extdir=$(php_extdir "$php"); ini=$(php_ini "$php")
    so_name="cryptonn-php${slug}-${ARCH}.so"
    so_path="${INSTALL_DIR}/${so_name}"
    chk_path="${so_path}.sha256"
    target="${extdir}/cryptonn.so"

    echo ""
    echo -e "  ${C}┌──────────────────────────────────────────────────────${NC}"
    echo -e "  ${C}│${NC}  ${L}${W}PHP ${ver}${NC}  ${D}${php}${NC}"
    echo -e "  ${C}│${NC}  ${D}ext dir › ${extdir}${NC}"
    echo -e "  ${C}│${NC}  ${D}php.ini › ${ini:-<not found>}${NC}"
    echo -e "  ${C}└──────────────────────────────────────────────────────${NC}"

    [[ -d "$extdir" ]] || { warn "Extension dir not found — skipping PHP $ver."; INSTALLED_FAIL+=("$ver"); return 0; }

    # Version check — skip if already on latest (unless --install forced)
    if (( force == 0 )) && ext_installed "$php"; then
        local cur lat
        cur=$(ext_loaded_ver "$php")
        lat=$(ver_file_read)
        if [[ -n "$cur" && "$cur" == "$lat" ]]; then
            printf "     %-16s" "Already at v${cur}"
            echo -e " ${INF} Up to date — skipping"
            INSTALLED_OK+=("$ver@${cur}")
            return 0
        fi
    fi

    # ── Download ──────────────────────────────────────────────────────────────
    printf "     %-16s" "Downloading..."
    if curl -fsSL --connect-timeout 20 --retry 3 \
            "${RELEASE_BASE}/${so_name}" -o "$so_path" 2>/dev/null; then
        local sz; sz=$(du -sh "$so_path" 2>/dev/null | cut -f1)
        echo -e " ${OK}  ${so_name}  ${D}(${sz})${NC}"
    else
        echo -e " ${ERR}  Download failed"
        warn "URL: ${RELEASE_BASE}/${so_name}"
        INSTALLED_FAIL+=("$ver"); return 0
    fi

    # ── Verify SHA-256 ────────────────────────────────────────────────────────
    printf "     %-16s" "SHA-256..."
    if curl -fsSL --connect-timeout 10 "${RELEASE_BASE}/${so_name}.sha256" \
            -o "$chk_path" 2>/dev/null; then
        local exp act
        exp=$(tr -d '[:space:]' < "$chk_path")
        act=$(sha256sum "$so_path" | awk '{print $1}')
        if [[ "$exp" == "$act" ]]; then
            echo -e " ${OK}  ${D}${act:0:20}…${NC}"
            rm -f "$chk_path"
        else
            echo -e " ${ERR}  Mismatch — aborting PHP $ver"
            rm -f "$so_path" "$chk_path"
            INSTALLED_FAIL+=("$ver"); return 0
        fi
    else
        echo -e " ${WARN}  Unavailable — skipped"
        rm -f "$chk_path"
    fi

    # ── Copy .so ──────────────────────────────────────────────────────────────
    local was_installed=0
    [[ -f "$target" ]] && was_installed=1
    printf "     %-16s" "Installing..."
    if cp "$so_path" "$target" && chmod 644 "$target"; then
        echo -e " ${OK}  ${target}"
    else
        echo -e " ${ERR}  Failed: ${so_path} → ${target}"
        INSTALLED_FAIL+=("$ver"); return 0
    fi

    # ── php.ini ───────────────────────────────────────────────────────────────
    printf "     %-16s" "php.ini..."
    if [[ -z "$ini" ]]; then
        echo -e " ${WARN}  Not found — add: extension=cryptonn"
    elif [[ ! -w "$ini" ]]; then
        echo -e " ${WARN}  Not writable: ${ini}"
    elif grep -qE "^;*[[:space:]]*extension=cryptonn" "$ini" 2>/dev/null; then
        sed -i 's|^;*[[:space:]]*extension=cryptonn.*|extension=cryptonn|' "$ini"
        echo -e " ${OK}  Enabled  ${D}(${ini})${NC}"
    else
        echo "extension=cryptonn" >> "$ini"
        echo -e " ${OK}  Added    ${D}(${ini})${NC}"
    fi

    (( was_installed )) && INSTALLED_UPDATED+=("$ver") || INSTALLED_OK+=("$ver")
}

# ── Restart FPM (Plesk) ───────────────────────────────────────────────────────
restart_plesk_fpm() {
    sec "Restarting Plesk PHP-FPM services"
    local n=0
    while IFS= read -r svc; do
        [[ -n "$svc" ]] || continue
        printf "     %-48s" "${svc}"
        if systemctl restart "$svc" 2>/dev/null; then
            echo -e " ${OK}"; (( n++ )) || true
        else
            echo -e " ${WARN} failed"
        fi
    done < <(systemctl list-units --type=service --state=active --no-legend 2>/dev/null \
             | awk '{print $1}' | grep -E 'plesk-php.*-fpm' || true)
    (( n == 0 )) && warn "No active plesk-php*-fpm services — restart FPM manually."
}

# ── Panel iterators ───────────────────────────────────────────────────────────
each_plesk_php()  { for p in /opt/plesk/php/*/bin/php;              do [[ -x "$p" ]] && echo "$p"; done; }
each_cpanel_php() { for p in /opt/cpanel/ea-php*/root/usr/bin/php;  do [[ -x "$p" ]] && echo "$p"; done; }
each_php() {
    case "$PANEL" in
        plesk)   each_plesk_php  ;;
        cpanel)  each_cpanel_php ;;
        *)
            if [[ -n "$FORCE_PHP" ]]; then echo "$FORCE_PHP"
            else
                for c in php php8.5 php8.4 php8.3 php8.2 php8.1 php8.0 php7.4 php7.3 php7.2; do
                    if command -v "$c" &>/dev/null; then command -v "$c"; break; fi
                done
            fi ;;
    esac
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
    echo -e "  ${C}┌──────────────────────────────────────────────────────${NC}"
    echo -e "  ${C}│${NC}  ${L}${W}PHP ${ver}${NC}  ${D}${php}${NC}"
    echo -e "  ${C}└──────────────────────────────────────────────────────${NC}"

    if [[ ! -f "$target" ]]; then
        printf "     %-16s" "Extension..."
        echo -e " ${INF}  Not installed — nothing to do"
        return 0
    fi

    # Remove .so
    printf "     %-16s" "Removing .so..."
    if rm -f "$target"; then
        echo -e " ${OK}  Deleted ${target}"
    else
        echo -e " ${ERR}  Could not remove ${target}"
        return 0
    fi

    # Remove from php.ini
    printf "     %-16s" "php.ini..."
    if [[ -n "$ini" && -w "$ini" ]]; then
        sed -i '/^[;]*[[:space:]]*extension=cryptonn/d' "$ini"
        echo -e " ${OK}  Line removed from ${ini}"
    else
        echo -e " ${WARN}  Skipped — remove manually: extension=cryptonn"
    fi

    UNINSTALLED_OK+=("$ver")
}

cmd_uninstall() {
    sec "Uninstalling CryptONN extension"

    # Confirm unless --yes
    if (( AUTO_YES == 0 )); then
        echo ""
        echo -e "  ${Y}${L}This will remove cryptonn.so from ALL PHP versions.${NC}"
        echo -ne "  ${W}Continue? [y/N] ${NC}"
        read -r ans
        [[ "$ans" =~ ^[Yy]$ ]] || { echo -e "  ${D}Aborted.${NC}"; exit 0; }
    fi

    local found=0
    while IFS= read -r php; do
        uninstall_for "$php"; (( found++ )) || true
    done < <(each_php)
    (( found == 0 )) && warn "No PHP binaries found."

    ver_file_delete

    case "$PANEL" in plesk) restart_plesk_fpm ;; esac

    # Summary
    echo ""
    box_top; box_nil
    if (( ${#UNINSTALLED_OK[@]} > 0 )); then
        box_mid "${G}${L}Uninstall complete!${NC}"
        box_nil; box_sep
        box_row "  ${G}${OK}${NC}  Removed from ${L}${#UNINSTALLED_OK[@]}${NC} PHP version(s):"
        for v in "${UNINSTALLED_OK[@]}"; do box_row "       ${D}•  PHP ${v}${NC}"; done
    else
        box_mid "${Y}${L}Nothing was removed${NC}"
    fi
    box_nil; box_bot; echo ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  STATUS
# ══════════════════════════════════════════════════════════════════════════════
cmd_status() {
    sec "Installation status"

    local lat_ver
    printf "     %-20s" "Checking GitHub..."
    lat_ver=$(get_latest_ver 2>/dev/null) || lat_ver=""
    if [[ -n "$lat_ver" ]]; then
        echo -e " ${OK}  Latest release: ${L}v${lat_ver}${NC}"
    else
        echo -e " ${WARN}  Could not reach GitHub"
    fi

    echo ""
    echo -e "  ${C}┌──────────────┬──────────────┬──────────────────────┐${NC}"
    echo -e "  ${C}│${NC}  ${L}PHP Version${NC}  ${C}│${NC}  ${L}Installed${NC}     ${C}│${NC}  ${L}Status${NC}               ${C}│${NC}"
    echo -e "  ${C}├──────────────┼──────────────┼──────────────────────┤${NC}"

    local any=0
    while IFS= read -r php; do
        [[ -x "$php" ]] || continue
        php_ok "$php"   || continue
        local ver cur_ver status_txt status_col
        ver=$(php_ver "$php")
        if ext_installed "$php"; then
            cur_ver=$(ext_loaded_ver "$php")
            [[ -z "$cur_ver" ]] && cur_ver="?"
            if [[ -n "$lat_ver" ]]; then
                if ver_gt "$lat_ver" "$cur_ver"; then
                    status_txt="Update available → v${lat_ver}"
                    status_col="${Y}"
                else
                    status_txt="Up to date"
                    status_col="${G}"
                fi
            else
                status_txt="Installed"
                status_col="${G}"
            fi
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${G}v%-12s${NC}${C}│${NC}  ${status_col}%-22s${NC}${C}│${NC}\n" \
                "PHP $ver" "$cur_ver" "$status_txt"
        else
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${D}%-14s${NC}${C}│${NC}  ${D}%-22s${NC}${C}│${NC}\n" \
                "PHP $ver" "—" "Not installed"
        fi
        (( any++ )) || true
    done < <(each_php)

    echo -e "  ${C}└──────────────┴──────────────┴──────────────────────┘${NC}"
    (( any == 0 )) && warn "No PHP binaries found."
    echo ""
}

# ══════════════════════════════════════════════════════════════════════════════
#  INSTALL / UPDATE — orchestration
# ══════════════════════════════════════════════════════════════════════════════
cmd_install() {
    local force="${1:-0}"
    local lat_ver cur_ver action_label

    sec "Fetching latest release version"
    printf "     %-20s" "GitHub release..."
    lat_ver=$(get_latest_ver 2>/dev/null) || lat_ver=""
    if [[ -n "$lat_ver" ]]; then
        echo -e " ${OK}  ${L}v${lat_ver}${NC}"
    else
        echo -e " ${WARN}  Unreachable — will install whatever was downloaded"
    fi

    cur_ver=$(ver_file_read)
    if [[ -n "$cur_ver" && -n "$lat_ver" ]]; then
        if [[ "$cur_ver" == "$lat_ver" && "$force" == "0" ]]; then
            echo ""
            ok "Already on v${cur_ver} — nothing to do."
            info "Use --install to force reinstall, or --status to inspect."
            echo ""
            return 0
        fi
        if ver_gt "$lat_ver" "$cur_ver"; then
            echo -e "     ${C}↑${NC}  Upgrade: ${D}v${cur_ver}${NC} → ${G}${L}v${lat_ver}${NC}"
        fi
    fi

    sec "Installing PHP extension(s)"

    local found=0
    while IFS= read -r php; do
        install_for "$php" "$force"
        (( found++ )) || true
    done < <(each_php)
    (( found == 0 )) && die "No PHP binaries found. Use --php /path/to/php"

    [[ -n "$lat_ver" ]] && ver_file_write "$lat_ver"

    case "$PANEL" in plesk) restart_plesk_fpm ;; esac
}

# ── Verification ──────────────────────────────────────────────────────────────
verify_all() {
    sec "Verification"
    echo ""
    echo -e "  ${C}┌──────────────┬──────────────┬──────────────────────┐${NC}"
    echo -e "  ${C}│${NC}  ${L}PHP Version${NC}  ${C}│${NC}  ${L}Version${NC}       ${C}│${NC}  ${L}Status${NC}               ${C}│${NC}"
    echo -e "  ${C}├──────────────┼──────────────┼──────────────────────┤${NC}"

    while IFS= read -r php; do
        [[ -x "$php" ]] || continue
        php_ok "$php"   || continue
        local ver v
        ver=$(php_ver "$php")
        v=$(ext_loaded_ver "$php")
        if [[ -n "$v" ]]; then
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${G}v%-12s${NC}${C}│${NC}  ${G}%-22s${NC}${C}│${NC}\n" \
                "PHP $ver" "$v" "Loaded ✔"
        else
            printf "  ${C}│${NC}  %-12s  ${C}│${NC}  ${D}%-14s${NC}${C}│${NC}  ${Y}%-22s${NC}${C}│${NC}\n" \
                "PHP $ver" "—" "Restart FPM/Apache"
        fi
    done < <(each_php)

    echo -e "  ${C}└──────────────┴──────────────┴──────────────────────┘${NC}"
    echo ""
}

# ── Final summary ─────────────────────────────────────────────────────────────
print_summary() {
    local n_ok=$(( ${#INSTALLED_OK[@]} + ${#INSTALLED_UPDATED[@]} ))
    local n_upd=${#INSTALLED_UPDATED[@]}
    local n_fail=${#INSTALLED_FAIL[@]}

    echo ""
    box_top; box_nil

    if (( n_ok > 0 )); then
        box_mid "${G}${L}Installation complete!${NC}"
    else
        box_mid "${Y}${L}Completed with warnings${NC}"
    fi

    box_nil; box_sep; box_nil

    if (( ${#INSTALLED_OK[@]} > 0 )); then
        box_row "  ${G}${OK}${NC}  Fresh install — ${L}${#INSTALLED_OK[@]}${NC} version(s):"
        for v in "${INSTALLED_OK[@]}"; do box_row "       ${D}•  PHP ${v}${NC}"; done
        box_nil
    fi
    if (( n_upd > 0 )); then
        box_row "  ${C}↑${NC}   Updated — ${L}${n_upd}${NC} version(s):"
        for v in "${INSTALLED_UPDATED[@]}"; do box_row "       ${D}•  PHP ${v}${NC}"; done
        box_nil
    fi
    if (( n_fail > 0 )); then
        box_row "  ${WARN}  Failed — ${L}${n_fail}${NC} version(s) (see warnings above):"
        for v in "${INSTALLED_FAIL[@]}"; do box_row "       ${D}•  PHP ${v}${NC}"; done
        box_nil
    fi

    box_sep; box_nil
    box_row "  ${D}No license key required on this server.${NC}"
    box_row "  ${D}The extension reads the license ID embedded in each${NC}"
    box_row "  ${D}encoded file and fetches the decryption key via API.${NC}"
    box_nil
    box_row "  Include encoded files normally:"
    box_row ""
    box_row "    ${C}require 'path/to/encoded_file.php';${NC}"
    box_nil
    box_row "  Verify installation:"
    box_row "    ${C}php -r \"echo extension_loaded('cryptonn') ? 'OK':'NO';\"${NC}"
    box_nil
    box_row "  Check for updates later:"
    box_row "    ${C}bash install.sh --status${NC}"
    box_nil
    box_bot; echo ""
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    print_header

    case "$CMD" in
        status)
            preflight
            cmd_status
            ;;
        uninstall)
            preflight
            cmd_uninstall
            ;;
        install)
            preflight
            cmd_install 1   # force=1
            verify_all
            print_summary
            ;;
        update)
            preflight
            cmd_install 0   # check version first
            verify_all
            print_summary
            ;;
        auto)
            preflight
            cmd_install 0
            verify_all
            print_summary
            ;;
    esac
}

main "$@"
