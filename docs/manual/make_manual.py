#!/usr/bin/env python3
"""Generate the Artilody Watts Trainer user manual (PDF).

Usage:
    python docs/manual/make_manual.py [output.pdf]

Requirements: reportlab (>= 3.6) and Pillow.
The default output is docs/Artilody-Watts-Trainer-Manual.pdf.

All manual text lives in content.py (next to this script). This file contains
only the layout engine: page templates, styles, table of contents, PDF
outline, figures, tables and callout boxes.
"""

from __future__ import annotations

import re
import shutil
import sys
import tempfile
from pathlib import Path

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, CondPageBreak, Flowable, Frame, Image, KeepTogether,
                                ListFlowable, ListItem, NextPageTemplate, PageBreak, PageTemplate, Paragraph,
                                Preformatted, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DOCS = ROOT / "docs"
SHOTS = DOCS / "screenshots"
MOCKUPS = DOCS / "mockups"
DEFAULT_OUT = DOCS / "Artilody-Watts-Trainer-Manual.pdf"

APP = "Artilody Watts Trainer"
VERSION = "1.2.0"
DATE = "October 2026"
REPO = "https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer"

# --- brand colours (from qml/Theme.qml) -------------------------------------
NAVY = colors.HexColor("#1d2c74")
NAVY2 = colors.HexColor("#25398f")
ACCENT = colors.HexColor("#2178ff")
ACCENT_LIGHT = colors.HexColor("#4da3ff")
TEXT = colors.HexColor("#1f2640")
MUTED = colors.HexColor("#5b6485")
RULE = colors.HexColor("#d5dcef")
ZEBRA = colors.HexColor("#f3f6fc")
ZONE_HEX = ("#3d86fb", "#25d3e3", "#45d659", "#ffd21f", "#ff8a26", "#ff3b30")
ZONES = [colors.HexColor(c) for c in ZONE_HEX]
CALLOUT = {
    "tip": ("Tip", "#2fbf55", colors.HexColor("#eefaf0")),
    "note": ("Note", "#2178ff", colors.HexColor("#edf4ff")),
    "warning": ("Warning", "#f07a12", colors.HexColor("#fff4ea")),
}

PAGE_W, PAGE_H = A4
MARGIN_L = MARGIN_R = 20 * mm
MARGIN_T = 24 * mm
MARGIN_B = 20 * mm
FRAME_W = PAGE_W - MARGIN_L - MARGIN_R


# --- fonts -------------------------------------------------------------------
def register_fonts() -> tuple[str, str]:
    """Use Arial (good Unicode punctuation coverage) when available, else Helvetica."""
    candidates = [Path("/System/Library/Fonts/Supplemental"), Path("/Library/Fonts"),
                  Path("C:/Windows/Fonts"), Path("/usr/share/fonts/truetype/msttcorefonts")]
    names = {"": ["Arial.ttf", "arial.ttf"], "-Bold": ["Arial Bold.ttf", "arialbd.ttf"],
             "-Italic": ["Arial Italic.ttf", "ariali.ttf"], "-BoldItalic": ["Arial Bold Italic.ttf", "arialbi.ttf"]}
    for base in candidates:
        found = {}
        for suffix, files in names.items():
            for f in files:
                if (base / f).exists():
                    found[suffix] = base / f
                    break
        if len(found) == 4:
            for suffix, path in found.items():
                pdfmetrics.registerFont(TTFont("Body" + suffix, str(path)))
            pdfmetrics.registerFontFamily("Body", normal="Body", bold="Body-Bold", italic="Body-Italic",
                                          boldItalic="Body-BoldItalic")
            return "Body", "Body-Bold"
    print("warning: Arial not found, falling back to Helvetica (some symbols may be missing)")
    return "Helvetica", "Helvetica-Bold"


FONT, FONT_B = register_fonts()
MONO = "Courier"


# --- styles ------------------------------------------------------------------
def S(name, **kw) -> ParagraphStyle:
    base = dict(fontName=FONT, fontSize=10, leading=14.6, textColor=TEXT, spaceAfter=6)
    base.update(kw)
    return ParagraphStyle(name, **base)


