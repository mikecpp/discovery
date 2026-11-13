#!/usr/bin/env python3
"""Render a small Markdown subset (headings, paragraphs, lists, fenced code,
pipe tables, inline code/bold/italic) to PDF with reportlab."""
import re
import sys
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (ListFlowable, ListItem, PageBreak, Paragraph, Preformatted,
                                SimpleDocTemplate, Spacer, Table, TableStyle, KeepTogether)

ACCENT = colors.HexColor("#1f5fa8")
CODE_BG = colors.HexColor("#f3f5f8")
RULE = colors.HexColor("#d5dbe3")

base = getSampleStyleSheet()
S = {
    "title": ParagraphStyle("title", parent=base["Title"], fontName="Helvetica-Bold", fontSize=24,
                            leading=30, alignment=TA_LEFT, textColor=ACCENT, spaceAfter=4),
    "h2": ParagraphStyle("h2", parent=base["Heading1"], fontName="Helvetica-Bold", fontSize=15,
                         leading=19, textColor=ACCENT, spaceBefore=16, spaceAfter=6, keepWithNext=1),
    "h3": ParagraphStyle("h3", parent=base["Heading2"], fontName="Helvetica-Bold", fontSize=12,
                         leading=15, spaceBefore=10, spaceAfter=4, keepWithNext=1),
    "body": ParagraphStyle("body", parent=base["Normal"], fontName="Helvetica", fontSize=10,
                           leading=14, spaceAfter=6),
    "cell": ParagraphStyle("cell", parent=base["Normal"], fontName="Helvetica", fontSize=8.5, leading=11),
    "cellhead": ParagraphStyle("cellhead", parent=base["Normal"], fontName="Helvetica-Bold",
                               fontSize=8.5, leading=11, textColor=colors.white),
    "code": ParagraphStyle("code", parent=base["Code"], fontName="Courier", fontSize=8.5, leading=11,
                           backColor=CODE_BG, borderColor=RULE, borderWidth=0.5, borderPadding=6,
                           leftIndent=0, spaceBefore=4, spaceAfter=10),
}


def inline(text):
    text = escape(text)
    text = re.sub(r"`([^`]+)`", r'<font face="Courier" size="8.5" backColor="#f3f5f8">\1</font>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<![\w*])\*([^*]+)\*(?![\w*])", r"<i>\1</i>", text)
    return text


def table(rows, width):
    cells = [row.strip().strip("|").split("|") for row in rows if not re.match(r"^\s*\|?\s*:?-{2,}", row)]
    cells = [[c.strip() for c in row] for row in cells]
    ncol = max(len(r) for r in cells)
    cells = [r + [""] * (ncol - len(r)) for r in cells]
    data = [[Paragraph(inline(c), S["cellhead"]) for c in cells[0]]]
    data += [[Paragraph(inline(c), S["cell"]) for c in row] for row in cells[1:]]
    lengths = [max(len(r[i]) for r in cells) for i in range(ncol)]
    weights = [min(max(l, 8), 60) for l in lengths]
    total = sum(weights)
    widths = [width * w / total for w in weights]
    t = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f7f9fb")]),
        ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
        ("BOX", (0, 0), (-1, -1), 0.6, RULE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]))
    return t


def render(md_path, pdf_path):
    lines = open(md_path, encoding="utf-8").read().splitlines()
    doc = SimpleDocTemplate(pdf_path, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm,
                            topMargin=18 * mm, bottomMargin=18 * mm, title="", author="")
    width = A4[0] - 40 * mm
    story, title = [], None
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            block = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                block.append(lines[i])
                i += 1
            story.append(Preformatted("\n".join(block), S["code"]))
        elif line.startswith("# "):
            title = line[2:].strip()
            story.append(Paragraph(inline(title), S["title"]))
        elif line.startswith("## "):
            story.append(Paragraph(inline(line[3:]), S["h2"]))
        elif line.startswith("### "):
            story.append(Paragraph(inline(line[4:]), S["h3"]))
        elif line.lstrip().startswith("|"):
            rows = []
            while i < len(lines) and lines[i].lstrip().startswith("|"):
                rows.append(lines[i])
                i += 1
            story.append(table(rows, width))
            story.append(Spacer(1, 8))
            continue
        elif re.match(r"^(\s*)([-*]|\d+\.)\s+", line):
            items, ordered = [], bool(re.match(r"^\s*\d+\.", line))
            first = int(re.match(r"^\s*(\d+)\.", line).group(1)) if ordered else None
            while i < len(lines) and re.match(r"^(\s*)([-*]|\d+\.)\s+", lines[i]):
                text = re.sub(r"^(\s*)([-*]|\d+\.)\s+", "", lines[i])
                i += 1
                while i < len(lines) and lines[i].startswith("   ") and lines[i].strip():
                    text += " " + lines[i].strip()
                    i += 1
                items.append(ListItem(Paragraph(inline(text), S["body"]), leftIndent=12))
            if ordered:
                story.append(ListFlowable(items, bulletType="1", bulletFormat="%s.", start=first,
                                          leftIndent=14, bulletFontSize=9))
            else:
                story.append(ListFlowable(items, bulletType="bullet", leftIndent=14, bulletFontSize=9))
            continue
        elif line.strip():
            para = [line.strip()]
            while i + 1 < len(lines) and lines[i + 1].strip() and not re.match(r"^(#|```|\s*\||\s*[-*]\s|\s*\d+\.\s)", lines[i + 1]):
                i += 1
                para.append(lines[i].strip())
            story.append(Paragraph(inline(" ".join(para)), S["body"]))
        i += 1

    def footer(canvas, doc_):
        canvas.saveState()
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(colors.grey)
        canvas.drawString(20 * mm, 11 * mm, title or "")
        canvas.drawRightString(A4[0] - 20 * mm, 11 * mm, "Page %d" % doc_.page)
        canvas.setStrokeColor(RULE)
        canvas.line(20 * mm, 14 * mm, A4[0] - 20 * mm, 14 * mm)
        canvas.restoreState()

    doc.title = title or ""
    doc.build(story, onFirstPage=footer, onLaterPages=footer)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: md2pdf.py INPUT.md OUTPUT.pdf")
    render(sys.argv[1], sys.argv[2])
