#!/usr/bin/env python3
"""test-and-upload.py

Detect a COM port, flash test firmware (from build/device), stream serial logs
until a success or failure is detected, and on success flash final firmware
(from build/firmware).

Usage:
  python test-and-upload.py

Configure behavior by editing the variables near the top of this file (PORT, DEVICE_BUILD, FIRMWARE_BUILD,
SERIAL_BAUD, FLASH_BAUD, TIMEOUT, READY_REGEX, PASS_REGEX, FAIL_REGEX, REGISTER_URL, REGISTER_TIMEOUT).

"""

import re
import subprocess
import sys
import time
from pathlib import Path

import serial
import serial.tools.list_ports
import urllib.request
import urllib.error

from datetime import datetime
import pygsheets

# Default paths relative to this script
SCRIPT_DIR = Path(__file__).parent
DEFAULT_DEVICE_BUILD = (SCRIPT_DIR / '..' / 'build' / 'device').resolve()
DEFAULT_FIRMWARE_BUILD = (SCRIPT_DIR / '..' / 'build' / 'firmware').resolve()

# Flash defaults (match existing upload.py)
CHIP = "esp32c6"
FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "4MB"
DEFAULT_FLASH_BAUD = "921600"

# ======== Configuration (edit these variables as needed) ========
# If PORT is None the script will auto-detect and prompt operator
PORT = None
DEVICE_BUILD = str(DEFAULT_DEVICE_BUILD)
FIRMWARE_BUILD = str(DEFAULT_FIRMWARE_BUILD)
SERIAL_BAUD = 115200
FLASH_BAUD = DEFAULT_FLASH_BAUD
TIMEOUT = 300  # seconds to wait for readiness string
READY_REGEX = r"^UPLOAD FIRMWARE READY$"
PASS_REGEX = r"^OVERALL:\s*PASS$"
FAIL_REGEX = r"^OVERALL:\s*FAIL$"
REGISTER_URL = 'https://juxta-pcb-manufacturing.vercel.app/api/register-device'
REGISTER_TIMEOUT = 6  # seconds
# ================================================================

# ======== Test Types Dictionary ========
# Initialize all expected tests as FAILED (will be updated as logs are received)
EXPECTED_TESTS = {
    "DeviceID": {"status": "UNKNOWN", "reason": ""},
    "NVS": {"status": "UNKNOWN", "reason": ""},
    "GPS": {"status": "UNKNOWN", "reason": ""},
    "GPS Initialization": {"status": "UNKNOWN", "reason": ""},
    "LED": {"status": "UNKNOWN", "reason": ""},
    "Charging Detect": {"status": "UNKNOWN", "reason": ""},
    "Battery ADC": {"status": "UNKNOWN", "reason": ""},
    "SPI Flash": {"status": "UNKNOWN", "reason": ""},
    "IMU": {"status": "UNKNOWN", "reason": ""},
    "IMU INT (GPIO5)": {"status": "UNKNOWN", "reason": ""},
    "BLE Connection": {"status": "UNKNOWN", "reason": ""},
    "Hub Connect": {"status": "UNKNOWN", "reason": ""}
}
# ================================================================

# ===============================================
EXCEL_FILE = "production_log.xlsx"
gc = pygsheets.authorize(service_file="credentials.json")
sh = gc.open('Production_PCB')
wks = sh[1]
def get_esp_mac(port):
    cmd = [sys.executable, "-m", "esptool", "--chip", CHIP, "--port", port, "read-mac"]
    try:
        result = subprocess.check_output(cmd, check=True, capture_output=True, text=True)
        for line in result.splitlines():
            # print(line)
            if "MAC:" in line:
                return line.split("MAC: ")[1].strip().replace(":", "").upper()
    except Exception as e:
        print(f"Error reading MAC: {e}")
    return "UNKNOWN"

