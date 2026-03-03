#!/bin/bash
#
# Install the Bebop compiler, libraries, and schemas.
# https://bebop.sh
#
# Apache License 2.0

set -u

abort() {
    printf "%s\n" "$@" >&2
    exit 1
}

if [ -z "${BASH_VERSION:-}" ]; then
    abort "Bash is required to run this installer."
fi

if [[ -t 1 ]]; then
    bold=$'\033[1m' red=$'\033[1;31m' blue=$'\033[1;34m' reset=$'\033[0m'
else
    bold='' red='' blue='' reset=''
fi

info() { printf "${blue}==>${bold} %s${reset}\n" "$*"; }
warn() { printf "${red}Warning${reset}: %s\n" "$*"; }
step() { printf "  %s " "$*"; }
ok()   { printf '\342\234\205\n'; }

ring_bell() { [[ -t 1 ]] && printf "\a"; }

# Semver comparison — cloudflare/semver_bash

# shellcheck disable=SC2001
semver_parse() {
    local RE='[^0-9]*\([0-9]*\)[.]\([0-9]*\)[.]\([0-9]*\)\([0-9A-Za-z-]*\)'
    eval "$2=$(echo "$1" | sed -e "s#$RE#\1#")"
    eval "$3=$(echo "$1" | sed -e "s#$RE#\2#")"
    eval "$4=$(echo "$1" | sed -e "s#$RE#\3#")"
    eval "$5=$(echo "$1" | sed -e "s#$RE#\4#")"
}

semver_eq() {
    local MAJOR_A=0 MINOR_A=0 PATCH_A=0 PRE_A=0
    local MAJOR_B=0 MINOR_B=0 PATCH_B=0 PRE_B=0
    semver_parse "$1" MAJOR_A MINOR_A PATCH_A PRE_A
    semver_parse "$2" MAJOR_B MINOR_B PATCH_B PRE_B
    [[ $MAJOR_A -eq $MAJOR_B && $MINOR_A -eq $MINOR_B && $PATCH_A -eq $PATCH_B && "_$PRE_A" == "_$PRE_B" ]]
}

semver_lt() {
    local MAJOR_A=0 MINOR_A=0 PATCH_A=0 PRE_A=0
    local MAJOR_B=0 MINOR_B=0 PATCH_B=0 PRE_B=0
    semver_parse "$1" MAJOR_A MINOR_A PATCH_A PRE_A
    semver_parse "$2" MAJOR_B MINOR_B PATCH_B PRE_B
    [[ $MAJOR_A -lt $MAJOR_B ]] && return 0
    [[ $MAJOR_A -le $MAJOR_B && $MINOR_A -lt $MINOR_B ]] && return 0
    [[ $MAJOR_A -le $MAJOR_B && $MINOR_A -le $MINOR_B && $PATCH_A -lt $PATCH_B ]] && return 0
    [[ "_$PRE_A" == "_" && "_$PRE_B" == "_" ]] && return 1
    [[ "_$PRE_A" == "_" ]] && return 1
    [[ "_$PRE_B" == "_" ]] && return 0
    [[ "_$PRE_A" < "_$PRE_B" ]]
}

semver_gt() {
    semver_eq "$1" "$2" && return 1
    semver_lt "$1" "$2" && return 1
    return 0
}

usage() {
    cat <<EOF
Usage: install.sh [VERSION] [--root]

Install the Bebop compiler, libraries, and schemas.

  VERSION    Version to install. Defaults to the latest release.
  --root     Install to /usr/local instead of ~/.local.
             Required for system tools like Xcode that do not search ~/.local.

Examples:
  install.sh                    Install latest to ~/.local
  install.sh 2026.0.0           Install specific version to ~/.local
  install.sh --root             Install latest to /usr/local
  install.sh 2026.0.0 --root    Install specific version to /usr/local
EOF
    exit 0
}

if [[ -n "${CI-}" && -n "${INTERACTIVE-}" ]]; then
    abort "Cannot use force-interactive mode in CI."
fi

# shellcheck disable=SC2016
if [[ -n "${INTERACTIVE-}" && -n "${NONINTERACTIVE-}" ]]; then
    abort 'Both $INTERACTIVE and $NONINTERACTIVE are set. Unset one and try again.'
fi

