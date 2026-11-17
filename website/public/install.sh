#!/bin/sh
# Installs `bui`, the command that makes, runs, checks and updates BUI apps.
#
#   curl -LsSf https://bui.preetham.top/install.sh | sh
#
# Linux and macOS (x86_64 and arm64). It downloads the newest release from GitHub, checks its
# checksum, puts `bui` in ~/.bui/bin and adds that folder to your shell's PATH.
#
#   BUI_VERSION=v0.5.1        install that release instead of the newest
#   BUI_INSTALL_DIR=DIR       install into DIR (default: ~/.bui/bin)
#   BUI_NO_MODIFY_PATH=1      do not touch any shell profile
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

REPO="ppmpreetham/BUI"
INSTALL_DIR="${BUI_INSTALL_DIR:-$HOME/.bui/bin}"
BASE_URL="${BUI_INSTALLER_BASE_URL:-}" # for testing: a folder that holds the archive and its .sha256

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  ORANGE="$(printf '\033[38;5;208m')"; DIM="$(printf '\033[2m')"; RED="$(printf '\033[31m')"
  GREEN="$(printf '\033[32m')"; BOLD="$(printf '\033[1m')"; RESET="$(printf '\033[0m')"
else
  ORANGE=""; DIM=""; RED=""; GREEN=""; BOLD=""; RESET=""
fi

say() { printf '%s\n' "$*"; }
step() { printf '%s●%s %s\n' "$ORANGE" "$RESET" "$*"; }
ok() { printf '%s✔%s %s\n' "$GREEN" "$RESET" "$*"; }
die() { printf '%s✘ %s%s\n' "$RED" "$*" "$RESET" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# download URL FILE (a progress bar on a terminal)
download() {
  if have curl; then
    if [ -t 2 ]; then curl -fL --progress-bar -o "$2" "$1"; else curl -fsSL -o "$2" "$1"; fi
  elif have wget; then
    wget -q ${BUI_WGET_PROGRESS:+--show-progress} -O "$2" "$1"
  else
    die "need curl or wget to download bui"
  fi
}

banner() {
  say ""
  say "${ORANGE}██████╗ ██╗   ██╗██╗${RESET}"
  say "${ORANGE}██╔══██╗██║   ██║██║${RESET}"
  say "${ORANGE}██████╔╝██║   ██║██║${RESET}"
  say "${ORANGE}██╔══██╗██║   ██║██║${RESET}"
  say "${ORANGE}██████╔╝╚██████╔╝██║${RESET}   ${BOLD}installing bui${RESET}"
  say "${ORANGE}╚═════╝  ╚═════╝ ╚═╝${RESET}"
  say ""
}

target() {
  os="$(uname -s)"; arch="$(uname -m)"
  case "$arch" in
    x86_64 | amd64) arch="x86_64" ;;
    aarch64 | arm64) arch="aarch64" ;;
    *) die "there is no bui build for the $arch processor" ;;
  esac
  case "$os" in
    Linux) echo "$arch-unknown-linux-gnu" ;;
    Darwin) echo "$arch-apple-darwin" ;;
    *) die "this installer is for Linux and macOS: on Windows use  irm https://bui.preetham.top/install.ps1 | iex" ;;
  esac
}

# the tag of the newest release, from the page's redirect (the API would rate-limit)
latest_tag() {
  if have curl; then
    url="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest")"
  else
    url="$(wget -q --max-redirect=0 --server-response -O /dev/null "https://github.com/$REPO/releases/latest" 2>&1 | sed -n 's/^ *Location: //p' | tail -n 1)"
  fi
  tag="${url##*/tag/}"
  case "$tag" in v[0-9]*) echo "$tag" ;; *) die "could not find the newest release of $REPO (offline, or no release yet)" ;; esac
}