def log_to_sheets(mac, status, test_results):
    """Log device test results to Google Sheets.
    
    Columns: Date Time, Device Mac, Status, Test results (DeviceID, NVS, GPS, LED, Charging Detect, 
             Battery ADC, SPI Flash, IMU, IMU INT, BLE Connection, Hub Connect)
    
    Special handling for GPS: if GPS Initialization is PASS, show PASS. Otherwise show GPS status/reason.
    For other tests: show status and reason if available.
    """
    timestamp = datetime.now().strftime("%d-%m-%Y %H:%M:%S")  # Include time
    
    # Special handling for GPS: combine GPS and GPS Initialization
    if test_results.get("GPS Initialization", {}).get("status") == "PASS":
        gps_cell = "PASS"
    else:
        gps_test = test_results.get("GPS", {})
        gps_status = gps_test.get("status", "FAILED")
        gps_reason = gps_test.get("reason", "")
        if gps_reason:
            gps_cell = f"{gps_status}: {gps_reason}"
        else:
            gps_cell = gps_status
    
    # Build row: Date Time, Device Mac, Status, then all test results in order
    # Helper function to format test result with reason
    def format_test_result(test_name):
        test_data = test_results.get(test_name, {})
        test_status = test_data.get("status", "UNKNOWN")
        test_reason = test_data.get("reason", "")
        if test_reason:
            return f"{test_status}: {test_reason}"
        else:
            return test_status
    
    values = [
        timestamp,
        mac,
        status,
        format_test_result("DeviceID"),
        format_test_result("NVS"),
        gps_cell,
        format_test_result("LED"),
        format_test_result("Charging Detect"),
        format_test_result("Battery ADC"),
        format_test_result("SPI Flash"),
        format_test_result("IMU"),
        format_test_result("IMU INT (GPIO5)"),
        format_test_result("BLE Connection"),
        format_test_result("Hub Connect"),
    ]
    
    try:
        wks.append_table(values=values)
        # print(f"saved to sheets: {mac}")
    except Exception as e:
        return
        # print(f"failed to save: {e}")
        
def monitor_multiple_esps():
    seen_ports = set()
    print("Monitoring for ESP32-C6 units. Plug them in one by one...")

    while True:
        current_ports = {p.device: p.hwid for p in serial.tools.list_ports.comports()}
        
        for device, hwid in current_ports.items():
            if "303A" in hwid.upper() and device not in seen_ports:
                print(f"\nNew ESP32-C6 detected: {device}")
                seen_ports.add(device)
                
                success = flash_merged(device)
                status = "Flashed" if success else "Failed"
                
                mac = get_device_mac(device)
                log_to_sheets(mac, status)
                # print(f"Saved to Excel: MAC {mac} | Status {status}")

        disconnected = seen_ports - set(current_ports.keys())
        for device in disconnected:
            print(f"ESP32-C6 removed: {device}. Ready for next device.")
            seen_ports.remove(device)

        time.sleep(0.5)

def run_esptool(args, retry_on_fail=True, return_output=False):
    cmd = [sys.executable, "-m", "esptool"] + args
    # User-facing note
    # if any(k in args for k in ("erase_flash", "erase-flash", "read_mac", "read-mac", "get_mac", "get-mac")):
    #     print("\n🚀 Running esptool command...")
    # else:
    #     print("\n🚀 Flashing with esptool...")

    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if return_output:
            # Return combined stdout+stderr for robust parsing
            return (result.stdout or "") + "\n" + (result.stderr or "")
        # Default success message for flashing ops
        # print("✅ Flash completed.")
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


def flash_dir(build_dir: Path, port: str, flash_baud: str):
    build_dir = Path(build_dir)
    if not build_dir.exists():
        print(f"❌ Build directory not found: {build_dir}")
        return False

    merged = next(build_dir.glob("*.merged.bin"), None)
    if merged:
        # print(f"\n✅ Found firmware image: {merged.name} - flashing...")
        return run_esptool([
            "--chip", CHIP,
            "--port", port,
            "--baud", flash_baud,
            "write-flash",
            "--flash-mode", FLASH_MODE,
            "--flash-freq", FLASH_FREQ,
            "--flash-size", FLASH_SIZE,
            "0x0000", str(merged),
        ])

    # Otherwise try split files
    files = {
        "0x0000": "*.bootloader.bin",
        "0x8000": "*.partitions.bin",
        "0xe000": "boot_app0.bin",
        "0x10000": "*.ino.bin",
    }

    flash_args = []
    for addr, pattern in files.items():
        matches = list(build_dir.glob(pattern))
        if not matches:
            print(f"❌ Firmware file missing ({pattern}) in {build_dir}. Aborting.")
            return False
        flash_args.extend([addr, str(matches[0])])

    # print("\n✅ Found split images, flashing now")
    return run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", flash_baud,
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
        *flash_args,
    ])


def erase_flash(port: str, flash_baud: str):
    # print("\n--- Erasing flash (this may take a few seconds)... ---")
    ok = run_esptool(["--chip", CHIP, "--port", port, "--baud", flash_baud, "erase_flash"])
    # if ok:
    #     print("✅ Erase completed.")
    # else:
    #     print("❌ Erase failed. Aborting.")
    return ok


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