# shellcheck disable=SC2016
if [[ -z "${NONINTERACTIVE-}" ]]; then
    if [[ -n "${CI-}" ]]; then
        warn 'Non-interactive mode: $CI is set.'
        NONINTERACTIVE=1
    elif [[ ! -t 0 ]]; then
        if [[ -z "${INTERACTIVE-}" ]]; then
            warn 'Non-interactive mode: stdin is not a TTY.'
            NONINTERACTIVE=1
        else
            warn 'Running interactive despite stdin not being a TTY because $INTERACTIVE is set.'
        fi
    fi
else
    info 'Non-interactive mode: $NONINTERACTIVE is set.'
fi

REPO="6over3/bebop-next"
VERSION=""
PREFIX="${HOME}/.local"
ROOT_INSTALL=0

for arg in "$@"; do
    case "$arg" in
        --root)  ROOT_INSTALL=1 ;;
        --help)  usage ;;
        -*)      abort "Unknown flag: $arg" ;;
        *)       VERSION="$arg" ;;
    esac
done

[[ $ROOT_INSTALL -eq 1 ]] && PREFIX="/usr/local"

shopt -s nocasematch
IFS=" " read -ra uname_out <<<"$(uname -sm)"

case "${uname_out[0]}" in
    Darwin)       os=darwin ;;
    Linux | GNU*) os=linux ;;
    *)            abort "Unsupported OS: ${uname_out[0]}" ;;
esac

case "${uname_out[1]}" in
    x86_64 | amd64)   arch=x64 ;;
    arm64 | aarch64)   arch=arm64 ;;
    *)                 abort "Unsupported architecture: ${uname_out[1]}" ;;
esac

shopt -u nocasematch
readonly os arch

unset HAVE_SUDO_ACCESS

have_sudo_access() {
    [[ -x /usr/bin/sudo ]] || return 1

    local -a SUDO=("/usr/bin/sudo")
    [[ -n "${SUDO_ASKPASS-}" ]] && SUDO+=("-A")
    [[ -z "${SUDO_ASKPASS-}" && -n "${NONINTERACTIVE-}" ]] && SUDO+=("-n")

    if [[ -z "${HAVE_SUDO_ACCESS-}" ]]; then
        if [[ -n "${NONINTERACTIVE-}" ]]; then
            "${SUDO[@]}" -l mkdir &>/dev/null
        else
            "${SUDO[@]}" -v && "${SUDO[@]}" -l mkdir &>/dev/null
        fi
        HAVE_SUDO_ACCESS="$?"
    fi

    if [[ "$os" != "linux" && "${HAVE_SUDO_ACCESS}" -ne 0 ]]; then
        abort "sudo access required on macOS. ${USER} must be an administrator."
    fi

    return "${HAVE_SUDO_ACCESS}"
}

execute() {
    if [[ $ROOT_INSTALL -eq 1 ]] && have_sudo_access; then
        /usr/bin/sudo "$@"
    else
        "$@"
    fi
}

download() {
    if command -v curl >/dev/null; then
        curl -fsSL -o "$2" "$1"
    elif command -v wget >/dev/null; then
        wget -q -O "$2" "$1"
    fi
}

fetch() {
    if command -v curl >/dev/null; then
        curl -fsSL "$1"
    elif command -v wget >/dev/null; then
        wget -qO- "$1"
    fi
}

[[ -z "${USER-}" ]] && { USER="$(id -un)"; export USER; }

# Ensure a sane CWD — when piped from curl the working directory may not exist
cd /tmp || exit 1

# Invalidate sudo timestamp on exit if it was not already active
if [[ -x /usr/bin/sudo ]] && ! /usr/bin/sudo -n -v 2>/dev/null; then
    trap '/usr/bin/sudo -k' EXIT
fi

# Reject direct root execution outside of CI containers
if [[ "${EUID:-${UID}}" == "0" ]]; then
    if ! [[ -f /proc/1/cgroup ]] ||
        ! grep -qE "azpl_job|actions_job|docker|garden|kubepods" /proc/1/cgroup; then
        abort "Do not run this installer as root."
    fi
fi

info "Checking dependencies..."
if ! command -v curl >/dev/null && ! command -v wget >/dev/null; then
    abort "curl or wget is required."
fi

if [[ $ROOT_INSTALL -eq 1 ]]; then
    info "Requesting sudo access..."
    if [[ "$os" != "linux" ]]; then
        have_sudo_access
    elif [[ -n "${NONINTERACTIVE-}" ]] && ! have_sudo_access; then
        abort "Insufficient permissions to install to ${PREFIX}."
    fi
fi