STYLES = {
    "body": S("body"),
    "cell": S("cell", fontSize=8.8, leading=11.8, spaceAfter=0),
    "cellb": S("cellb", fontName=FONT_B, fontSize=8.8, leading=11.8, spaceAfter=0, textColor=colors.white),
    "caption": S("caption", fontSize=8.6, leading=11.5, textColor=MUTED, alignment=TA_CENTER, spaceBefore=4,
                 spaceAfter=12),
    "h1": S("h1", fontName=FONT_B, fontSize=23, leading=28, textColor=NAVY, spaceBefore=0, spaceAfter=4,
            keepWithNext=1),
    "h2": S("h2", fontName=FONT_B, fontSize=14.5, leading=19, textColor=ACCENT, spaceBefore=12, spaceAfter=5,
            keepWithNext=1),
    "h3": S("h3", fontName=FONT_B, fontSize=11.2, leading=15, textColor=NAVY, spaceBefore=8, spaceAfter=3,
            keepWithNext=1),
    "lead": S("lead", fontSize=11, leading=16, textColor=MUTED, spaceAfter=10),
    "bullet": S("bullet", spaceAfter=2.5),
    "callout": S("callout", fontSize=9.4, leading=13.4, spaceAfter=0),
    "code": S("code", fontName=MONO, fontSize=8.6, leading=11.2, textColor=colors.HexColor("#14204f"),
              spaceAfter=0),
    "toc0": S("toc0", fontName=FONT_B, fontSize=10.2, leading=12.8, leftIndent=0, spaceBefore=2.6, spaceAfter=0),
    "toc1": S("toc1", fontSize=8.9, leading=10.6, leftIndent=16, spaceAfter=0, textColor=MUTED),
}


# --- flowables -----------------------------------------------------------------
class Heading(Paragraph):
    """A heading that registers itself in the TOC and the PDF outline."""

    def __init__(self, text, style, level, key, toc_text, in_toc=True):
        super().__init__(text, style)
        self.level, self.key, self.toc_text, self.in_toc = level, key, toc_text, in_toc

    def draw(self):
        self.canv.bookmarkHorizontal(self.key, 0, self.height + 6)
        super().draw()


class ZoneStripe(Flowable):
    """Six-colour zone stripe used under chapter titles."""

    def __init__(self, width=48 * mm, height=3.2, space_after=14):
        super().__init__()
        self.w, self.h, self.space_after = width, height, space_after

    def wrap(self, aw, ah):
        return self.w, self.h + self.space_after

    def draw(self):
        seg = self.w / len(ZONES)
        for i, c in enumerate(ZONES):
            self.canv.setFillColor(c)
            self.canv.rect(i * seg, self.space_after, seg, self.h, stroke=0, fill=1)


class ManualDoc(BaseDocTemplate):
    def __init__(self, filename, cover_image, **kw):
        super().__init__(filename, pagesize=A4, leftMargin=MARGIN_L, rightMargin=MARGIN_R, topMargin=MARGIN_T,
                         bottomMargin=MARGIN_B, title=f"{APP} – User Manual", author="Artilody",
                         subject=f"User manual for {APP} {VERSION}",
                         keywords="indoor cycling, power training, FTMS, ERG, FTP", creator="make_manual.py", **kw)
        self.chapter = ""
        self.cover_image = cover_image
        full = Frame(0, 0, PAGE_W, PAGE_H, leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0, id="full")
        body = Frame(MARGIN_L, MARGIN_B, FRAME_W, PAGE_H - MARGIN_T - MARGIN_B, leftPadding=0, rightPadding=0,
                     topPadding=0, bottomPadding=0, id="body")
        self.addPageTemplates([
            PageTemplate(id="cover", frames=[full], onPage=draw_cover),
            PageTemplate(id="body", frames=[body], onPageEnd=draw_header_footer),
        ])

    def beforeDocument(self):
        self.chapter = ""

    def afterFlowable(self, fl):
        if isinstance(fl, Heading):
            if fl.level == 0:
                self.chapter = fl.toc_text if fl.in_toc else ""
            if fl.in_toc:
                self.notify("TOCEntry", (fl.level, fl.toc_text, self.page, fl.key))
            self.canv.addOutlineEntry(fl.toc_text, fl.key, level=fl.level, closed=fl.level > 0)


