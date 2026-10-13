# Toolchain setup (as actually performed)

Written down because the network on this machine is unusual and the workarounds
are not obvious. A machine with normal internet access can skip straight to
"Standard path" at the bottom.

## Host

| Item | Value |
| --- | --- |
| Host OS | Windows 11 |
| Build environment | WSL2, Ubuntu 22.04.5 LTS, on `D:\wsl\Ubuntu-22.04` (the WSL disk is on D:, **not** C:) |
| Python | 3.10.12, inside a venv at `~/ncs-venv` |
| CMake | 3.29.6 (pip). Deliberately held below 4.x, which drops compatibility with `cmake_minimum_required(VERSION < 3.5)` projects |
| West | 1.5.0 (pip) |
| Toolchain | Zephyr SDK 0.16.8, `arm-zephyr-eabi` only |
| NCS | v2.7.0 → Zephyr `v3.6.99-ncs2` (SDK 0.16.5 minimum, per `zephyr/SDK_VERSION`) |

## 1. System packages: `apt` turned out to be unnecessary

Zephyr's getting-started guide asks for a long list of Debian packages. On this
machine **none of it was needed**, and that is worth recording because it saves
a `sudo` round trip and ~200 MB of downloads:

| Tool | Where it actually came from |
| --- | --- |
| `dtc` (devicetree compiler) | bundled in the Zephyr SDK's host tools — `~/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/dtc`, version 1.6.0. `build.sh` prepends that directory to `PATH`. |
| `gperf` | **optional.** `zephyr/cmake/modules/FindHostTools.cmake` says so verbatim: `# gperf is an optional dependency` / `find_program(GPERF gperf)`. It is only reached through `gen_kobject_list.py` for `CONFIG_USERSPACE`, which this application does not use. |
| `cmake`, `ninja` | pip, into the venv |
| host `gcc` / `make` | not needed — nothing host-native is built |
| `unzip` | not needed — sources come as `.tar.gz` and the SDK as `.tar.xz` |

If you *do* want the apt path (a fresh machine, or you add userspace support
later), `scripts/00_apt_deps.sh` is there:

```bash
bash scripts/00_apt_deps.sh      # needs sudo
```

## 2. Python environment (no sudo)

Ubuntu 22.04's `python3-pip` is not always present; the venv bootstraps pip by
itself, which is why it is created before anything else:

```bash
python3 -m venv ~/ncs-venv
source ~/ncs-venv/bin/activate
pip install west cmake ninja
pip install "cmake==3.29.6"
bash scripts/10_python_deps.sh          # zephyr/scripts/requirements.txt
```

## 3. The network problem, and what was done about it

Measured facts, all through the only available route (a local proxy at
`127.0.0.1:7892`; there is **no direct internet at all** on this machine):

| Endpoint | Measured throughput | Behaviour |
| --- | --- | --- |
| `gitee.com` (domestic) | **12.5 MB/s** | stable — but only mirrors upstream Zephyr, not NCS |
| `ghfast.top` GitHub accelerator, `git clone` | ~590 KB/s | stable |
| `github.com` via proxy, `git clone` | 10–270 KB/s | **drops mid-transfer** (`fetch-pack: unexpected disconnect while reading sideband packet`, `invalid index-pack output`) |
| `codeload.github.com` via proxy | ~50 KB/s | slow but completes |

Consequences and mitigations:

1. **`git clone` is not usable for the big repositories here.** A failed clone
   restarts from zero, and the `sdk-nrf` manifest repository is 157 MB. Retrying
   is a lottery: three attempts in a row failed at 54 MB, 34 MB and 25 MB.
   `http.version=HTTP/1.1` and `http.lowSpeedLimit=0` reduce but do not remove
   the failure rate.
2. **Source tarballs complete.** `scripts/06_fetch_sources.sh` fetches
   `codeload.github.com` tarballs instead of git clones. The 73 MB `sdk-zephyr`
   tarball succeeded first try where repeated clones did not.
3. **The accelerator exists and works, but not for everything.** `ghfast.top`
   serves `git clone`, `/archive/` tarballs, and release assets. It does **not**
   honour `Range`, so `curl -C -` cannot resume through it — resume only works
   against `github.com` directly. `scripts/09_sdk_toolchain.sh` therefore tries
   the accelerator first and falls back to direct.
4. **Revisions come from the manifest, not from guesswork.** The module commits
   in `06_fetch_sources.sh` (`hal_nordic`, `cmsis`, `picolibc`, …) are copied
   verbatim out of `sdk-zephyr`'s own `west.yml`, so the tree is a genuine NCS
   v2.7.0 tree even though it was not assembled by `west update`.

## 4. Sources

```bash
bash scripts/06_fetch_sources.sh
bash scripts/15_fetch_tinycrypt.sh     # separate: see below
```