if [[ -d "$PREFIX" && ! -x "$PREFIX" ]]; then
    abort "${PREFIX} exists but is not searchable. Run: sudo chmod 775 ${PREFIX}"
fi

if [[ -z "$VERSION" ]]; then
    info "Resolving latest version..."
    VERSION=$(fetch "https://api.github.com/repos/${REPO}/releases/latest" \
        | grep '"tag_name"' | head -1 | cut -d'"' -f4)
    [[ -n "$VERSION" ]] || abort "Failed to determine latest version from GitHub."
fi

readonly VERSION
readonly RELEASE_URL="https://github.com/${REPO}/releases/download/${VERSION}"

if command -v bebopc &>/dev/null; then
    installed="$(bebopc --version 2>/dev/null)"

    if semver_eq "$installed" "$VERSION"; then
        info "bebopc ${installed} is already installed and up to date."
        exit 0
    fi

    if semver_gt "$installed" "$VERSION"; then
        abort "bebopc ${installed} is newer than ${VERSION}. Uninstall first to downgrade."
    fi

    if semver_lt "$installed" "$VERSION"; then
        warn "Upgrading bebopc from ${installed} to ${VERSION}."
    fi
else
    info "This will install to ${bold}${PREFIX}${reset}:"
    cat <<EOF
  ${bold}bin/${reset}          bebopc compiler and code generators
  ${bold}lib/${reset}          libbebop static library
  ${bold}include/${reset}      C and C++ headers
  ${bold}share/bebop/${reset}  schemas and runtime support files
EOF
fi

target="${os}-${arch}"
archive="bebop@${VERSION}+${target}.tar.gz"

info "Installing Bebop ${VERSION} for ${target}..."
(
    step "Creating temp directory"
    tmpdir=$(mktemp -d) || { printf "\n"; abort "Failed to create temp directory."; }
    trap 'rm -rf "$tmpdir"' EXIT
    ok

    step "Downloading ${archive}"
    if ! download "${RELEASE_URL}/${archive}" "${tmpdir}/${archive}"; then
        printf "\n"
        abort "Download failed: ${RELEASE_URL}/${archive}"
    fi
    ok

    step "Extracting to ${PREFIX}"
    execute mkdir -p "${PREFIX}"
    if ! execute tar xzf "${tmpdir}/${archive}" -C "${PREFIX}" 2>/dev/null; then
        printf "\n"
        abort "Extraction failed. The archive may be corrupt."
    fi
    ok

    if [[ ! -f "${PREFIX}/bin/bebopc" ]]; then
        abort "bebopc binary not found after extraction."
    fi
    execute chmod +x "${PREFIX}"/bin/bebopc*

    echo "  Installed:"
    for f in "${PREFIX}"/bin/bebopc*; do
        [[ -x "$f" ]] && echo "    $(basename "$f")"
    done

    echo
    info "Installation complete."
    ring_bell
    echo

    # Detect shell profile for PATH instructions
    case "${SHELL}" in
        */bash*)
            [[ -r "${HOME}/.bash_profile" ]] \
                && shell_profile="${HOME}/.bash_profile" \
                || shell_profile="${HOME}/.profile"
            ;;
        */zsh*)   shell_profile="${HOME}/.zprofile" ;;
        */fish*)  shell_profile="${HOME}/.config/fish/config.fish" ;;
        */nu*)    shell_profile="${HOME}/.config/nushell/env.nu" ;;
        *)        shell_profile="${HOME}/.profile" ;;
    esac

    info "Next steps:"
    if [[ ":${PATH}:" != *":${PREFIX}/bin:"* ]]; then
        echo "  Add bebop to your PATH:"
        case "${SHELL}" in
            */fish*)
                echo "    ${bold}fish_add_path ${PREFIX}/bin${reset}"
                ;;
            */nu*)
                echo "    ${bold}echo '\$env.PATH = (\$env.PATH | prepend \"${PREFIX}/bin\")' | save --append ${shell_profile}${reset}"
                ;;
            *)
                echo "    ${bold}echo 'export PATH=\"${PREFIX}/bin:\$PATH\"' >> ${shell_profile}${reset}"
                echo "    ${bold}export PATH=\"${PREFIX}/bin:\$PATH\"${reset}"
                ;;
        esac
        echo
    fi
    echo "  ${bold}bebopc --help${reset} to get started"
    echo "  ${bold}https://bebop.sh${reset} for documentation"
) || exit 1
