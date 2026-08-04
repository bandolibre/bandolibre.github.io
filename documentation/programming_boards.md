
# Connecting the debugger probe

Plug the ST-Link into a USB port and verify the device is detected:

```
$ lsusb | grep STLINK
Bus 001 Device 070: ID 0483:3754 STMicroelectronics STLINK-V3
```

Install [stlink-tools](https://github.com/stlink-org/stlink) from source, in a
directory next to your `bandoneo` checkout:

```
sudo apt remove stlink-tools
sudo apt install build-essential cmake libusb-1.0-0-dev
git clone --depth 1 --branch testing https://github.com/stlink-org/stlink
cd stlink
git apply ../bandoneo/documentation/0001-fix-st-trace-fix-SWO-trace-on-STLINK-V3-HS-bulk-endp.patch
make release && sudo make install && sudo ldconfig
```

Once the ST-Link is plugged into the board:

```
$ st-info --probe
Found 1 stlink programmers
  version:    V3J15
  serial:     002F00413235510637333439
  flash:      131072 (pagesize: 2048)
  sram:       131072
  chipid:     0x469
  dev-type:   STM32G47x_G48x
```

# Install build prerequisites

The ARM toolchain and the build backend:

```
sudo apt install gcc-arm-none-eabi ninja-build
```

I use [`just`](https://just.systems/) to run commands from a configuration file
such as `code/main-g474/justfile`:

```
curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to DEST
```

or

```
cargo install just
```

# Build the firmware

Generate the build system once, then build:

```
cd code/main-g474
just init_build
just build
```

Re-run `just init_build` after adding or removing source files, or after
changing `CMakeLists.txt`.

# Flash the firmware

Flashing wing firmware onto a main board — or the reverse — drives pins against
the connected hardware and can damage the boards, so a chip must be registered
before it can be flashed. `code/tool/boards.csv` maps each STM32 unique device
ID to its board type, and is checked before every flash.

Register a board once, before its first flash:

```
cd code/main-g474
just registry_add
```

Then flash with:

```
cd code/main-g474
just flash
```

`just flash` programs every connected ST-Link whose chip is registered as that
board type, so both wings can be flashed in a single command.

The same recipes apply in `code/wing-g474`.


# Read UART debug console

Characters written with `printf` (via `_write` retargeted to `HAL_UART_Transmit`) are sent over USART to the STLink VCP bridge, which forwards them to a host `/dev/ttyACMx` device.

Use `tio` to read it — it auto-reconnects across resets, unlike `screen` or `cat`:

```bash
tio /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*
```

The glob resolves to the correct `/dev/ttyACMx` regardless of what other USB serial devices are present. Or via the justfile recipe:

```bash
just console
```

Press `ctrl-t q` to quit.


