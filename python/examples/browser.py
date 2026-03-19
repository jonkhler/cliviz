"""Render a live webpage in the terminal via headless Chromium.

Install: uv pip install playwright && playwright install chromium
Run:     uv run python python/examples/browser.py [url]
Keys:    q=quit
"""

import io
import os
import select
import sys

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

import cliviz


def read_key(fd: int) -> int:
    if not select.select([fd], [], [], 0)[0]:
        return 0
    b = os.read(fd, 1)
    return b[0] if b else 0


def main() -> None:
    url = sys.argv[1] if len(sys.argv) > 1 else "https://news.ycombinator.com"

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=10)  # browsers are slow, 10fps is plenty

        browser = pw.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": pb.width, "height": pb.height})
        page.goto(url, wait_until="domcontentloaded")

        while True:
            pacer.pace()

            if read_key(sys.stdin.fileno()) == ord("q"):
                break

            # Screenshot → numpy → pixel buffer
            png = page.screenshot(type="png")
            img = Image.open(io.BytesIO(png)).convert("RGB")
            img = img.resize((pb.width, pb.height), Image.NEAREST)
            pb.pixels[:] = np.array(img, dtype=np.uint8)

            pb.encode_all()
            pb.draw_text(1, 0, f" {pacer.fps:.0f}fps  {url}  q=quit ",
                         255, 255, 255, 30, 30, 50)
            pb.present()

        browser.close()


if __name__ == "__main__":
    main()
