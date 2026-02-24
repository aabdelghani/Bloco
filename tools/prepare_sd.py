#!/usr/bin/env python3
"""
Prepare SD card for DFPlayer Mini.

Detects a connected SD card, formats it as FAT32, and copies
the sounds/ folder with the correct numbered folder/file naming
required by the DFPlayer Mini module.

Usage:
    sudo python3 tools/prepare_sd.py

SD Card Layout (created by this script):
    01/  idle sounds     (001-004.mp3)
    02/  happy sounds    (001-002.mp3)
    03/  excited sounds  (001-003.mp3)
    04/  misc sounds     (001-006.mp3)
"""

import os
import sys
import glob
import shutil
import subprocess
import tempfile

# --- File mapping: (source filename) -> (folder, track number) ---
# DFPlayer uses numbered folders (01-99) and numbered tracks (001-255)
FILE_MAP = [
    # Folder 01: Idle sounds
    ("idle1.mp3",      "01", "001.mp3"),
    ("idle2.mp3",      "01", "002.mp3"),
    ("idle3.mp3",      "01", "003.mp3"),
    ("idle4.mp3",      "01", "004.mp3"),
    # Folder 02: Happy sounds
    ("happy1.mp3",     "02", "001.mp3"),
    ("happy2.mp3",     "02", "002.mp3"),
    # Folder 03: Excited sounds
    ("excited1.mp3",   "03", "001.mp3"),
    ("excited2.mp3",   "03", "002.mp3"),
    ("excited3.mp3",   "03", "003.mp3"),
    # Folder 04: Misc sounds
    ("scared.mp3",     "04", "001.mp3"),
    ("sad.mp3",        "04", "002.mp3"),
    ("connected.mp3",  "04", "003.mp3"),
    ("disconnect.mp3", "04", "004.mp3"),
    ("end.mp3",        "04", "005.mp3"),
    ("squeek.mp3",     "04", "006.mp3"),
]

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SOUNDS_DIR = os.path.join(PROJECT_ROOT, "sounds")


def find_sd_card():
    """Detect removable block devices that look like an SD card."""
    candidates = []
    try:
        result = subprocess.run(
            ["lsblk", "-dno", "NAME,SIZE,RM,TYPE,TRAN"],
            capture_output=True, text=True, check=True
        )
    except FileNotFoundError:
        print("Error: lsblk not found")
        sys.exit(1)

    for line in result.stdout.strip().splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        name, size, rm, dtype = parts[0], parts[1], parts[2], parts[3]
        tran = parts[4] if len(parts) > 4 else ""

        # Skip zero-size, non-removable, and non-disk entries
        if size == "0B" or rm != "1" or dtype != "disk":
            continue

        dev = f"/dev/{name}"
        candidates.append((dev, size, tran))

    return candidates


def get_partition(device):
    """Get the first partition of a device, or the device itself."""
    # Check for partitions like /dev/sda1 or /dev/mmcblk0p1
    result = subprocess.run(
        ["lsblk", "-lno", "NAME,TYPE", device],
        capture_output=True, text=True
    )
    for line in result.stdout.strip().splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "part":
            return f"/dev/{parts[0]}"
    return device


def format_fat32(device):
    """Format device as FAT32 with a single partition."""
    print(f"\nFormatting {device} as FAT32...")

    # Unmount if mounted
    subprocess.run(["umount", device], capture_output=True)
    # Also unmount any partitions
    result = subprocess.run(
        ["lsblk", "-lno", "NAME", device],
        capture_output=True, text=True
    )
    for line in result.stdout.strip().splitlines():
        dev_name = line.strip()
        subprocess.run(["umount", f"/dev/{dev_name}"], capture_output=True)

    # Create a new partition table with a single FAT32 partition
    print("  Creating partition table...")
    subprocess.run(
        ["parted", "-s", device, "mklabel", "msdos"],
        check=True
    )
    subprocess.run(
        ["parted", "-s", device, "mkpart", "primary", "fat32", "1MiB", "100%"],
        check=True
    )

    # Wait for kernel to detect new partition
    subprocess.run(["partprobe", device], capture_output=True)
    subprocess.run(["sleep", "1"])

    # Find the new partition
    partition = get_partition(device)
    if partition == device:
        # Try common partition naming
        if "mmcblk" in device or "nvme" in device:
            partition = f"{device}p1"
        else:
            partition = f"{device}1"

    print(f"  Formatting {partition} as FAT32...")
    subprocess.run(
        ["mkfs.vfat", "-F", "32", "-n", "DFPLAYER", partition],
        check=True
    )

    return partition


