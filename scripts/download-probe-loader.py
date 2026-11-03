#!/usr/bin/env python3
"""Fetch the researched Enshwick dev bundle, refusing changed artifacts."""
import argparse
import hashlib
from pathlib import Path
import urllib.request

URL = "https://github.com/enshwick/enshwick/releases/download/dev/enshwick.zip"
SHA256 = "f6ceb5b93bf0e659f95e1f75b67c578b38b6ac1b510a0027999380e2ab3de7ec"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(".build/enshwick.zip"))
    args = parser.parse_args()
    try:
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read(32 * 1024 * 1024 + 1)
        if len(data) > 32 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != SHA256:
            raise ValueError("Loader archive changed. Do not bypass verification: review a new SDK/loader pair and rebuild, or use a retained matching archive.")
        if args.output.exists() and args.output.read_bytes() != data:
            raise ValueError(f"Refusing to overwrite a different file: {args.output}")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(f"Verified {args.output}: {SHA256}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Download failed: {error}\n")


if __name__ == "__main__":
    main()
