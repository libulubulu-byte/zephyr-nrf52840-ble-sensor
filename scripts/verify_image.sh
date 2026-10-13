#!/usr/bin/env bash
B=/home/lizhen/build/env_node
SDK=/home/lizhen/zephyr-sdk-0.16.8

echo '=== artifacts copied to the workspace ==='
ls -l /mnt/d/ESP32-IDF/ESP32S3_Zephyr/firmware/

echo
echo '=== ELF load addresses (must start at 0x00026000) ==='
"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf" -l "$B/zephyr/zephyr.elf" \
    | grep -A1 'LOAD' | grep -E 'LOAD|0x00' | head -12

echo
echo '=== entry point ==='
"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf" -h "$B/zephyr/zephyr.elf" | grep -i entry

echo
echo '=== UF2 family id (expect 0xada52840) ==='
python3 - "$B/zephyr/zephyr.uf2" <<'EOF'
import struct, sys
p = sys.argv[1]
with open(p, 'rb') as f:
    head = f.read(32)
magic0, magic1, flags, target, payload, blkno, nblk, famid = struct.unpack('<8I', head)
print(f"  magic  : 0x{magic0:08x} 0x{magic1:08x}  (expect 0x0a324655 0x9e5d5157)")
print(f"  flags  : 0x{flags:08x}")
print(f"  target : 0x{target:08x}   (flash address of this block)")
print(f"  family : 0x{famid:08x}   (expect 0xada52840 = NRF52840 / Adafruit)")
print(f"  blocks : {nblk}")
import os
print(f"  size   : {os.path.getsize(p)} bytes")
EOF

echo
echo '=== partitions in the generated devicetree ==='
python3 - "$B/zephyr/zephyr.dts" <<'EOF'
import re, sys
t = open(sys.argv[1]).read()
for m in re.finditer(r'(code_partition|storage_partition):\s*partition@([0-9a-f]+)\s*\{(.*?)\};', t, re.S):
    name, off, body = m.group(1), m.group(2), m.group(3)
    size = re.search(r'reg\s*=\s*<0x[0-9a-f]+\s+(0x[0-9a-f]+)>', body)
    print(f"  {name:18s} offset 0x{off:>8s}  size {size.group(1) if size else '?'}")
EOF

echo
echo '=== warnings left in the build log ==='
grep -c 'warning:' /home/lizhen/build.log
grep 'warning:' /home/lizhen/build.log | head -8