# --- page decoration -------------------------------------------------------------
def draw_cover(canv, doc):
    canv.saveState()
    canv.linearGradient(0, PAGE_H, 0, 0, (NAVY, NAVY2), extend=False)
    canv.setPageCompression(1)
    icon = SHOTS / "icon-256.png"
    size = 36 * mm
    top = PAGE_H - 26 * mm
    canv.drawImage(str(icon), (PAGE_W - size) / 2, top - size, size, size, mask="auto")
    y = top - size - 17 * mm
    canv.setFillColor(colors.white)
    canv.setFillAlpha(1)
    canv.setFont(FONT_B, 32)
    canv.drawCentredString(PAGE_W / 2, y, APP)
    canv.setFillColor(ACCENT_LIGHT)
    canv.setFont(FONT_B, 20)
    canv.drawCentredString(PAGE_W / 2, y - 12 * mm, "User Manual")
    canv.setFillColor(colors.Color(1, 1, 1, 0.8))
    canv.setFont(FONT, 11.5)
    canv.drawCentredString(PAGE_W / 2, y - 21 * mm,
                           "Power-based indoor cycling training for Windows, macOS, Linux and Raspberry Pi")
    # screenshot with rounded corners and a soft shadow
    w = 160 * mm
    h = w * 992 / 1584
    sx, sy = (PAGE_W - w) / 2, 66 * mm
    canv.setFillColor(colors.Color(0, 0, 0, 0.28))
    canv.roundRect(sx + 1.6 * mm, sy - 1.6 * mm, w, h, 3 * mm, stroke=0, fill=1)
    path = canv.beginPath()
    path.roundRect(sx, sy, w, h, 3 * mm)
    canv.saveState()
    canv.clipPath(path, stroke=0, fill=0)
    canv.setFillAlpha(1)  # PDF fill alpha also applies to images
    canv.drawImage(str(doc.cover_image), sx, sy, w, h)
    canv.restoreState()
    seg = PAGE_W / 6
    for i, c in enumerate(ZONES):
        canv.setFillColor(c)
        canv.rect(i * seg, 44 * mm, seg, 2.2 * mm, stroke=0, fill=1)
    canv.setFillColor(colors.white)
    canv.setFillAlpha(1)
    canv.setFont(FONT_B, 12)
    canv.drawCentredString(PAGE_W / 2, 32 * mm, f"Version {VERSION}   ·   {DATE}")
    canv.setFillColor(colors.Color(1, 1, 1, 0.72))
    canv.setFont(FONT, 9.5)
    canv.drawCentredString(PAGE_W / 2, 24 * mm, REPO.replace("https://", ""))
    canv.drawCentredString(PAGE_W / 2, 18.5 * mm, "Free software, licensed under the GNU General Public License v3.0")
    canv.linkURL(REPO, ((PAGE_W - 120 * mm) / 2, 22 * mm, (PAGE_W + 120 * mm) / 2, 28 * mm), relative=0)
    canv.restoreState()


def draw_header_footer(canv, doc):
    canv.saveState()
    top = PAGE_H - 13 * mm
    canv.setFont(FONT_B, 8.4)
    canv.setFillColor(NAVY)
    canv.drawString(MARGIN_L, top, APP)
    w = pdfmetrics.stringWidth(APP, FONT_B, 8.4)
    canv.setFont(FONT, 8.4)
    canv.setFillColor(MUTED)
    canv.drawString(MARGIN_L + w, top, "  ·  User Manual")
    if doc.chapter:
        canv.drawRightString(PAGE_W - MARGIN_R, top, doc.chapter)
    canv.setStrokeColor(RULE)
    canv.setLineWidth(0.6)
    canv.line(MARGIN_L, top - 3 * mm, PAGE_W - MARGIN_R, top - 3 * mm)
    canv.setFillColor(ACCENT)
    canv.rect(MARGIN_L, top - 3 * mm - 0.5, 18 * mm, 1.5, stroke=0, fill=1)
    fy = 11 * mm
    canv.line(MARGIN_L, fy + 4.5 * mm, PAGE_W - MARGIN_R, fy + 4.5 * mm)
    canv.setFont(FONT, 8.4)
    canv.setFillColor(MUTED)
    canv.drawString(MARGIN_L, fy, f"Version {VERSION}  ·  {DATE}")
    canv.setFont(FONT_B, 8.6)
    canv.setFillColor(NAVY)
    canv.drawRightString(PAGE_W - MARGIN_R, fy, f"Page {doc.page}")
    canv.restoreState()


