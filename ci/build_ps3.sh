#!/usr/bin/env bash
# Build spookyflow.pkg with a PSL1GHT toolchain.
#
# Used by .github/workflows/build.yml in two ways:
#   1) primary: on the Ubuntu runner, after the prebuilt toolchain tarball
#      (bucanero/ps3toolchain release, the one apollo-ps3's CI uses) has been
#      unpacked to $PS3DEV.
#   2) fallback: inside the flipacholas/ps3devextra Docker image (Alpine,
#      PSL1GHT + Tiny3D preinstalled, last updated ~2018).
# It locates the SDK, makes sure Tiny3D is available (building it from
# source if not), then runs `make pkg`.
set -euo pipefail

say() { echo "::group::$*" 2>/dev/null || true; echo "== $*"; }
end() { echo "::endgroup::" 2>/dev/null || true; }

# --- locate the SDK -----------------------------------------------------------
if [ -z "${PS3DEV:-}" ]; then
  for d in /usr/local/ps3dev /opt/ps3dev /ps3dev "$HOME/ps3dev"; do
    if [ -f "$d/ppu_rules" ] || [ -f "$d/psl1ght/ppu_rules" ]; then export PS3DEV="$d"; break; fi
  done
fi
if [ -z "${PSL1GHT:-}" ]; then
  if [ -f "$PS3DEV/ppu_rules" ]; then export PSL1GHT="$PS3DEV"
  elif [ -f "$PS3DEV/psl1ght/ppu_rules" ]; then export PSL1GHT="$PS3DEV/psl1ght"
  fi
fi
if [ -z "${PS3DEV:-}" ] || [ -z "${PSL1GHT:-}" ] || [ ! -f "$PSL1GHT/ppu_rules" ]; then
  echo "ERROR: could not find PSL1GHT (ppu_rules). PS3DEV='${PS3DEV:-}' PSL1GHT='${PSL1GHT:-}'"
  find / -name ppu_rules -not -path '*/proc/*' 2>/dev/null | head -5 || true
  exit 1
fi
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
echo "PS3DEV=$PS3DEV"
echo "PSL1GHT=$PSL1GHT"
command -v ppu-gcc >/dev/null && ppu-gcc --version | head -1 || echo "warning: ppu-gcc not on PATH"

# --- Tiny3D -------------------------------------------------------------------
say "Checking for Tiny3D"
if find "$PS3DEV" -name 'libtiny3d.a' 2>/dev/null | grep -q .; then
  echo "Tiny3D found: $(find "$PS3DEV" -name 'libtiny3d.a' | head -1)"
else
  echo "Tiny3D not found; building wargio/tiny3D from source"
  # UNVERIFIED: tiny3D/lib's 'install' target copies libtiny3d.a and tiny3d.h
  # into $PS3DEV/portlibs/ppu (where -L$(PORTLIBS)/lib finds it).
  rm -rf /tmp/tiny3D
  git clone --depth 1 https://github.com/wargio/tiny3D /tmp/tiny3D
  make -C /tmp/tiny3D/lib
  make -C /tmp/tiny3D/lib install
fi
end

# --- build ----------------------------------------------------------------------
say "make pkg"
make clean >/dev/null 2>&1 || true
make pkg
end
ls -la ./*.pkg
