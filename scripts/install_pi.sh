#!/usr/bin/env bash
# install_pi.sh — Bootstrap MadMapperControl on a Raspberry Pi
# Usage:  bash install_pi.sh [--skip-of] [--skip-web] [--skip-build]
set -euo pipefail

# ── colour helpers ──────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()      { echo -e "${GREEN}[ OK ]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
section() { echo -e "\n${BOLD}━━━  $*  ━━━${NC}"; }
die()     { echo -e "${RED}[ERR]${NC}  $*" >&2; exit 1; }

# ── argument flags ──────────────────────────────────────────────────
SKIP_OF=false; SKIP_WEB=false; SKIP_BUILD=false
for arg in "$@"; do
  case $arg in
    --skip-of)    SKIP_OF=true    ;;
    --skip-web)   SKIP_WEB=true   ;;
    --skip-build) SKIP_BUILD=true ;;
    -h|--help)
      echo "Usage: bash install_pi.sh [--skip-of] [--skip-web] [--skip-build]"
      exit 0 ;;
    *) die "Unknown argument: $arg" ;;
  esac
done

# ── config ──────────────────────────────────────────────────────────
OF_VERSION="0.12.0"
OF_INSTALL_DIR="$HOME/openFrameworks"
ADDONS_DIR="$OF_INSTALL_DIR/addons"
APPS_DIR="$OF_INSTALL_DIR/apps/myApps"
PROJECT_DIR="$APPS_DIR/MadMapperControl_MM6_V2"
PROJECT_BRANCH="multiServer"
NODE_MAJOR="20"

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
  aarch64) OF_ARCH="linuxaarch64" ;;
  armv7l)  OF_ARCH="linuxarmv7l"  ;;
  x86_64)  OF_ARCH="linux64"      ;;
  *)       die "Unsupported architecture: $ARCH" ;;
esac

OF_TARBALL="of_v${OF_VERSION}_${OF_ARCH}_release.tar.gz"
OF_URL="https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/${OF_TARBALL}"

# ── sanity checks ───────────────────────────────────────────────────
[[ $EUID -eq 0 ]] && die "Do not run as root. The script will sudo when needed."
command -v git  >/dev/null || die "git is not installed (run: sudo apt install git)"

# ════════════════════════════════════════════════════════════════════
section "1 · System dependencies"
# ════════════════════════════════════════════════════════════════════
info "Updating package lists…"
sudo apt-get update -qq

PKGS=(
  build-essential
  libpoco-dev
  libasound2-dev
  librtmidi-dev
  libusb-1.0-0-dev
  libavcodec-dev libavformat-dev libswscale-dev
  libboost-filesystem-dev libboost-system-dev
  curl wget
)
info "Installing packages: ${PKGS[*]}"
sudo apt-get install -y "${PKGS[@]}"
ok "System packages ready"

# ════════════════════════════════════════════════════════════════════
section "2 · openFrameworks"
# ════════════════════════════════════════════════════════════════════
OF_LIB_DIR="$OF_INSTALL_DIR/libs/openFrameworksCompiled/lib"
OF_COMPILED=false
[[ -n "$(ls -A "$OF_LIB_DIR" 2>/dev/null)" ]] && OF_COMPILED=true

if $SKIP_OF; then
  warn "--skip-of set — skipping OF download & compile"
elif $OF_COMPILED; then
  ok "openFrameworks already compiled at $OF_INSTALL_DIR — skipping"
else
  # Source present but not compiled, or not present at all
  if [[ ! -d "$OF_INSTALL_DIR/libs/openFrameworks" ]]; then
    info "Downloading OF ${OF_VERSION} for ${OF_ARCH}…"
    cd "$HOME"
    wget -q --show-progress "$OF_URL" -O "$OF_TARBALL"
    info "Extracting…"
    tar -xzf "$OF_TARBALL"
    mv "of_v${OF_VERSION}_${OF_ARCH}_release" "$OF_INSTALL_DIR"
    rm "$OF_TARBALL"
    ok "Extracted to $OF_INSTALL_DIR"
  else
    ok "openFrameworks source found — skipping download, compiling…"
  fi

  info "Installing OF system dependencies…"
  sudo "$OF_INSTALL_DIR/scripts/linux/debian/install_dependencies.sh"

  info "Compiling openFrameworks (this takes 20–40 min on Pi 5)…"
  make -C "$OF_INSTALL_DIR/libs/openFrameworksCompiled/project" \
       Release -j"$(nproc)" --no-print-directory
  ok "openFrameworks compiled"
fi