# --- story builder ------------------------------------------------------------------
class Story:
    """Collects flowables; provides the small vocabulary used by content.py."""

    def __init__(self, build_dir: Path):
        self.items: list = []
        self.build_dir = build_dir
        self.ch = 0
        self.sec = 0
        self.fig = 0
        self.appendix = 0
        self.prefix = ""
        self.text_seen: list[str] = []
        self.zone_hex = ZONE_HEX

    def _p(self, text, style):
        self.text_seen.append(text)
        return Paragraph(text, style)

    # headings -------------------------------------------------------------------
    def chapter(self, title, lead=None, appendix=False, new_page=False):
        """Chapters continue on the current page when at least ~11 cm are
        left (avoids half-empty pages); new_page forces a page break."""
        self.items.append(NextPageTemplate("body"))
        if new_page:
            self.items.append(PageBreak())
        else:
            self.items.append(CondPageBreak(110 * mm))
            self.items.append(Spacer(1, 22))
        if appendix:
            self.appendix += 1
            num = "ABCDEFG"[self.appendix - 1]
            label, toc = f"Appendix {num}", f"Appendix {num}  {title}"
        else:
            self.ch += 1
            num = str(self.ch)
            label, toc = num, f"{num}  {title}"
        self.sec = 0
        self.prefix = num
        self.text_seen.append(title)
        self.items.append(Heading(f'<font color="#2178ff">{label}</font>&nbsp;&nbsp;{title}', STYLES["h1"], 0,
                                  f"ch{num}", toc))
        self.items.append(ZoneStripe())
        if lead:
            self.items.append(self._p(lead, STYLES["lead"]))

    def section(self, title):
        self.sec += 1
        num = f"{self.prefix}.{self.sec}"
        self.text_seen.append(title)
        self.items.append(CondPageBreak(34 * mm))
        self.items.append(Heading(f"{num}&nbsp;&nbsp;{title}", STYLES["h2"], 1, f"s{num}", f"{num}  {title}"))

    def sub(self, title):
        self.items.append(CondPageBreak(24 * mm))
        self.items.append(self._p(title, STYLES["h3"]))

    # text blocks ---------------------------------------------------------------
    def p(self, text):
        self.items.append(self._p(text, STYLES["body"]))

    def bullets(self, items, numbered=False):
        flow = [ListItem(self._p(t, STYLES["bullet"]), leftIndent=15) for t in items]
        if numbered:
            kw = dict(bulletType="1", bulletFormat="%s.", bulletFontName=FONT_B, bulletColor=NAVY)
        else:
            kw = dict(bulletType="bullet", start="•", bulletColor=ACCENT, bulletFontName=FONT)
        self.items.append(ListFlowable(flow, leftIndent=15, bulletFontSize=9.6, spaceAfter=6, **kw))

    def steps(self, items):
        self.bullets(items, numbered=True)

    def callout(self, kind, text):
        label, edge, bg = CALLOUT[kind]
        para = self._p(f'<font name="{FONT_B}" color="{edge}">{label}</font>&nbsp;&nbsp;{text}',
                       STYLES["callout"])
        t = Table([[para]], colWidths=[FRAME_W])
        t.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), bg),
            ("LINEBEFORE", (0, 0), (0, -1), 3.2, colors.HexColor(edge)),
            ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 10),
            ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 7.5),
        ]))
        self.items.append(KeepTogether([Spacer(1, 3), t, Spacer(1, 9)]))

    def tip(self, text):
        self.callout("tip", text)

    def note(self, text):
        self.callout("note", text)

    def warning(self, text):
        self.callout("warning", text)

    def code(self, text):
        t = Table([[Preformatted(text, STYLES["code"])]], colWidths=[FRAME_W])
        t.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#eef1f8")),
            ("BOX", (0, 0), (-1, -1), 0.5, RULE),
            ("LEFTPADDING", (0, 0), (-1, -1), 9), ("TOPPADDING", (0, 0), (-1, -1), 6),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
        ]))
        self.items.append(KeepTogether([t, Spacer(1, 8)]))

    def table(self, header, rows, widths, swatches=None, keep=False):
        """widths are relative column widths; swatches: optional colour per row
        drawn as a bar in front of the first column."""
        total = sum(widths)
        col_w = [FRAME_W * w / total for w in widths]
        data = [[self._p(h, STYLES["cellb"]) for h in header]]
        for r in rows:
            data.append([self._p(str(c), STYLES["cell"]) for c in r])
        t = Table(data, colWidths=col_w, repeatRows=1)
        style = [
            ("BACKGROUND", (0, 0), (-1, 0), NAVY),
            ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, ZEBRA]),
            ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
            ("BOX", (0, 0), (-1, -1), 0.5, RULE),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
            ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6),
            ("TOPPADDING", (0, 0), (-1, -1), 4.2), ("BOTTOMPADDING", (0, 0), (-1, -1), 4.8),
        ]
        if swatches:
            for i, c in enumerate(swatches, start=1):
                if c:
                    style.append(("LINEBEFORE", (0, i), (0, i), 6, colors.HexColor(c)))
        t.setStyle(TableStyle(style))
        if keep:
            self.items.append(KeepTogether([t, Spacer(1, 10)]))
        else:
            self.items += [t, Spacer(1, 10)]

    # figures --------------------------------------------------------------------
    def prepare_image(self, src: Path, crop=None, max_px=1500) -> tuple[Path, int, int]:
        im = PILImage.open(src)
        if crop:
            im = im.crop(crop)
        im = im.convert("RGB")
        if im.width > max_px:
            im = im.resize((max_px, round(im.height * max_px / im.width)), PILImage.LANCZOS)
        out = self.build_dir / (src.stem.replace(" ", "_") + ("_crop" if crop else "") + ".jpg")
        im.save(out, "JPEG", quality=85, optimize=True, progressive=True)
        return out, im.width, im.height

    def figure(self, src: Path, caption: str, width_frac=1.0, crop=None):
        path, w, h = self.prepare_image(src, crop)
        self.fig += 1
        dw = FRAME_W * width_frac
        img = Image(str(path), width=dw, height=dw * h / w)
        frame = Table([[img]], colWidths=[dw + 2])
        frame.setStyle(TableStyle([("BOX", (0, 0), (-1, -1), 0.6, RULE), ("LEFTPADDING", (0, 0), (-1, -1), 1),
                                   ("RIGHTPADDING", (0, 0), (-1, -1), 1), ("TOPPADDING", (0, 0), (-1, -1), 1),
                                   ("BOTTOMPADDING", (0, 0), (-1, -1), 1)]))
        cap = self._p(f'<font name="{FONT_B}" color="#1d2c74">Figure {self.fig}.</font> {caption}',
                      STYLES["caption"])
        self.items.append(KeepTogether([Spacer(1, 4), frame, cap]))

    def spacer(self, h=6):
        self.items.append(Spacer(1, h))

    def pagebreak(self):
        self.items.append(PageBreak())