def register_device(mac12: str, url: str, timeout_s: int) -> bool:
    """POST the device ID payload 'AT<MAC12>' to the given URL and log a short, operator-friendly result.

    Returns True on HTTP 2xx, False otherwise.
    """
    payload = "AT" + mac12
    # print(f"Registering device: {payload} ...")
    try:
        req = urllib.request.Request(url, data=payload.encode('utf-8'),
                                     headers={'Content-Type': 'text/plain'}, method='POST')
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            code = resp.getcode()
            if 200 <= code < 300:
                # print(f"✅ Registration complete: {payload}")
                return True
            else:
                print(f"❌ Registration failed: server returned {code}")
                return False
    except urllib.error.HTTPError as e:
        print(f"❌ Registration failed: server returned {e.code}")
        return False
    except urllib.error.URLError:
        print("❌ Registration failed: network error (check internet/WiFi)")
        return False
    except Exception:
        print("❌ Registration failed: unknown error")
        return False


def _print_banner(msg: str, kind: str = 'info'):
    # Simple banner to make pass/fail visible to operators
    line = '=' * max(len(msg) + 8, 40)
    if kind == 'fail':
        print('\n' + line)
        print(f"!!!  TEST FAILED: {msg}  !!!")
        print(line + '\n')
    elif kind == 'pass':
        print('\n' + line)
        print(f"***  TEST PASSED: {msg}  ***")
        print(line + '\n')
    else:
        print('\n' + line)
        print(msg)
        print(line + '\n')


def print_test_summary(test_results: dict, overall_status: str):
    """Print test results summary in same order as Google Sheets columns."""
    print("\n" + "="*70)
    print("TEST RESULTS")
    print("="*70)
    
    # Print in same order as Google Sheets columns
    test_order = [
        # "DeviceID",
        # "NVS", 
        "GPS",  # Note: this will show GPS status, but GPS Initialization is handled in sheets
        # "LED",
        # "Charging Detect",
        # "Battery ADC", 
        "SPI Flash",
        "IMU",
        "IMU INT (GPIO5)",
        # "BLE Connection",
        # "Hub Connect"
    ]

    # Special handling for GPS: combine GPS and GPS Initialization
    if test_results.get("GPS Initialization", {}).get("status") == "PASS":
        gps_status = "PASS"
        gps_reason = ""
    else:
        gps_test = test_results.get("GPS", {})
        gps_status = gps_test.get("status", "FAILED")
        gps_reason = gps_test.get("reason", "")
    
    for test_name in test_order:
        if test_name in test_results:
            if test_name == "GPS": 
                status = gps_status
                reason = gps_reason
            else:
                status = test_results[test_name]["status"]
                reason = test_results[test_name]["reason"]
            
            symbol = "✅" if status == "PASS" else "❌"
            if reason:
                print(f"{symbol} {test_name}: {status} - {reason}")
            else:
                print(f"{symbol} {test_name}: {status}")
    
    print("="*70)
    print(f"Overall Status: {overall_status.upper()}")
    print("="*70 + "\n")


def stream_serial_for_signals(port: str, baud: int, ready_re, pass_re, fail_re, timeout_s: int):
    """Stream serial logs and capture TEST results.
    
    Returns: tuple of (status_str, test_results_dict)
    """
    # print(f"\n🔌 Opening serial {port} @ {baud}")
    try:
        ser = serial.Serial(port, baudrate=baud, timeout=1)
    except Exception:
        print("❌ Cannot open serial port. Check device connection and try again.")
        return 'error', {}

    # Copy the expected tests dict and initialize all as FAILED
    test_results = {k: {"status": v["status"], "reason": v["reason"]} 
                    for k, v in EXPECTED_TESTS.items()}

    start = time.time()
    test_log_pattern = re.compile(r"^TEST:\s+(.+?)\s+=>\s+(PASS|FAIL)(?::\s+(.*))?$")
    
    try:
        while True:
            if timeout_s and (time.time() - start) > timeout_s:
                print("\n⏱️  Timeout waiting for readiness string")
                return 'timeout', test_results

            try:
                line = ser.readline()
            except serial.SerialException:
                # print("\n❌ Serial disconnected.")
                return 'disconnect', test_results
            except Exception:
                print("\n❌ Serial read error.")
                return 'error', test_results

            if not line:
                continue

            try:
                text = line.decode(errors='replace').rstrip('\r\n')
            except Exception:
                text = repr(line)

            ts = time.strftime('%H:%M:%S')
            # print(f"[{ts}] {text}")

            # Parse TEST log lines
            test_match = test_log_pattern.match(text)
            if test_match:
                test_name = test_match.group(1)
                result_type = test_match.group(2)  # PASS or FAIL
                reason = test_match.group(3) or ""       # reason (or empty string)
                
                if test_name in test_results:
                    test_results[test_name]["status"] = result_type
                    test_results[test_name]["reason"] = reason

            # Detect overall fail
            if fail_re and fail_re.search(text):
                # _print_banner('OVERALL: FAIL', kind='fail')
                continue

            # Detect overall pass
            if pass_re and pass_re.search(text):
                # _print_banner('OVERALL: PASS', kind='pass')
                continue

            # Detect readiness string
            if ready_re and ready_re.search(text):
                # print("\n✅ Readiness string detected (UPLOAD FIRMWARE READY)")
                return 'ready', test_results

    finally:
        try:
            ser.close()
        except Exception:
            pass

    return 'error', test_results


