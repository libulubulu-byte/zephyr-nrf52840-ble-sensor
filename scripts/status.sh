#!/usr/bin/env bash
# Quick environment / download status report.
TC=/home/lizhen/toolchain_linux-x86_64_arm-zephyr-eabi.tar.xz

echo '=== toolchain ==='
if [ -f "$TC" ]; then
    echo "  downloaded: $(stat -c %s "$TC") bytes  (~98000000 expected)"
else
    echo "  archive absent"
fi
echo "  installed : $([ -d /home/lizhen/zephyr-sdk-0.16.8/arm-zephyr-eabi ] && echo yes || echo no)"

echo
echo '=== NCS source tree ==='
ls -d /home/lizhen/ncs/*/ 2>/dev/null | sed 's|/home/lizhen/ncs/||'
du -sh /home/lizhen/ncs 2>/dev/null

echo
echo '=== job states ==='
for j in 09_sdk_toolchain 06_fetch_sources 07_fetch_ncs; do
    if pgrep -f "$j" >/dev/null 2>&1; then echo "  $j RUNNING"; else echo "  $j stopped"; fi
done

echo
echo '=== tarball cache ==='
ls -lh /home/lizhen/tarballs/ 2>/dev/null | tail -8

echo
echo '=== disk ==='
df -h / | tail -1
