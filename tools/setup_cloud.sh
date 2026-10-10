#!/bin/sh
# Debian cloud verification tools, installed locally without root access.
set -eu
cd "$(dirname "$0")/.."
command -v gcc >/dev/null
command -v python3 >/dev/null
command -v dpkg-deb >/dev/null
tooling=/workspace/lpc1768-tooling
mkdir -p "$tooling/apt/lists/partial" "$tooling/apt/cache/archives/partial" \
         "$tooling/apt/conf.d" "$tooling/toolchain"
if [ ! -x "$tooling/toolchain/usr/bin/arm-none-eabi-gcc" ]; then
    cat > "$tooling/apt/sources.list" <<'EOF'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
EOF
    cat > "$tooling/apt/apt.conf" <<'EOF'
Dir::Etc::parts "/workspace/lpc1768-tooling/apt/conf.d";
Dir::Etc::main "/dev/null";
Dir::Etc::sourcelist "/workspace/lpc1768-tooling/apt/sources.list";
Dir::Etc::sourceparts "/workspace/lpc1768-tooling/apt/conf.d";
Dir::State::lists "/workspace/lpc1768-tooling/apt/lists";
Dir::Cache "/workspace/lpc1768-tooling/apt/cache";
APT::Get::List-Cleanup "false";
APT::Update::Error-Mode "any";
Acquire::Retries "2";
#clear APT::Update::Post-Invoke;
#clear APT::Update::Post-Invoke-Success;
#clear DPkg::Post-Invoke;
EOF
    (
        cd "$tooling"
        export APT_CONFIG="$tooling/apt/apt.conf"
        /usr/bin/apt-get update
        /usr/bin/apt-get download gcc-arm-none-eabi binutils-arm-none-eabi
        for package in gcc-arm-none-eabi_*.deb binutils-arm-none-eabi_*.deb; do
            dpkg-deb -x "$package" "$tooling/toolchain"
        done
    )
fi
# Pinned vendor headers, fetched over verified HTTPS and checked before use.
# LPC device definitions originate from NXP; core/compiler support from ARM.
python3 - <<'PY'
import hashlib
from pathlib import Path
import urllib.request

root = Path('/workspace/lpc1768-tooling')
base = ('https://raw.githubusercontent.com/ARMmbed/mbed-os/'
        'd723bf9e55415433e108124ee6d36337feddf1b8/')
device = 'targets/TARGET_NXP/TARGET_LPC176X/device/'
core = 'cmsis/CMSIS_5/CMSIS/TARGET_CORTEX_M/Include/'
files = {
    'LPC17xx.h': (device, 'ef173e191d3e57c628edc7f64e5e6add3a94db8db965d811a8fd97556a0cbc0b'),
    'system_LPC17xx.h': (device, 'bbd171d52874e32c51fc10c1113f993cf7628e2fda0d67ea33b3711673627da3'),
    'core_cm3.h': (core, 'c9937781259a8897adf514445441c68570087d6a72a558d8a4afe75cdb62dbd2'),
    'cmsis_version.h': (core, '0ad241efa3091a4a4dd9eb52a63611b4510e0a62d9b59ec439ff41c0d54bbb3a'),
    'cmsis_compiler.h': (core, 'b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8'),
    'cmsis_gcc.h': (core, '43bfd1fe69fbbc2ca70aaf7d43cc3e20f16e0c7f66e1bdef9b51af6dc14b2617'),
    'mpu_armv7.h': (core, '29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372'),
}
for name, (directory, expected) in files.items():
    path = root / name
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == expected:
        continue
    with urllib.request.urlopen(base + directory + name, timeout=30) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit('Vendor header checksum mismatch: ' + name)
    path.write_bytes(data)
print('Pinned CMSIS headers verified.')
PY
sh tools/test.sh
sh tools/cross_check.sh
