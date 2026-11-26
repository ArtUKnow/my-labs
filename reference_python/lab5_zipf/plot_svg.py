import os
import sys
import json
import math


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(BASE_DIR, "results")

WIDTH = 800
HEIGHT = 500
PADDING = 60


def make_scale(values, size, padding):
    low = math.log10(min(values))
    high = math.log10(max(values))
    if high == low:
        high = low + 1

    def scale(value):
        return padding + (math.log10(value) - low) / (high - low) * (size - 2 * padding)

    return scale, low, high


def polyline(points, scale_x, scale_y, key, color, dash=""):
    coords = []
    for p in points:
        if p[key] <= 0:
            continue
        x = scale_x(p["rank"])
        y = HEIGHT - scale_y(p[key])
        coords.append("%.1f,%.1f" % (x, y))
    extra = ""
    if dash != "":
        extra = ' stroke-dasharray="%s"' % dash
    return '<polyline fill="none" stroke="%s" stroke-width="2"%s points="%s"/>' % (color, extra, " ".join(coords))


def make_svg(data):
    points = data["points"]
    ranks = [p["rank"] for p in points]
    freqs = [p["freq"] for p in points] + [p["zipf"] for p in points] + [p["mandelbrot"] for p in points]
    freqs = [f for f in freqs if f > 0]
    scale_x, x_low, x_high = make_scale(ranks, WIDTH, PADDING)
    scale_y, y_low, y_high = make_scale(freqs, HEIGHT, PADDING)
    parts = []
    parts.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" font-family="sans-serif" font-size="12">' % (WIDTH, HEIGHT))
    parts.append('<rect width="100%" height="100%" fill="white"/>')
    for e in range(int(math.floor(x_low)), int(math.ceil(x_high)) + 1):
        x = scale_x(10 ** e)
        parts.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#ddd"/>' % (x, PADDING, x, HEIGHT - PADDING))
        parts.append('<text x="%.1f" y="%d" text-anchor="middle">10^%d</text>' % (x, HEIGHT - PADDING + 20, e))
    for e in range(int(math.floor(y_low)), int(math.ceil(y_high)) + 1):
        y = HEIGHT - scale_y(10 ** e)
        parts.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#ddd"/>' % (PADDING, y, WIDTH - PADDING, y))
        parts.append('<text x="%d" y="%.1f" text-anchor="end">10^%d</text>' % (PADDING - 8, y + 4, e))
    parts.append(polyline(points, scale_x, scale_y, "freq", "#2563eb"))
    parts.append(polyline(points, scale_x, scale_y, "zipf", "#dc2626", "6,4"))
    parts.append(polyline(points, scale_x, scale_y, "mandelbrot", "#16a34a", "2,3"))
    parts.append('<text x="%d" y="%d" text-anchor="middle">ранг (log)</text>' % (WIDTH / 2, HEIGHT - 15))
    parts.append('<text x="15" y="%d" transform="rotate(-90 15 %d)" text-anchor="middle">частота (log)</text>' % (HEIGHT / 2, HEIGHT / 2))
    parts.append('<text x="%d" y="30" fill="#2563eb">— корпус</text>' % (WIDTH - 260))
    parts.append('<text x="%d" y="30" fill="#dc2626">- - Ципф</text>' % (WIDTH - 180))
    parts.append('<text x="%d" y="30" fill="#16a34a">··· Мандельброт</text>' % (WIDTH - 110))
    parts.append("</svg>")
    return "\n".join(parts)


def main():
    path = os.path.join(RESULTS_DIR, "zipf.json")
    if len(sys.argv) > 1:
        path = sys.argv[1]
    data = json.load(open(path, encoding="utf-8"))
    svg = make_svg(data)
    out_path = os.path.join(os.path.dirname(path), "zipf.svg")
    f = open(out_path, "w", encoding="utf-8")
    f.write(svg)
    f.close()
    print("saved", out_path)


if __name__ == "__main__":
    main()
