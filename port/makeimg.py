# Combine bootloader, partition table and application into a final binary.
from __future__ import print_function

import argparse
import datetime
import subprocess
import os
import shutil
import sys
import tempfile
from pathlib import Path

idf_path = os.getenv("IDF_PATH")
if not idf_path:
    raise RuntimeError("IDF_PATH is not set; activate the ESP-IDF environment first")
sys.path.append(str(Path(idf_path) / "components" / "partition_table"))

import gen_esp32part

OFFSET_BOOTLOADER_DEFAULT = 0x1000
OFFSET_PARTITIONS_DEFAULT = 0x8000

def get_version_info_from_git(repo_path):
    # Python 2.6 doesn't have check_output, so check for that
    try:
        subprocess.check_output
    except AttributeError:
        return None

    # Note: git describe doesn't work if no tag is available
    try:
        git_tag = subprocess.check_output(
            ["git", "describe", "--tags", "--dirty", "--always", "--match", "v[1-9].*"],
            cwd=repo_path,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        ).strip()
        # Turn git-describe's output into semver compatible (dot-separated
        # identifiers inside the prerelease field).
        git_tag = git_tag.split("-", 1)
        if len(git_tag) == 1:
            return git_tag[0]
        else:
            return git_tag[0] + "-" + git_tag[1].replace("-", ".")
    except (subprocess.CalledProcessError, OSError):
        return None

def get_hash_from_git(repo_path):
    # Python 2.6 doesn't have check_output, so check for that.
    try:
        subprocess.check_output
    except AttributeError:
        return None

    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=repo_path,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        ).strip()
    except (subprocess.CalledProcessError, OSError):
        return None

def load_sdkconfig_value(filename, value, default):
    value = "CONFIG_" + value + "="
    with open(filename, "r") as f:
        for line in f:
            if line.startswith(value):
                return line.split("=", 1)[1]
    return default


def load_sdkconfig_hex_value(filename, value, default):
    value = load_sdkconfig_value(filename, value, None)
    if value is None:
        return default
    return int(value, 16)


def load_sdkconfig_str_value(filename, value, default):
    value = load_sdkconfig_value(filename, value, None)
    if value is None:
        return default
    return value.strip().strip('"')


def load_partition_table(filename):
    with open(filename, "rb") as f:
        return gen_esp32part.PartitionTable.from_binary(f.read())

def littlefs_bin_park(port_dir, build_dir, board_name, language, size):
    file_system_dir = port_dir / "file_system"
    sources = [file_system_dir / "common", file_system_dir / board_name]
    if language == "en":
        # The mpython_v3 English UI was supplied under its product name.
        english_resource_name = "labplus_mpython_pro" if board_name == "mpython_v3" else board_name
        sources.append(port_dir / "en" / english_resource_name)

    for source in sources:
        if not source.is_dir():
            raise FileNotFoundError("File-system source directory not found: {}".format(source))

    # A fresh staging directory prevents stale files from earlier builds leaking
    # into this firmware image. The English tree overlays common and board files.
    with tempfile.TemporaryDirectory(prefix="makeimg-filesystem-", dir=str(build_dir)) as temp_dir:
        staging_dir = Path(temp_dir) / "file_system"
        staging_dir.mkdir()
        for source in sources:
            shutil.copytree(str(source), str(staging_dir), dirs_exist_ok=True)

        subprocess.run(
            [str(port_dir / "mklittlefs"), "-c", str(staging_dir), "-s", str(size), str(build_dir / "littlefs.bin")],
            check=True,
            cwd=str(build_dir),
        )


parser = argparse.ArgumentParser(description="Build the complete MicroPython firmware image")
parser.add_argument("board", help="MicroPython board name")
parser.add_argument("language", nargs="?", choices=("zh", "en"), default="zh")
args = parser.parse_args()

port_dir = Path(__file__).resolve().parent
repo_dir = port_dir.parent
build_dir = Path.cwd().resolve()
arg_board_name = args.board
arg_language = args.language

git_tag = get_version_info_from_git(str(repo_dir))
git_hash = get_hash_from_git(str(repo_dir))
build_date = datetime.date.today().strftime("%Y-%m-%d")
bin_name = "{}-{}-{}_{}-{}.bin".format(arg_board_name, git_tag, git_hash, build_date, arg_language)
uf2_name = "{}-{}-{}_{}-{}.uf2".format(arg_board_name, git_tag, git_hash, build_date, arg_language)
print(bin_name)

littlefs_bin_park(port_dir, build_dir, arg_board_name, arg_language, 0x200000)

