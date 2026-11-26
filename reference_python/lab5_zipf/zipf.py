import os
import sys
import json
import math
import gzip


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TOKENS_DIR = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "primary_tokens")
RESULTS_DIR = os.path.join(BASE_DIR, "results")


def count_frequencies(tokens_dir):
    frequencies = {}
    for source in sorted(os.listdir(tokens_dir)):
        source_dir = os.path.join(tokens_dir, source)
        if not os.path.isdir(source_dir):
            continue
        for name in sorted(os.listdir(source_dir)):
            if not name.endswith((".txt", ".txt.gz")):
                continue
            opener = gzip.open if name.endswith(".gz") else open
            with opener(os.path.join(source_dir, name), "rt", encoding="utf-8") as file:
                for line in file:
                    token = line.strip()
                    if token != "":
                        frequencies[token] = frequencies.get(token, 0) + 1
    return frequencies


def count_from_tokens(tokens):
    frequencies = {}
    for token in tokens:
        frequencies[token] = frequencies.get(token, 0) + 1
    return frequencies


def ranked_list(frequencies):
    items = list(frequencies.items())
    items.sort(key=lambda item: (-item[1], item[0]))
    return items


def fit_zipf(ranked):
    n = len(ranked)
    sum_x = 0.0
    sum_y = 0.0
    sum_xx = 0.0
    sum_xy = 0.0
    for i in range(n):
        x = math.log(i + 1)
        y = math.log(ranked[i][1])
        sum_x += x
        sum_y += y
        sum_xx += x * x
        sum_xy += x * y
    slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x)
    intercept = (sum_y - slope * sum_x) / n
    return math.exp(intercept), -slope


def zipf_error(ranked, c, s, b):
    error = 0.0
    for i in range(len(ranked)):
        predicted = math.log(c) - s * math.log(i + 1 + b)
        actual = math.log(ranked[i][1])
        error += (predicted - actual) ** 2
    return error / len(ranked)


def fit_mandelbrot(ranked):
    best = None
    best_error = None
    n = len(ranked)
    log_frequencies = [math.log(freq) for _, freq in ranked]
    sum_y = sum(log_frequencies)
    sum_yy = sum(y * y for y in log_frequencies)
    for b10 in range(0, 101, 5):
        b = b10 / 10.0
        sum_x = 0.0
        sum_xx = 0.0
        sum_xy = 0.0
        for i, y in enumerate(log_frequencies):
            x = math.log(i + 1 + b)
            sum_x += x
            sum_xx += x * x
            sum_xy += x * y
        slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x)
        intercept = (sum_y - slope * sum_x) / n
        error = max(0.0, (sum_yy - intercept * sum_y - slope * sum_xy) / n)
        if best_error is None or error < best_error:
            best_error = error
            best = (math.exp(intercept), -slope, b)
    return best[0], best[1], best[2], best_error


def pick_points(ranked, c, s, mc, ms, mb):
    points = []
    step = 1
    i = 0
    while i < len(ranked):
        rank = i + 1
        points.append({
            "rank": rank,
            "freq": ranked[i][1],
            "zipf": round(c / (rank ** s), 3),
            "mandelbrot": round(mc / ((rank + mb) ** ms), 3),
        })
        if rank >= 100:
            step = max(1, int(rank / 20))
        i += step
    return points


def analyze(frequencies):
    ranked = ranked_list(frequencies)
    c, s = fit_zipf(ranked)
    simple_c = ranked[0][1]
    mc, ms, mb, m_error = fit_mandelbrot(ranked)
    total = 0
    for term, freq in ranked:
        total += freq
    hapax = 0
    for term, freq in ranked:
        if freq == 1:
            hapax += 1
    result = {
        "unique_terms": len(ranked),
        "total_tokens": total,
        "hapax_legomena": hapax,
        "hapax_share": round(hapax / len(ranked), 4),
        "zipf": {"c": round(c, 3), "s": round(s, 4), "error": round(zipf_error(ranked, c, s, 0), 5)},
        "zipf_simple": {"c": simple_c, "s": 1.0, "error": round(zipf_error(ranked, simple_c, 1.0, 0), 5)},
        "mandelbrot": {"c": round(mc, 3), "s": round(ms, 4), "b": round(mb, 2), "error": round(m_error, 5)},
        "top_terms": [{"term": t, "freq": f} for t, f in ranked[:30]],
        "points": pick_points(ranked, c, s, mc, ms, mb),
    }
    return result


def main():
    tokens_dir = TOKENS_DIR
    if len(sys.argv) > 1:
        tokens_dir = sys.argv[1]
    frequencies = count_frequencies(tokens_dir)
    result = analyze(frequencies)
    print("unique terms:", result["unique_terms"])
    print("total tokens:", result["total_tokens"])
    print("hapax legomena:", result["hapax_legomena"], "(%.1f%%)" % (result["hapax_share"] * 100))
    print("zipf fit: c=%.1f s=%.3f error=%.4f" % (result["zipf"]["c"], result["zipf"]["s"], result["zipf"]["error"]))
    print("zipf simple: c=%d s=1 error=%.4f" % (result["zipf_simple"]["c"], result["zipf_simple"]["error"]))
    print("mandelbrot: c=%.1f s=%.3f b=%.1f error=%.4f" % (result["mandelbrot"]["c"], result["mandelbrot"]["s"], result["mandelbrot"]["b"], result["mandelbrot"]["error"]))
    print("top terms:")
    for item in result["top_terms"][:15]:
        print("  %s\t%d" % (item["term"], item["freq"]))
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "zipf.json"), "w", encoding="utf-8")
    json.dump(result, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
