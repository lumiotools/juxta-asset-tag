import subprocess
import sys
from pathlib import Path
import serial.tools.list_ports
import time
import re
from datetime import datetime
import pygsheets
# ================= USER CONFIG =================

# Use script's location to build absolute path to build directory
SCRIPT_DIR = Path(__file__).parent
BUILD_DIR = (SCRIPT_DIR).resolve()   # Resolves to absolute path

# Auto-detect the esp32 build subdirectory (e.g., esp32.esp32.esp32c6)
# def find_build_dir():
#     if not BUILD_ROOT.exists():
#         return None
    
#     # Look for esp32.* subdirectories
#     esp32_dirs = list(BUILD_ROOT.glob("esp32.*"))
#     if esp32_dirs:
#         return esp32_dirs[0]  # Use the first match
    
#     # Fall back to BUILD_ROOT if no subdirectory found
#     return BUILD_ROOT

# BUILD_DIR = find_build_dir()

CHIP = "esp32c6"
BAUD = "921600"

FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "4MB"

# ===============================================
EXCEL_FILE = "production_log.xlsx"
gc = pygsheets.authorize(service_file="credentials.json")
sh = gc.open('Production_PCB')
wks = sh[0]

def get_device_mac(port: str):
    # Use only 'read-mac' and require BASE MAC to be present.
    cmd = ["--chip", CHIP, "--port", port, "read-mac"]
    out = run_esptool(cmd, return_output=True)
    if not out:
        return None
    m_base = re.search(r"BASE\s+MAC:\s*([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})", out, re.IGNORECASE)
    if m_base:
        return m_base.group(1).upper()
    # If BASE MAC not present, abort (no fallback)
    return None

def log_to_sheets(mac, status):
    timestamp = datetime.now().strftime("%d-%m-%Y %H:%M:%S")  # Include time
    try:
        wks.append_table(values=[timestamp, mac, status])
        print(f"saved to sheets: {mac}")
    except Exception as e:
        print(f"failed to save: {e}")
        
def monitor_multiple_esps():
    seen_ports = set()
    print("Monitoring for ESP32-C6 units. Plug them in one by one...")

    while True:
        current_ports = {p.device: p.hwid for p in serial.tools.list_ports.comports()}
        
        for device, hwid in current_ports.items():
            if "303A" in hwid.upper() and device not in seen_ports:
                print(f"\nNew ESP32-C6 detected: {device}")
                seen_ports.add(device)
                
                # Try merged binary first, then split binaries
                success = flash_merged(device)
                if success is False:  # File not found
                    print("\n⚠️  merged.bin not found, trying split binaries")
                    success = flash_split(device)
                
                # status = "Flashed" if success else "Failed"
                
                # mac = get_device_mac(device)
                # mac12 = mac.replace(":", "").upper()  # Remove colons from MAC
                # log_to_sheets(mac12, status)
                # print(f"Saved to sheets: MAC {mac12} | Status {status}")

        disconnected = seen_ports - set(current_ports.keys())
        for device in disconnected:
            print(f"ESP32-C6 removed: {device}. Ready for next device.")
            seen_ports.remove(device)

        time.sleep(0.5)


def run_esptool(args, retry_on_fail=True, return_output=False):
    cmd = [sys.executable, "-m", "esptool"] + args
    # User-facing note
    if any(k in args for k in ("erase_flash", "erase-flash", "read_mac", "read-mac", "get_mac", "get-mac")):
        print("\n🚀 Running esptool command...")
    else:
        print("\n🚀 Flashing with esptool...")

    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if return_output:
            # Return combined stdout+stderr for robust parsing
            return (result.stdout or "") + "\n" + (result.stderr or "")
        else:
            print(result.stdout)
        # Default success message for flashing ops
        print("✅ Flash completed.")
        return True
    except subprocess.CalledProcessError as e:
        # When caller asked for output, return None so caller can try alternatives
        if return_output:
            # Return combined output if present
            out = (e.stdout or "") + "\n" + (e.stderr or "")
            return out if out.strip() else None
        print("\n❌ esptool reported an error. Please check the USB cable and COM port, and try again.")
        return False
    except Exception as e:
        if return_output:
            return None
        print(f"\n❌ Unexpected error during esptool operation: {e}")
        return False



def flash_merged(port):
    merged = next(BUILD_DIR.glob("*.merged.bin"), None)
    if not merged:
        return False

    print(f"\n✅ Found merged image: {merged.name}")

    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "write-flash",
        "--flash-mode", FLASH_MODE,
        "--flash-freq", FLASH_FREQ,
        "--flash-size", FLASH_SIZE,
        "0x0000", str(merged),
    ])
    return success


def flash_split(port):
    files = {
        "0x0000": "*.bootloader.bin",
        "0x8000": "*.partitions.bin",
        "0xe000": "boot_app0.bin",
        "0x10000": "*.ino.bin",
    }

    flash_args = []

    for addr, pattern in files.items():
        matches = list(BUILD_DIR.glob(pattern))
        if not matches:
            print(f"❌ Missing file: {pattern}")
            return False
        flash_args.extend([addr, str(matches[0])])

    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
        *flash_args,
    ])
    return success


def main():
    print("\nESP32-C6 Firmware Flash Tool")
    print("-----------------------------")

    if BUILD_DIR is None or not BUILD_DIR.exists():
        print(f"❌ Build directory not found: {BUILD_DIR}")
        sys.exit(1)
    
    print(f"📁 Using build directory: {BUILD_DIR}")


    monitor_multiple_esps()


if __name__ == "__main__":
    main()