arg_sdkconfig = build_dir / "sdkconfig"
arg_bootloader_bin = build_dir / "bootloader" / "bootloader.bin"
arg_partitions_bin = build_dir / "partition_table" / "partition-table.bin"
arg_application_bin = build_dir / "micropython.bin"
arg_vfs_bin = build_dir / "littlefs.bin"
arg_output_bin = build_dir / bin_name
arg_output_uf2 = build_dir / uf2_name
arg_voice_data_bin = port_dir / "managed_components" / "espressif__esp-sr" / "esp-tts" / "esp_tts_chinese" / "esp_tts_voice_data_xiaoxin.dat"
arg_sr_bin = build_dir / "srmodels" / "srmodels.bin"

# Load required sdkconfig values.
idf_target = load_sdkconfig_str_value(arg_sdkconfig, "IDF_TARGET", "").upper()
offset_bootloader = load_sdkconfig_hex_value(
    arg_sdkconfig, "BOOTLOADER_OFFSET_IN_FLASH", OFFSET_BOOTLOADER_DEFAULT
)
offset_partitions = load_sdkconfig_hex_value(
    arg_sdkconfig, "PARTITION_TABLE_OFFSET", OFFSET_PARTITIONS_DEFAULT
)

# Load the partition table.
partition_table = load_partition_table(arg_partitions_bin)

max_size_bootloader = offset_partitions - offset_bootloader
max_size_partitions = 0
offset_application = 0
max_size_application = 0
offset_font = 0
max_size_font = 0
offset_vfs = 0
max_size_vfs = 0
offset_voice_data = 0
max_size_voice_data = 0
offset_sr = 0
max_size_sr = 0

# partition table
# Name,       Type, SubType,   Offset,    Size,      Flags
# -------------------------------------------------------
# bootloader, app,  boot,      0x0,       0x7000,
# partitions, data, partition, 0x8000,    0
# nvs,        data, nvs,       0x9000,    0x6000,
# phy_init,   data, phy,       0xf000,    0x1000,
# factory,    app,  factory,   0x10000,   0x6E0000,
# vfs,        data, spiffs,    0x6F0000,  0x200000,
# voice_data, data, fat,       0x8F0000,  0x3CD000,
# model,  data, spiffs,    0xCBD000,  0x31F000,
# fr,         data,   ,        0xFDC000,  0x20000
# user_nvs,   data, nvs,       0xFFC000,  0x4000,

# Inspect the partition table to find offsets and maximum sizes.
for part in partition_table:
    if part.name == "nvs":
        max_size_partitions = part.offset - offset_partitions # partition table size
    # elif part.name == "font":
    #     offset_font = part.offset
    #     max_size_font = part.size
    elif part.type == gen_esp32part.APP_TYPE and offset_application == 0:
        offset_application = part.offset
        max_size_application = part.size
    elif part.name == "vfs":
        offset_vfs = part.offset
        max_size_vfs = part.size
    elif part.name == "voice_data":
        offset_voice_data = part.offset
        max_size_voice_data = part.size
    elif part.name == "model":
        offset_sr = part.offset
        max_size_sr = part.size

# Define the input files, their location and maximum size.
files_in = [
    ("bootloader", offset_bootloader, max_size_bootloader, arg_bootloader_bin),
    ("partitions", offset_partitions, max_size_partitions, arg_partitions_bin),
    ("application", offset_application, max_size_application, arg_application_bin),
    ("vfs", offset_vfs, max_size_vfs, arg_vfs_bin),
]
if arg_language != "en" and offset_voice_data > 0:
    files_in.append(("voice_data", offset_voice_data, max_size_voice_data, arg_voice_data_bin))
files_in.extend([
    ("model", offset_sr, max_size_sr, arg_sr_bin),
])
file_out = arg_output_bin

# Write output file with combined firmware.
cur_offset = offset_bootloader
with open(file_out, "wb") as fout:
    for name, offset, max_size, file_in in files_in:
        assert offset >= cur_offset
        fout.write(b"\xff" * (offset - cur_offset))
        cur_offset = offset
        with open(file_in, "rb") as fin:
            data = fin.read()
            fout.write(data)
            cur_offset += len(data)
            print(
                "%-12s@0x%06x % 8d  (% 8d remaining)"
                % (name, offset, len(data), max_size - len(data))
            )
            if len(data) > max_size:
                print(
                    "ERROR: %s overflows allocated space of %d bytes by %d bytes"
                    % (name, max_size, len(data) - max_size)
                )
                sys.exit(1)
    print("%-22s% 8d" % ("total", cur_offset))

# Generate .uf2 file if the SoC has native USB.
if idf_target in ("ESP32S2", "ESP32S3"):
    sys.path.append(str(repo_dir / "micropython" / "tools"))
    import uf2conv

    families = uf2conv.load_families()
    uf2conv.appstartaddr = 0
    uf2conv.familyid = families[idf_target]
    with open(arg_application_bin, "rb") as fin, open(arg_output_uf2, "wb") as fout:
        fout.write(uf2conv.convert_to_uf2(fin.read()))
