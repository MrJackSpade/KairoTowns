"""KairoTowns's catalog settings: FM Towns CD, floppy and SCSI hard disk media, artwork references
and the platform's slug. A CD's content ID hashes its data track file, as the app's media rules find it
(kairotowns MainActivity): a CCD its IMG, an MDS its MDF, a CUE the BIN, IMG or ISO beside it,
and an ISO itself. A CUE beside a CCD or MDS of the same name, and an ISO beside a CUE, are the
same disc, listed once."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "shared/tools"))
import content_ids
from launchbox_artwork import References

APP = "kairotowns"
PLATFORMS = ("fmtowns",)
ARTWORK_CODE = 86
ARTWORK = References("KairoTowns", (ARTWORK_CODE,))

# CHDs hash the disc image they were made from (shared tools/chd_image.py, KairoTowns#97).
CD = {".iso", ".mds", ".cue", ".ccd", ".chd"}
FLOPPY = {".d77", ".d88", ".88d", ".hdm"}
# SCSI hard disks, the SCSI ID in the extension (KairoTowns#38).
HARD_DISK = {".h0", ".h1", ".h2", ".h3", ".h4", ".h5", ".h6"}
LIMITS = {"cd": 2 * 1024**3, "fd": 64 * 1024**2, "hd": 4 * 1024**3}
KINDS = set(LIMITS)
CD_DATA = {".ccd": [".img"], ".mds": [".mdf"], ".cue": [".bin", ".img", ".iso"]}
SUPERSEDED_BY = {".cue": {".ccd", ".mds"}, ".iso": {".cue"}}


def kind(name):
    suffix = Path(name).suffix.lower()
    return "cd" if suffix in CD else "fd" if suffix in FLOPPY else "hd" if suffix in HARD_DISK else None


# Version 3: HDM floppies and hard disks. Version 4: CHDs.
MEDIA = content_ids.Media(kind, content_ids.same_file, LIMITS.__getitem__, 4, seven_zip=True,
                          candidates=CD_DATA, superseded_by=SUPERSEDED_BY)

MOBYGAMES = "fmtowns"
SCANS = {"fmtowns-cd": (".downloads/scan/fmtowns-cd.json", ("fmtowns",))}

# Disk sets (shared/tools/disk_sets.py): a game boots from its hard disk, with its set's CD in the drive,
# else from its CD, with its floppy (often a user disk) in drive A.
BOOT_KINDS = ["hd", "cd", "fd"]
COMPANION = {"cd": ("fd", "floppyA", True), "hd": ("cd", "cd", False, True)}

# The media slots a disk swap can use and the disks each takes (MainActivity.mediaSlots, the
# catalog schema's CatalogSetting.DiskSwaps; shared/tools/startup_automation.py).
DISK_SWAPS = {0: {"cd"}, 1: {"fd"}, 2: {"fd"}}

# The extensions the app removes from a file's name before looking it up (the catalog schema's
# mediaExtensions); the name index is built the same way (shared app_catalog.lookup_name).
MEDIA_EXTENSIONS = r"\.(?:zip|7z|ccd|cue|mds|iso|chd|img|bin|mdf|d77|d88|88d|hdm|h[0-6])$"
