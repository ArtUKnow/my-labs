import os
import sys
from html.parser import HTMLParser


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "data", "raw")
TEXT_DIR = os.path.join(BASE_DIR, "data", "sample_text")

SKIP_TAGS = ["script", "style", "table", "sup", "nav", "footer", "header"]
TEXT_TAGS = ["p", "h1", "h2", "h3", "h4", "li", "dd", "dt", "blockquote"]


class TextExtractor(HTMLParser):
    def __init__(self):
        HTMLParser.__init__(self)
        self.parts = []
        self.title = ""
        self.skip_depth = 0
        self.text_depth = 0
        self.in_title = False
        self.in_content = False
        self.content_depth = 0
        self.div_depth = 0

    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)
        if tag == "title":
            self.in_title = True
        if tag == "div":
            self.div_depth += 1
            if attrs_dict.get("id") == "mw-content-text":
                self.in_content = True
                self.content_depth = self.div_depth
        if tag in SKIP_TAGS:
            self.skip_depth += 1
        if tag in TEXT_TAGS:
            self.text_depth += 1
        if tag == "br" and self.text_depth > 0:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False
        if tag == "div":
            if self.in_content and self.div_depth == self.content_depth:
                self.in_content = False
            self.div_depth -= 1
        if tag in SKIP_TAGS and self.skip_depth > 0:
            self.skip_depth -= 1
        if tag in TEXT_TAGS and self.text_depth > 0:
            self.text_depth -= 1
            self.parts.append("\n")

    def handle_data(self, data):
        if self.in_title:
            self.title += data
        if self.skip_depth > 0:
            return
        if self.text_depth == 0:
            return
        if not self.in_content and self.title != "":
            return
        self.parts.append(data)

    def get_text(self):
        text = "".join(self.parts)
        lines = []
        for line in text.split("\n"):
            line = " ".join(line.split())
            if line != "":
                lines.append(line)
        return "\n".join(lines)


def extract_text_from_html(html):
    extractor = TextExtractor()
    extractor.feed(html)
    return extractor.get_text()


def extract_title(html):
    extractor = TextExtractor()
    extractor.feed(html)
    title = extractor.title.strip()
    for suffix in [" — Википедия", " - Simple English Wikipedia, the free encyclopedia", " - Wikipedia"]:
        if title.endswith(suffix):
            title = title[: -len(suffix)]
    return title


def process_all(raw_dir, text_dir):
    count = 0
    for source_name in sorted(os.listdir(raw_dir)):
        source_dir = os.path.join(raw_dir, source_name)
        if not os.path.isdir(source_dir):
            continue
        out_dir = os.path.join(text_dir, source_name)
        os.makedirs(out_dir, exist_ok=True)
        for file_name in sorted(os.listdir(source_dir)):
            if not file_name.endswith(".html"):
                continue
            path = os.path.join(source_dir, file_name)
            html = open(path, encoding="utf-8").read()
            text = extract_text_from_html(html)
            title = extract_title(html)
            out_path = os.path.join(out_dir, file_name.replace(".html", ".txt"))
            f = open(out_path, "w", encoding="utf-8")
            f.write(title + "\n" + text)
            f.close()
            count += 1
    return count


def main():
    raw_dir = RAW_DIR
    text_dir = TEXT_DIR
    if len(sys.argv) > 2:
        raw_dir = sys.argv[1]
        text_dir = sys.argv[2]
    count = process_all(raw_dir, text_dir)
    print("extracted documents:", count)


if __name__ == "__main__":
    main()