def copy_sounds(mount_point):
    """Copy sound files to SD card with DFPlayer naming convention."""
    print(f"\nCopying sounds to {mount_point}...")

    # Create folders
    folders = sorted(set(folder for _, folder, _ in FILE_MAP))
    for folder in folders:
        folder_path = os.path.join(mount_point, folder)
        os.makedirs(folder_path, exist_ok=True)
        print(f"  Created folder: {folder}/")

    # Copy files IN ORDER (critical for DFPlayer — it indexes by copy order)
    copied = 0
    missing = 0
    for src_name, folder, dst_name in FILE_MAP:
        src_path = os.path.join(SOUNDS_DIR, src_name)
        dst_path = os.path.join(mount_point, folder, dst_name)

        if not os.path.isfile(src_path):
            print(f"  WARNING: {src_name} not found in sounds/ — skipping")
            missing += 1
            continue

        shutil.copy2(src_path, dst_path)
        print(f"  {src_name:20s} -> {folder}/{dst_name}")
        copied += 1
        # Small delay between copies to preserve file order on FAT
        subprocess.run(["sync"])

    print(f"\nDone: {copied} files copied, {missing} missing")
    return copied, missing


def main():
    # Check root
    if os.geteuid() != 0:
        print("This script must be run as root (for formatting).")
        print(f"  sudo python3 {sys.argv[0]}")
        sys.exit(1)

    # Check sounds directory
    if not os.path.isdir(SOUNDS_DIR):
        print(f"Error: sounds/ directory not found at {SOUNDS_DIR}")
        sys.exit(1)

    # Find SD card
    print("Scanning for removable devices...")
    candidates = find_sd_card()

    if not candidates:
        print("\nNo SD card detected!")
        print("Please insert the SD card and try again.")
        print("\nIf the card is inserted, check:")
        print("  - lsblk  (to see all devices)")
        print("  - dmesg | tail  (for recent USB/SD events)")
        sys.exit(1)

    # Show candidates
    print(f"\nFound {len(candidates)} removable device(s):\n")
    for i, (dev, size, tran) in enumerate(candidates):
        tran_str = f" ({tran})" if tran else ""
        print(f"  [{i + 1}] {dev}  {size}{tran_str}")

    # Select device
    if len(candidates) == 1:
        choice = 0
    else:
        try:
            sel = input(f"\nSelect device [1-{len(candidates)}]: ").strip()
            choice = int(sel) - 1
            if choice < 0 or choice >= len(candidates):
                raise ValueError
        except (ValueError, EOFError):
            print("Invalid selection")
            sys.exit(1)

    device, size, tran = candidates[choice]

    # Confirm
    print(f"\n{'='*50}")
    print(f"  Device:  {device}")
    print(f"  Size:    {size}")
    print(f"  Action:  FORMAT as FAT32 + copy sounds")
    print(f"{'='*50}")
    print(f"\n  *** ALL DATA ON {device} WILL BE ERASED ***\n")

    try:
        confirm = input("Type 'YES' to continue: ").strip()
    except EOFError:
        confirm = ""
    if confirm != "YES":
        print("Aborted.")
        sys.exit(0)

    # Format
    partition = format_fat32(device)

    # Mount
    mount_point = tempfile.mkdtemp(prefix="dfplayer_")
    print(f"\nMounting {partition} at {mount_point}...")
    subprocess.run(["mount", partition, mount_point], check=True)

    try:
        # Copy sounds
        copied, missing = copy_sounds(mount_point)

        # Show result
        print(f"\n{'='*50}")
        print("SD Card contents:")
        for root, dirs, files in os.walk(mount_point):
            level = root.replace(mount_point, "").count(os.sep)
            indent = "  " * level
            folder_name = os.path.basename(root) or "/"
            print(f"  {indent}{folder_name}/")
            for f in sorted(files):
                fpath = os.path.join(root, f)
                fsize = os.path.getsize(fpath)
                print(f"  {indent}  {f}  ({fsize} bytes)")
        print(f"{'='*50}")

    finally:
        # Sync and unmount
        print("\nSyncing and unmounting...")
        subprocess.run(["sync"])
        subprocess.run(["umount", mount_point])
        os.rmdir(mount_point)

    print("\nSD card is ready! Insert it into the DFPlayer Mini module.")


if __name__ == "__main__":
    main()