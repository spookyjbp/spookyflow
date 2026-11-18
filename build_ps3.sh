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

# --- mbedTLS (0.9.3: HTTPS for Download Missing Covers) ---------------------------
# Optional: built from the official mbedTLS 2.28 release with a small config
# (a TLS 1.2 client, no OS services). If anything here fails, spookyflow is
# built without HTTPS (plain-HTTP cover sources still work).
build_tls() {
  local ver=2.28.10 portlibs="$PS3DEV/portlibs/ppu" work=/tmp/sf_mbedtls
  rm -rf "$work" && mkdir -p "$work/obj" || return 1
  local url="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$ver/mbedtls-$ver.tar.bz2"
  if command -v curl >/dev/null; then curl -sSL "$url" -o "$work/m.tar.bz2" || return 1
  else wget -q "$url" -O "$work/m.tar.bz2" || return 1; fi
  tar xjf "$work/m.tar.bz2" -C "$work" || return 1
  cat > "$work/sf_mbedtls_config.h" <<'CFG'
/* spookyflow: a small TLS 1.2 client (cover downloads). No OS services:
 * no file system, no clock, no platform entropy (the app adds its own). */
#ifndef SF_MBEDTLS_CONFIG_H
#define SF_MBEDTLS_CONFIG_H
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384
#include "mbedtls/check_config.h"
#endif
CFG
  local f
  for f in "$work/mbedtls-$ver"/library/*.c; do
    ppu-gcc -O2 -mcpu=cell -c "$f" -I"$work/mbedtls-$ver/include" -I"$work"       -DMBEDTLS_CONFIG_FILE='"sf_mbedtls_config.h"' -o "$work/obj/$(basename "$f" .c).o" || return 1
  done
  mkdir -p "$portlibs/lib" "$portlibs/include"
  ppu-ar rcs "$portlibs/lib/libsfmbedtls.a" "$work"/obj/*.o || return 1
  cp -R "$work/mbedtls-$ver/include/mbedtls" "$portlibs/include/" || return 1
  cp "$work/sf_mbedtls_config.h" "$portlibs/include/" || return 1
  echo "mbedTLS $ver built: $portlibs/lib/libsfmbedtls.a"
}
say "mbedTLS (HTTPS)"
TLS=0
if build_tls; then TLS=1; else echo "warning: mbedTLS could not be built; spookyflow is built without HTTPS"; fi
end

# --- build ----------------------------------------------------------------------
say "make pkg (SF_TLS=$TLS)"
make clean >/dev/null 2>&1 || true
if ! make pkg SF_TLS=$TLS; then
  if [ "$TLS" = 1 ]; then
    echo "warning: the build with HTTPS failed (above); building without it"
    make clean >/dev/null 2>&1 || true
    make pkg SF_TLS=0
  else
    exit 1
  fi
fi
end
ls -la ./*.pkg
