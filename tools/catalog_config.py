"""KairoTowns's catalog settings: FM Towns CD and floppy media, artwork references and the
platform's slug. A CD's content ID hashes its data: an ISO itself, an MDS its MDF, a CUE the BIN
of the same name."""
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

CD = {".iso", ".mds", ".cue"}
FLOPPY = {".d77", ".d88", ".88d"}
LIMITS = {"cd": 2 * 1024**3, "fd": 64 * 1024**2}
KINDS = set(LIMITS)
CD_DATA = {".mds": ".mdf", ".cue": ".bin"}


def kind(name):
    suffix = Path(name).suffix.lower()
    return "cd" if suffix in CD else "fd" if suffix in FLOPPY else None


def data(name, kind):
    suffix = Path(name).suffix.lower()
    return name[:-len(suffix)] + CD_DATA[suffix] if suffix in CD_DATA else name


MEDIA = content_ids.Media(kind, data, LIMITS.__getitem__, 1, seven_zip=True)

MOBYGAMES = "fmtowns"
# No local media scanned yet.
SCANS = {}