Resulting layout (west-shaped, so a real `west` workspace can be dropped on top
of it later):

```
~/ncs/
├── zephyr/                     sdk-zephyr      v3.6.99-ncs2
└── modules/
    ├── hal/nordic/             hal_nordic      ab5cb2e2faeb1edfad7a25286dcb513929ae55da
    ├── hal/cmsis/              cmsis           4b96cbb174678dcd3ca86e11e1f24bc5f8726da0
    ├── lib/picolibc/           picolibc        764ef4e401a8f4c6a86ab723533841f072885a5b
    └── crypto/tinycrypt/       tinycrypt       1012a3ebee18c15ede5efc8332ee2fc37817670f
```

**Why tinycrypt is fetched separately.** Zephyr's Bluetooth host needs
AES-CMAC: `BT_HOST_CRYPTO` does `select TINYCRYPT`. Without the module, Kconfig
sees the symbol pulled up while `ZEPHYR_TINYCRYPT_MODULE` is `n` and the
configure step aborts with

```
warning: TINYCRYPT ... is currently being y-selected by the following symbols:
 - BT_GATT_CACHING ...
error: Aborting due to Kconfig warnings
```

The module is tiny (~300 KB), so it is fetched on its own rather than widening
the main filter.

**Module discovery, and one gotcha.** `ZEPHYR_MODULES` has to be passed to
CMake, not only exported: with it set only in the environment the configure
step silently fell back to Zephyr's default module loading and tinycrypt was
missing from the generated `zephyr_modules.txt`. `build.sh` passes
`-DZEPHYR_MODULES=...` explicitly, and the file is worth checking whenever a
module seems to be ignored:

```bash
cat ~/build/env_node/zephyr_modules.txt
```

