
import csv
import gzip
import hashlib
import html
import json
from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path
import re

from search import SimpleSearch, STOP_WORDS

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / 'lab1_corpus' / 'data'
QUERY_DIR = DATA / 'queries' / 'scoap3'
RUNS_PATH = ROOT / 'lab4_stemmer' / 'results' / 'corpus_runs.json'


def manifest_rows():
    with (DATA / 'provenance' / 'scoap3.tsv').open(newline='', encoding='utf-8') as file:
        return list(csv.DictReader(file, delimiter='\t'))


def prepare_queries(rows, per_journal=4):

    groups = defaultdict(list)
    for row in rows:
        title = html.unescape(re.sub(r'<[^>]+>', '', row['title']))
        words = re.findall(r"[A-Za-z][A-Za-z'-]*", title)
        if len(words) >= 6:
            groups[row['journal']].append((row, title, words))
    QUERY_DIR.mkdir(parents=True, exist_ok=True)
    queries, judgments, evidence = [], [], []
    for journal in sorted(groups):
        choices = sorted(groups[journal], key=lambda item: hashlib.sha256(item[0]['doi'].encode()).hexdigest())
        for row, title, words in choices[:per_journal]:
            qid = 'q' + str(len(queries) + 1)
            query = ' '.join(words[:8])
            doc_id = 'scoap3/' + row['file'].removesuffix('.txt.gz')
            queries.append((qid, query))
            judgments.append((qid, doc_id, 3))
            evidence.append({'query_id': qid, 'query': query, 'target': doc_id,
                             'doi': row['doi'], 'title': title, 'journal': journal,
                             'url': row['url']})
    (QUERY_DIR / 'queries.txt').write_text(''.join(f'{q}\t{t}\n' for q, t in queries), encoding='utf-8')
    (QUERY_DIR / 'qrels.txt').write_text(''.join(f'{q}\t{d}\t{g}\n' for q, d, g in judgments), encoding='utf-8')
    protocol = {'task': 'known_item_title_retrieval', 'selection': 'four articles per journal ordered by SHA256(DOI), titles with at least six words',
                'query_rule': 'first eight alphabetic title words',
                'relevance': 'grade 3 for the specified original article identified by DOI; all other articles are non-targets for this identity task',
                'limitations': 'Automatically derived identity labels, not independent human topical relevance judgments. Titles occur in the indexed documents. Metrics do not establish thematic search quality.',
                'corpus_documents': len(rows), 'queries': evidence}
    (QUERY_DIR / 'protocol.json').write_text(json.dumps(protocol, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return queries


def token_paths(rows):
    directory = DATA / 'primary_tokens' / 'scoap3'
    paths = [directory / row['file'] for row in rows]
    expected = {path.name for path in paths}
    actual = {p.name for p in directory.glob('*.txt.gz')}
    if actual != expected:
        raise ValueError('Primary tokens do not match the full corpus; run lab3_tokenizer/tokenize_corpus.py')
    return paths


def fingerprint(rows, paths):
    digest = hashlib.sha256()
    for path in [DATA / 'provenance' / 'scoap3.tsv', QUERY_DIR / 'queries.txt', QUERY_DIR / 'qrels.txt',
                 Path(__file__), Path(__file__).with_name('search.py'),
                 Path(__file__).with_name('lemmatizer.py'), Path(__file__).with_name('stemmer.py')]:
        digest.update(path.read_bytes())
    for row, path in zip(rows, paths):
        info = path.stat()
        digest.update(f"{row['file']}:{info.st_size}:{info.st_mtime_ns}".encode())
    return digest.hexdigest()


def read_counts(path):
    with gzip.open(path, 'rt', encoding='utf-8') as file:
        return Counter(file.read().splitlines())


def retrieve(rows, paths, queries, k=5, modes=('raw', 'stem', 'lemma')):
    vocabulary = Counter()
    for index, path in enumerate(paths, 1):
        vocabulary.update(read_counts(path))
        if index % 5000 == 0:
            print('vocabulary documents', index, flush=True)
    engines = {mode: SimpleSearch([], mode, vocabulary) for mode in modes}
    query_terms = {mode: {qid: engine.prepare(text) for qid, text in queries} for mode, engine in engines.items()}
    wanted = {mode: {term for terms in by_query.values() for term in terms} for mode, by_query in query_terms.items()}
    mappings, sizes = {}, {}
    for mode, engine in engines.items():
        normalize = lru_cache(maxsize=None)(engine.normalize)
        mapping, all_terms = {}, set()
        for word in vocabulary:
            if word in STOP_WORDS:
                continue
            term = normalize(word)
            all_terms.add(term)
            if term in wanted[mode]:
                mapping[word] = term
        mappings[mode], sizes[mode] = mapping, len(all_terms)
        print('normalization', mode, 'terms', sizes[mode], flush=True)
    rankings = {mode: {qid: [] for qid, _ in queries} for mode in modes}
    for index, (row, path) in enumerate(zip(rows, paths), 1):
        counts = read_counts(path)
        length = sum(count for word, count in counts.items() if word not in STOP_WORDS)
        if not length:
            continue
        doc_id = 'scoap3/' + row['file'].removesuffix('.txt.gz')
        for mode in modes:
            found = Counter()
            mapping = mappings[mode]
            for word, count in counts.items():
                if word in mapping:
                    found[mapping[word]] += count
            for qid, terms in query_terms[mode].items():
                matched = sum(term in found for term in terms)
                if matched:
                    score = sum(found.get(term, 0) / length * 1000 for term in terms)
                    rankings[mode][qid].append((matched, score, doc_id))
        if index % 5000 == 0:
            print('searched documents', index, flush=True)
    runs = {}
    for mode in modes:
        runs[mode] = {}
        for qid, ranked in rankings[mode].items():
            ranked.sort(key=lambda item: (-item[0], -item[1], item[2]))
            runs[mode][qid] = [item[2] for item in ranked[:k]]
    return runs, sizes


def ensure_runs():
    rows = manifest_rows()
    queries = prepare_queries(rows)
    paths = token_paths(rows)
    key = fingerprint(rows, paths)
    if RUNS_PATH.exists():
        cached = json.loads(RUNS_PATH.read_text())
        if cached.get('fingerprint') == key:
            return cached
    runs, sizes = retrieve(rows, paths, queries)
    result = {'fingerprint': key, 'evaluation_documents': len(rows), 'k': 5,
              'task': 'known_item_title_retrieval', 'query_count': len(queries),
              'terms_in_index': sizes, 'runs': runs}
    RUNS_PATH.parent.mkdir(exist_ok=True)
    RUNS_PATH.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    print('evaluated', ensure_runs()['evaluation_documents'], 'documents')
