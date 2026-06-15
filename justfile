default:
    @just --list

# Monitor the Bandoneo MIDI port, reconnecting across reboots
midimon:
    tools/midimon.sh

# Open UART debug console via STLink VCP (resolves the right /dev/ttyACMx automatically)
console:
    tio -b 921600 /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*
