"""KairoTowns's catalog settings: FM Towns CD and floppy media, artwork references and the
platform's slug. A CD's content ID hashes its data track file, as the app's media rules find it
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

CD = {".iso", ".mds", ".cue", ".ccd"}
FLOPPY = {".d77", ".d88", ".88d"}
LIMITS = {"cd": 2 * 1024**3, "fd": 64 * 1024**2}
KINDS = set(LIMITS)
CD_DATA = {".ccd": [".img"], ".mds": [".mdf"], ".cue": [".bin", ".img", ".iso"]}
SUPERSEDED_BY = {".cue": {".ccd", ".mds"}, ".iso": {".cue"}}


def kind(name):
    suffix = Path(name).suffix.lower()
    return "cd" if suffix in CD else "fd" if suffix in FLOPPY else None


MEDIA = content_ids.Media(kind, content_ids.same_file, LIMITS.__getitem__, 2, seven_zip=True,
                          candidates=CD_DATA, superseded_by=SUPERSEDED_BY)

MOBYGAMES = "fmtowns"
SCANS = {"fmtowns-cd": (".downloads/scan/fmtowns-cd.json", ("fmtowns",))}

# Disk sets (shared/tools/disk_sets.py): a game boots from its CD, with its floppy (often a user disk) in drive A.
BOOT_KINDS = ["cd", "fd"]
COMPANION = {"cd": ("fd", "floppyA", True)}