sha256_of() {
  if have sha256sum; then sha256sum "$1" | cut -d ' ' -f 1
  elif have shasum; then shasum -a 256 "$1" | cut -d ' ' -f 1
  else die "need sha256sum or shasum to check the download"; fi
}

add_to_path() {
  case ":$PATH:" in *":$INSTALL_DIR:"*) return 0 ;; esac
  [ -n "${BUI_NO_MODIFY_PATH:-}" ] && { say "${DIM}Add $INSTALL_DIR to your PATH to use it.${RESET}"; return 0; }
  line="export PATH=\"$INSTALL_DIR:\$PATH\" # added by the bui installer"
  changed=""
  for rc in "$HOME/.profile" "$HOME/.bashrc" "$HOME/.zshrc"; do
    # profiles that exist (and .profile always, as login shells read it)
    if [ -f "$rc" ] || [ "$rc" = "$HOME/.profile" ]; then
      if ! grep -qs "added by the bui installer" "$rc" 2>/dev/null; then
        printf '\n%s\n' "$line" >> "$rc"
        changed="$changed $rc"
      fi
    fi
  done
  if have fish && [ -d "$HOME/.config/fish" ]; then
    mkdir -p "$HOME/.config/fish/conf.d"
    printf 'fish_add_path "%s"\n' "$INSTALL_DIR" > "$HOME/.config/fish/conf.d/bui.fish"
    changed="$changed fish"
  fi
  [ -n "$changed" ] && ok "Added $INSTALL_DIR to your PATH (${changed# })"
  return 0
}

main() {
  banner
  triple="$(target)"
  tag="${BUI_VERSION:-}"
  if [ -z "$tag" ]; then step "Looking for the newest release"; tag="$(latest_tag)"; fi
  case "$tag" in v*) ;; *) tag="v$tag" ;; esac
  asset="bui-$triple.tar.gz"
  if [ -n "$BASE_URL" ]; then url="$BASE_URL/$asset"; else url="https://github.com/$REPO/releases/download/$tag/$asset"; fi

  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT INT TERM

  step "Downloading bui ${tag#v} for $triple"
  download "$url" "$tmp/$asset" || die "could not download $url"
  download "$url.sha256" "$tmp/$asset.sha256" || die "could not download the checksum"
  want="$(cut -d ' ' -f 1 < "$tmp/$asset.sha256" | tr 'A-F' 'a-f')"
  got="$(sha256_of "$tmp/$asset")"
  [ "$want" = "$got" ] || die "the download does not match its checksum: not installing it"
  ok "Checksum matches"

  tar -xzf "$tmp/$asset" -C "$tmp" || die "could not unpack the download"
  [ -f "$tmp/bui" ] || die "the download has no bui in it"
  mkdir -p "$INSTALL_DIR"
  # the new file is written beside the old one (the same disk), then renamed over it in one step: the old
  # bui stays whole until the new one is complete, and a running one is replaced, not overwritten
  staged="$(mktemp "$INSTALL_DIR/.bui-install.XXXXXX")" || die "could not write in $INSTALL_DIR"
  trap 'rm -rf "$tmp" "$staged"' EXIT INT TERM
  cp "$tmp/bui" "$staged" && chmod 755 "$staged" && mv -f "$staged" "$INSTALL_DIR/bui"     || die "could not install into $INSTALL_DIR: the old bui (if any) is as it was"
  ok "Installed $INSTALL_DIR/bui"

  add_to_path
  say ""
  say "${BOLD}${GREEN}bui is ready.${RESET} Open a new terminal (or run ${BOLD}export PATH=\"$INSTALL_DIR:\$PATH\"${RESET}), then:"
  say ""
  say "  ${ORANGE}bui new my-app${RESET}   ${DIM}make an app${RESET}"
  say "  ${ORANGE}bui doctor${RESET}       ${DIM}check your setup${RESET}"
  say "  ${ORANGE}bui update${RESET}       ${DIM}get newer versions later${RESET}"
  say ""
}

main "$@"
