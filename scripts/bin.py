import subprocess
import serial.tools.list_ports
import sys
from pathlib import Path

# ===================== USER CONFIG =====================

BIN_DIR = Path("./bin")

BIN_FILES = {
    "0x0000":  "bootloader.bin",
    "0x8000":  "partition-table.bin",
    "0xe000":  "ota_data_initial.bin",
    "0x10000": "firmware.bin",
}

CHIP = "esp32c6"
FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "4MB"
BAUD_RATE = "921600"

# ======================================================


def find_ports():
    return list(serial.tools.list_ports.comports())


def choose_port(ports):
    print("\nDetected COM ports:")
    for i, p in enumerate(ports):
        print(f"[{i}] {p.device} - {p.description}")

    if len(ports) == 1:
        print(f"\n✅ Auto-selected: {ports[0].device}")
        return ports[0].device

    idx = input("\nSelect port number: ")
    return ports[int(idx)].device


def check_bins():
    for addr, name in BIN_FILES.items():
        path = BIN_DIR / name
        if not path.exists():
            print(f"❌ Missing file: {path}")
            sys.exit(1)


def flash(port):
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD_RATE,
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
    ]

    for addr, name in BIN_FILES.items():
        cmd.extend([addr, str(BIN_DIR / name)])

    print("\n🚀 Flash command:")
    print(" ".join(cmd))

    subprocess.run(cmd, check=True)


def main():
    print("\nESP32-C6 Flash Tool (Minimal SPIFFS + OTA)")
    print("----------------------------------------")

    check_bins()

    ports = find_ports()
    if not ports:
        print("❌ No COM ports found")
        sys.exit(1)

    port = choose_port(ports)

    confirm = input(f"\nFlash device on {port}? (y/N): ").lower()
    if confirm != "y":
        print("❌ Cancelled")
        sys.exit(0)

    flash(port)
    print("\n✅ Flash completed successfully")


if __name__ == "__main__":
    main()