def main():
    # Configuration is defined at the top of the script via module-level variables.
    # Edit PORT, DEVICE_BUILD, FIRMWARE_BUILD, SERIAL_BAUD, FLASH_BAUD, TIMEOUT,
    # READY_REGEX, PASS_REGEX, FAIL_REGEX, REGISTER_URL, and REGISTER_TIMEOUT as needed.
    # No command-line arguments are required or supported.

    device_build = Path(DEVICE_BUILD)
    firmware_build = Path(FIRMWARE_BUILD)

    seen_ports = set()
    print("Monitoring for ESP32-C6 units. Plug them in one by one...")

    while True:
        current_ports = {p.device: p.hwid for p in serial.tools.list_ports.comports()}
        
        for port, hwid in current_ports.items():
            if "303A" in hwid.upper() and port not in seen_ports:
                print(f"\nNew ESP32-C6 detected: {port}")
                seen_ports.add(port)
                
                # print(f"\nUsing port: {port}")

                # Brief operator-facing summary of configuration
                # print("\nConfiguration:")
                # print(f" - Test build dir: {device_build}")
                # print(f" - Firmware build dir: {firmware_build}")
                # print(f" - Serial baud: {SERIAL_BAUD}")
                # print(f" - Flash baud: {FLASH_BAUD}")
                # print(f" - Registration URL: {REGISTER_URL}")
                # print()

                # Erase flash to ensure clean state, and read base MAC
                if not erase_flash(port, FLASH_BAUD):
                    sys.exit(1)

                device_mac = get_device_mac(port)
                if device_mac:
                    print(f"Device base MAC: {device_mac}")
                else:
                    print("\n❌ Could not read BASE MAC from bootloader (read-mac). Aborting.")
                    sys.exit(1) 

                # Flash test firmware
                print("\n--- Flashing test firmware ---")
                ok = flash_dir(device_build, port, FLASH_BAUD)
                if not ok:
                    print("\n❌ Failed to flash test firmware. Aborting.")
                    sys.exit(1)

                # Give device a moment to boot and start printing
                # print("\n⏳ Waiting 1.5s for device reboot...")
                # time.sleep(1.5)

                ready_re = re.compile(READY_REGEX)
                pass_re = re.compile(PASS_REGEX)
                fail_re = re.compile(FAIL_REGEX)

                print("\n--- Starting test ---")
                status, test_results = stream_serial_for_signals(port, SERIAL_BAUD, ready_re, pass_re, fail_re, TIMEOUT)

                mac12 = device_mac.replace(":", "").upper()
                
                if status == 'ready':
                    # Ready: flash production firmware
                    print("\n--- Readiness confirmed: flashing production firmware ---")
                    ok = flash_dir(firmware_build, port, FLASH_BAUD)
                    if ok:
                        print("\n✅ Firmware upload complete. Done.")
                        
                        register_device(mac12, REGISTER_URL, REGISTER_TIMEOUT)
                        
                        log_to_sheets(mac12, "Flashed", test_results)
                        # print(f"Saved to Excel: MAC {mac12} | Status {status}")
                        
                        # Print test summary before exit
                        print_test_summary(test_results, 'PASS')
                    else:
                        # print("\n❌ Firmware upload failed.")
                        log_to_sheets(mac12, "Test Passed, Flash Failed", test_results)
                        print_test_summary(test_results, 'FIRMWARE UPLOAD FAILED')
                elif status == 'fail':
                    # print("\n❌ Test reported OVERALL: FAIL. Firmware not uploaded.")
                    log_to_sheets(mac12, "Test Failed", test_results)
                    print_test_summary(test_results, 'FAIL')
                else:
                    # print("\n❌ Test failed or device disconnected - firmware not uploaded.")
                    log_to_sheets(mac12, "Test Failed", test_results)
                    print_test_summary(test_results, 'ERROR/TIMEOUT')

        disconnected = seen_ports - set(current_ports.keys())
        for device in disconnected:
            print(f"ESP32-C6 removed: {device}. Ready for next device.")
            seen_ports.remove(device)

        time.sleep(0.5)


if __name__ == '__main__':
    main()