`sdk-nrf` (the NCS glue module) and `sdk-nrfxlib` (Nordic's SoftDevice Controller)
are in the script but optional for this application: it uses only Zephyr APIs and
Zephyr's own open-source link layer, so the 500 MB `nrfxlib` download was skipped
on this link. Adding it back and setting `CONFIG_BT_LL_SOFTDEVICE=y` instead of
`CONFIG_BT_LL_SW_SPLIT` is the only difference for a SoftDevice-Controller build.

## 5. Toolchain

```bash
bash scripts/04_sdk_download.sh    # Zephyr SDK 0.16.8, "minimal" package (42 MB, no compilers)
bash scripts/09_sdk_toolchain.sh   # toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz (~94 MB)
```

The minimal SDK contains no compiler. `setup.sh` would download the toolchain
from `github.com` directly, which is exactly the slow path above; `09_sdk_toolchain.sh`
pulls the same asset through the accelerator and unpacks it into the SDK
directory instead. Asset naming was taken from `setup.sh` itself
(`toolchain_${host}_${toolchain}.tar.xz`).

### Host tools (this is where `dtc` comes from)

The same SDK directory ships a self-extracting host-tools bundle. It is what
makes the `apt` step unnecessary:

```bash
cd ~/hosttools
bash ~/zephyr-sdk-0.16.8/zephyr-sdk-x86_64-hosttools-standalone-0.9.sh -y -d ~/hosttools
# -> ~/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/dtc   (DTC 1.6.0)
```

`-y -d <dir>` are the installer's own flags; without them it prompts and
defaults to `/opt/zephyr-sdk/0.9`, which needs root. `build.sh` prepends the
`usr/bin` directory to `PATH` whenever it finds `dtc` there.

## 6. Building

```bash
bash scripts/build.sh
```

This calls CMake directly rather than `west build`:

```bash
export ZEPHYR_BASE=$HOME/ncs/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.16.8
export ZEPHYR_MODULES="$HOME/ncs/modules/hal/nordic;$HOME/ncs/modules/hal/cmsis;$HOME/ncs/modules/lib/picolibc"
cmake -B ~/build/env_node -GNinja -DBOARD=nrf52840_supermini /path/to/zephyr
ninja -C ~/build/env_node
```

Two reasons for not using `west build` here:

* `west build` needs a west workspace, which needs `.west/config` and the
  `sdk-nrf` manifest repository — the 157 MB clone that keeps failing.
* `ZEPHYR_MODULES` is an explicit list, so a half-extracted module cannot
  silently poison the build.

The build directory is kept in the WSL filesystem (`~/build/...`), not under
`/mnt/d`, because crossing the Windows boundary for a few thousand object files
is dramatically slower.

## Standard path

On a machine with normal connectivity, none of the above is needed:

```bash
west init -m https://github.com/nrfconnect/sdk-nrf --mr v2.7.0
cd ncs && west update --narrow -o=--depth=1
west zephyr-export
west sdk install -t arm-zephyr-eabi
cd /path/to/project && west build -b nrf52840_supermini zephyr/
```

## Flashing

Done from Windows, because the UF2 volume and the CDC console belong to the
host. `scripts/flash.ps1` handles every mode the bootloader can be in:

```powershell
powershell -File scripts\flash.ps1              # build + wait + flash + capture the banner
powershell -File scripts\flash.ps1 -NoBuild     # skip the rebuild
```

What it does, in order:

1. looks for the `NICENANO` volume; if absent, sends the 1200 bps touch to the
   application port and waits up to 120 s for the bootloader to appear;
2. **if MSC**: copies `firmware\zephyr.uf2` and waits for the volume to vanish,
   which is the bootloader programming the flash and rebooting;
3. **if only CDC**: builds a DFU package (`adafruit-nrfutil dfu genpkg
   --dev-type 0x0052 --sd-req 0x00`) and flashes over
   `adafruit-nrfutil dfu serial --singlebank`;
4. waits for the application (239A:8029) to re-enumerate and captures 30 s of
   console into `docs/console_boot.log`.

Two things learned the hard way and encoded in that script:

* **`adafruit-nrfutil` prints a Python traceback and still exits 0.** Trusting
  `$LASTEXITCODE` reports a failed flash as a success, so the output text is
  inspected for `Failed to upgrade|Traceback` instead.
* **The 1200 bps touch only works while the Arduino firmware is running.** The
  Zephyr build does not implement it (see §7 of the README), so after the first
  Zephyr flash the board needs a physical double-tap of the reset pad — or the
  `0x03` control-point command, which is the whole reason that command exists.

### Recovery: why the application cannot lock you out

A fair question to ask before flashing anything here, especially a build that
deliberately switches peripherals off, is "if this goes wrong, can I still get
back in?" The answer is yes, and it does not rest on the application behaving.
Three independent guarantees:

**1. The bootloader is in flash the application cannot address.**

```
0x00000000 - 0x00026000   MBR + SoftDevice S140        (not ours)
0x00026000 - 0x000E6000   code_partition               (application)
0x000E6000 - 0x000F4000   storage_partition            (application)
0x000F4000 - 0x00100000   bootloader + settings        <- in NO partition
```

The layout comes from the devicetree, and the arithmetic is the point: the
application's partitions end at `0xF4000`, which is exactly where the
bootloader begins. There is no partition covering the bootloader, so the
application has no address space in which it lives, and nothing in this project
writes to one.

**2. The bootloader runs before the application, on every reset.**

A double-tap of the reset pad is a hardware event. The application is not
executing at that moment and cannot interfere with it: the chip resets, the
bootloader runs out of its own flash, re-initialises USBD and the HFXO itself,
and offers the `NICENANO` volume.

**3. Almost nothing survives a reset.**

On this part a reset clears the peripheral registers. What is deliberately
retained is RAM, `POWER->GPREGRET` and `POWER->RESETREAS` — and the last two are
retained *precisely because* the bootloader reads them to decide why it was
reset. That is why the probe's USB teardown touches only `USBD` and `CLOCK`
registers and deliberately does not meddle with `POWER`: a power tweak has no
business leaving fingerprints in the block the bootloader reads to make its
decision. Nothing here writes `GPREGRET` except the intentional
enter-bootloader command in the product firmware.

**4. The probe cannot write flash at all.**

```
CONFIG_FLASH              not set
CONFIG_FLASH_MAP          not set
CONFIG_FLASH_PAGE_LAYOUT  not set
CONFIG_NVS                not set
CONFIG_SETTINGS           not set
```

No flash driver is compiled in. It is not that the probe is careful about where
it writes — it has no means of writing.

So the recovery procedure is always the same one:

```powershell
# 1. plug USB in (the UF2 volume needs it)
# 2. double-tap RST to GND  -> a NICENANO volume appears
# 3. flash whatever you want to go back to
powershell -File scripts\flash_wait.ps1 -Image firmware\power_test.uf2
powershell -File scripts\flash_wait.ps1                       # or the product
```

If the double-tap does not take, power-cycle the board and vary the interval
between taps (0.2 s / 0.5 s / 1 s all work; the window is what matters). A
power cycle clears `GPREGRET` and `RESETREAS`, so it also resets the condition
the bootloader evaluates.

Other useful scripts:

| Script | Purpose |
| --- | --- |
| `scripts/build.sh` | configure + build + copy artifacts to `firmware/` |
| `scripts/verify_image.sh` | assert the ELF loads at `0x26000` and the UF2 family is `0xADA52840` |
| `scripts/status.sh` | what is downloaded, what is installed, what is running |
| `scripts/capture_console.ps1 [s]` | capture the CDC console with ANSI codes stripped |
| `scripts/gen_dfu_pkg.ps1` | build and inspect a DFU package by hand |
| `scripts/state.ps1` | which 239A device is present, in which mode |
| `scripts/flash_wait.ps1` | wait for a manual double-tap, then flash — the variant to use once the Zephyr build is running |
