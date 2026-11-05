#!/usr/bin/env python3
"""Build an FBI-compatible QR code for a direct CIA download.

FBI's remote installer scans a QR code whose text is the file URL. A GitHub
release page is not a CIA. This script checks that the URL returns the file,
then writes a PNG. It does not invent a release URL.
"""

import sys
import urllib.request
from pathlib import Path


def check_url(url: str) -> None:
    if not url.startswith("https://"):
        raise SystemExit("The CIA URL must use https://")
    if not url.split("?", 1)[0].lower().endswith(".cia"):
        raise SystemExit("The URL must point at a .cia file, not a web page.")
    if "/releases/tag/" in url or url.rstrip("/").endswith("github.com"):
        raise SystemExit("Refusing a GitHub page URL. Use the release asset URL that ends in .cia.")
    request = urllib.request.Request(url, headers={"User-Agent": "2DSync-qr"})
    with urllib.request.urlopen(request, timeout=30) as response:
        content_type = response.headers.get("Content-Type", "")
        prefix = response.read(64)
    if "text/html" in content_type.lower() or prefix.lstrip().startswith(b"<"):
        raise SystemExit("The URL returned a web page. FBI needs the CIA file itself.")
    if not prefix:
        raise SystemExit("The URL returned an empty file.")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_fbi_qr.py https://host/2DSync.cia output.png")
    url, output = sys.argv[1], sys.argv[2]
    check_url(url)
    try:
        import qrcode
    except ImportError:
        raise SystemExit("Install the qrcode package first: python3 -m pip install qrcode[pil]")
    image = qrcode.make(url)
    Path(output).write_bytes(b"")
    image.save(output)
    print(f"Wrote {output}")
    print("Scan it with FBI -> Remote Install -> Scan QR Code.")


if __name__ == "__main__":
    main()
