# ATF-MTKSOC

This repository IS NOT MTK OFFICIAL repository.

## Quick Start

- Prepare:

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential libssl-dev device-tree-compiler
```

- Create build directory:

```bash
rm -rf build/
mkdir -p build
```

- Configure manually

```bash
cp configs/mt7987_defconfig build/.config
make defconfig
make CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

- Configure with menuconfig

```bash
make menuconfig
make CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```
