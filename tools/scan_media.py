#!/usr/bin/env python3
"""Hash the FM Towns media in a folder of files, ZIPs and 7z archives into a scan index.

Usage: scan_media.py <media folder> <index.json>
An existing index is reused for files whose size and modification time are unchanged.
"""
import json
import sys
from collections import Counter
from pathlib import Path

from catalog_config import MEDIA, content_ids


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    root, index = Path(sys.argv[1]), Path(sys.argv[2])
    previous = json.loads(index.read_text(encoding="utf-8")) if index.is_file() else None
    result = content_ids.scan(root, MEDIA, previous)
    index.parent.mkdir(parents=True, exist_ok=True)
    temporary = index.with_name(index.name + ".tmp")
    temporary.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(index)
    print(json.dumps(dict(Counter(item["status"] for item in result["archives"]))))


if __name__ == "__main__":
    main()