# ════════════════════════════════════════════════════════════════════
section "3 · Addons"
# ════════════════════════════════════════════════════════════════════
clone_or_update() {
  local url="$1" dir="$2" branch="${3:-}"
  if [[ -d "$dir/.git" ]]; then
    info "$(basename "$dir") already exists — updating"
    git -C "$dir" fetch --all
    if [[ -n "$branch" ]]; then
      git -C "$dir" checkout "$branch" 2>/dev/null || git -C "$dir" checkout -b "$branch" "origin/$branch"
    fi
    git -C "$dir" pull --ff-only || warn "Could not fast-forward $dir (local changes?)"
  else
    info "Cloning $(basename "$url")${branch:+ (branch: $branch)}…"
    if [[ -n "$branch" ]]; then
      git clone --depth=1 --branch "$branch" "$url" "$dir"
    else
      git clone --depth=1 "$url" "$dir"
    fi
  fi
}

mkdir -p "$ADDONS_DIR"
clone_or_update "https://github.com/jonasfehr/ofxMadOscQuery.git" "$ADDONS_DIR/ofxMadOscQuery" "multiServer"
clone_or_update "https://github.com/jonasfehr/ofxMidiDevice.git"   "$ADDONS_DIR/ofxMidiDevice"   "faderport16"
clone_or_update "https://github.com/d3cod3/ofxMidi.git"            "$ADDONS_DIR/ofxMidi"
ok "Addons ready"

# Verify bundled addons exist
for addon in ofxGui ofxOsc; do
  [[ -d "$ADDONS_DIR/$addon" ]] || die "$addon not found in $ADDONS_DIR — OF installation may be incomplete"
done

# ════════════════════════════════════════════════════════════════════
section "4 · Project"
# ════════════════════════════════════════════════════════════════════
mkdir -p "$APPS_DIR"
clone_or_update "https://github.com/jonasfehr/MadMapperControl.git" "$PROJECT_DIR"

info "Checking out branch: $PROJECT_BRANCH"
git -C "$PROJECT_DIR" fetch --all
git -C "$PROJECT_DIR" checkout "$PROJECT_BRANCH"
git -C "$PROJECT_DIR" pull --ff-only || warn "Could not fast-forward — may have local changes"
ok "Project at $PROJECT_DIR on branch $PROJECT_BRANCH"

# ════════════════════════════════════════════════════════════════════
section "5 · Node.js + web UI"
# ════════════════════════════════════════════════════════════════════
if $SKIP_WEB; then
  warn "--skip-web set — skipping web build"
else
  if ! command -v node >/dev/null || [[ $(node --version | cut -d. -f1 | tr -d 'v') -lt 18 ]]; then
    info "Installing Node.js ${NODE_MAJOR} LTS…"
    curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" | sudo -E bash -
    sudo apt-get install -y nodejs
  else
    ok "Node.js already installed: $(node --version)"
  fi

  info "Building web UI…"
  cd "$PROJECT_DIR/web"
  npm install
  npm run build
  ok "Web assets built → bin/data/web/"
fi

# ════════════════════════════════════════════════════════════════════
section "6 · Build C++ app"
# ════════════════════════════════════════════════════════════════════
if $SKIP_BUILD; then
  warn "--skip-build set — skipping C++ compile"
else
  info "Compiling (using $(nproc) cores)…"
  make -C "$PROJECT_DIR" Release -j"$(nproc)"
  ok "Binary: $PROJECT_DIR/bin/MadMapperControl_MM6_V2"
fi

# ════════════════════════════════════════════════════════════════════
section "7 · Optional — systemd service"
# ════════════════════════════════════════════════════════════════════
SERVICE_FILE="/etc/systemd/system/mmcontrol.service"
CURRENT_USER="$(whoami)"

if [[ -f "$SERVICE_FILE" ]]; then
  ok "systemd service already installed — skipping"
else
  read -r -p "Install as a systemd service (start on boot)? [y/N] " yn
  if [[ "${yn,,}" == "y" ]]; then
    sudo tee "$SERVICE_FILE" > /dev/null <<EOF
[Unit]
Description=MadMapperControl MM6
After=network.target sound.target

[Service]
ExecStart=${PROJECT_DIR}/bin/MadMapperControl_MM6_V2
WorkingDirectory=${PROJECT_DIR}
Restart=on-failure
RestartSec=5
User=${CURRENT_USER}

[Install]
WantedBy=multi-user.target
EOF
    sudo systemctl daemon-reload
    sudo systemctl enable mmcontrol
    ok "Service installed and enabled"
    info "Control with: sudo systemctl start|stop|status mmcontrol"
  else
    info "Skipped service install"
  fi
fi

# ════════════════════════════════════════════════════════════════════
section "Done"
# ════════════════════════════════════════════════════════════════════
PI_IP=$(hostname -I | awk '{print $1}')
echo -e "
${GREEN}${BOLD}Installation complete.${NC}

  Run manually:
    ${CYAN}cd ${PROJECT_DIR}${NC}
    ${CYAN}./bin/MadMapperControl_MM6_V2${NC}

  Then open:
    ${CYAN}http://${PI_IP}:8080${NC}  (from this Pi)
    ${CYAN}http://${PI_IP}:8080${NC}  (from another device on the network)
"