# --- glyph check ------------------------------------------------------------------------
def check_glyphs(texts):
    if FONT != "Body":
        return
    cmap = pdfmetrics.getFont("Body").face.charToGlyph
    missing = set()
    for t in texts:
        plain = re.sub(r"<[^>]+>", "", t)
        plain = plain.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&nbsp;", " ")
        missing |= {ch for ch in plain if ord(ch) > 127 and ord(ch) not in cmap}
    if missing:
        print("warning: glyphs missing in body font:", " ".join(sorted(missing)))


# --- main ----------------------------------------------------------------------------------
def main():
    out = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_OUT
    build_dir = Path(tempfile.mkdtemp(prefix="awt-manual-"))
    try:
        sys.dont_write_bytecode = True  # keep docs/manual free of __pycache__
        sys.path.insert(0, str(HERE))
        import content  # manual text

        story = Story(build_dir)
        cover, _, _ = story.prepare_image(SHOTS / "dashboard.png", max_px=1400)
        doc = ManualDoc(str(out), cover)

        # page 1: cover (drawn by draw_cover); page 2: contents
        story.items += [Spacer(1, 1), NextPageTemplate("body"), PageBreak()]
        story.items.append(Heading("Contents", STYLES["h1"], 0, "contents", "Contents", in_toc=False))
        story.items.append(ZoneStripe())
        story.items.append(TableOfContents(levelStyles=[STYLES["toc0"], STYLES["toc1"]], dotsMinLevel=0))

        content.build(story, dict(SHOTS=SHOTS, MOCKUPS=MOCKUPS, VERSION=VERSION, REPO=REPO))
        check_glyphs(story.text_seen)
        doc.multiBuild(story.items)
        print(f"wrote {out} ({out.stat().st_size / 1024:.0f} KiB, {doc.page} pages, {story.fig} figures)")
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
