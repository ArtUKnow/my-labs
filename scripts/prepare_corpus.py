
import argparse
import csv
import gzip
import hashlib
import html
import json
import re
import shutil
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / 'lab1_corpus' / 'data'
DOI = re.compile(r'^10\.\d{4,9}/\S+$')
WORD = re.compile(r"\b[A-Za-z][A-Za-z'\-]*\b")


def source_id(journal):
    return 'journal_' + re.sub(r'[^a-z0-9]+', '_', journal.lower()).strip('_')


def repair_doi(row):
    if DOI.fullmatch(row['doi']):
        return None
    for url in [row.get('xml_url', ''), row.get('pdf_url', '')]:
        match = re.search(r'(10\.\d{4,9}/.+?)\.(?:xml|pdf)$', urllib.parse.unquote(url))
        if not match:
            continue
        proposed = match.group(1)
        with urllib.request.urlopen(urllib.request.Request(row['xml_url'], headers={'User-Agent':'MAI-IR-coursework/1.0'}), timeout=30) as response:
            xml = response.read()
        if proposed.encode() not in xml:
            raise ValueError('DOI not corroborated in original XML: ' + row['record_id'])
        return {'record_id':row['record_id'], 'old':row['doi'], 'new':proposed,
                'evidence_url':row['xml_url'], 'evidence_sha256':hashlib.sha256(xml).hexdigest()}
    raise ValueError('No corroborated DOI for ' + row['record_id'])


def prepare(apply=False, validate_text=True):
    manifest = DATA / 'provenance' / 'scoap3.tsv'
    with manifest.open(encoding='utf-8', newline='') as file:
        reader = csv.DictReader(file, delimiter='\t'); fields = reader.fieldnames; rows = list(reader)
    groups = defaultdict(list)
    for row in rows:
        groups[row['sha256']].append(row)
    removed = []
    for group in groups.values():
        group.sort(key=lambda row:(not bool(DOI.fullmatch(row['doi'])), int(row['record_id'])))
        removed.extend(group[1:])
    removed_ids = {row['record_id'] for row in removed}
    active = sorted((dict(row) for row in rows if row['record_id'] not in removed_ids), key=lambda row:int(row['record_id']))
    repairs = []
    original_dois = {row['record_id']:row['doi'] for row in active}
    for row in active:
        repair = repair_doi(row)
        if repair:
            repairs.append(repair); row['doi'] = repair['new']
    doi_groups = defaultdict(list)
    for row in active:doi_groups[row['doi'].lower()].append(row)
    active = []
    for group in doi_groups.values():
        group.sort(key=lambda row:(not bool(DOI.fullmatch(original_dois[row['record_id']])),int(row['record_id'])))
        active.append(group[0]);removed.extend(group[1:])
    active.sort(key=lambda row:int(row['record_id']))
    minimum = None
    for row in active:
        if int(row['words']) < 3000:
            raise ValueError('Short article ' + row['record_id'])
        if validate_text:
            path = DATA / 'text' / 'scoap3' / row['file']
            with gzip.open(path, 'rb') as file: content=file.read()
            if hashlib.sha256(content).hexdigest() != row['sha256']:
                raise ValueError('Hash mismatch ' + row['record_id'])
            body=content.decode('utf-8').split('\n',1)[1]
            actual=len(WORD.findall(body)); minimum=actual if minimum is None else min(minimum,actual)
            if actual != int(row['words']) or actual < 3000:
                raise ValueError('Word count mismatch ' + row['record_id'])
    if len({row['doi'].lower() for row in active}) != len(active):
        raise ValueError('Duplicate DOI after normalization')
    if len({row['sha256'] for row in active}) != len(active):
        raise ValueError('Duplicate text remains')
    sources = [{'source_id':source_id(journal), 'journal':journal, 'documents':count,
                'repository':'https://repo.scoap3.org/',
                'source_meaning':'Distinct journal publication; SCOAP3 is the common delivery repository'}
               for journal,count in sorted(Counter(row['journal'] for row in active).items())]
    summary = {'apply':apply, 'original_records':len(rows), 'documents':len(active),
               'minimum_body_words':minimum, 'removed_duplicates':removed,
               'doi_repairs':repairs, 'sources':sources,
               'group_uniqueness':'Cannot be verified from this repository; requires comparison with classmates.'}
    if not apply:
        return summary
    archive=DATA/'archive_duplicates'; archive.mkdir(exist_ok=True)
    if removed:
        with (archive/'removed_rows.json').open('w',encoding='utf-8') as file: json.dump(removed,file,ensure_ascii=False,indent=2)
        for row in removed:
            path=DATA/'text'/'scoap3'/row['file']
            shutil.move(str(path),str(archive/row['file']))
            token=DATA/'tokens'/'scoap3'/row['file']
            if token.exists(): shutil.move(str(token),str(archive/(row['file']+'.tokens.gz')))
    if not (archive/'manifest_before.tsv').exists(): shutil.copy2(manifest,archive/'manifest_before.tsv')
    temporary=manifest.with_suffix('.tmp')
    with temporary.open('w',encoding='utf-8',newline='') as file:
        writer=csv.DictWriter(file,fields,delimiter='\t',lineterminator='\n');writer.writeheader();writer.writerows(active)
    temporary.replace(manifest)
    with (DATA/'native_documents.tsv').open('w',encoding='utf-8',newline='') as file, (DATA/'provenance'/'documents.tsv').open('w',encoding='utf-8',newline='') as portable:
        native_writer=csv.writer(file,delimiter='\t',lineterminator='\n',quoting=csv.QUOTE_NONE,quotechar=None);portable_writer=csv.writer(portable,delimiter='\t',lineterminator='\n',quoting=csv.QUOTE_NONE,quotechar=None)
        header=['id','path','url','title','journal'];native_writer.writerow(header);portable_writer.writerow(header)
        for index,row in enumerate(active):
            title=' '.join(html.unescape(re.sub(r'<[^>]+>','',row['title'])).split())
            path=DATA/'text'/'scoap3'/row['file']
            tail=[row['url'],title,row['journal']]
            native_writer.writerow([index,str(path.resolve()),*tail]);portable_writer.writerow([index,str(path.relative_to(ROOT)),*tail])
    (DATA/'provenance'/'sources.json').write_text(json.dumps(sources,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (ROOT/'lab1_corpus'/'results'/'corpus_validation.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return summary


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--apply',action='store_true');parser.add_argument('--skip-text-validation',action='store_true');args=parser.parse_args()
    result=prepare(args.apply,not args.skip_text_validation)
    print(json.dumps({key:value for key,value in result.items() if key not in ('removed_duplicates','sources')},ensure_ascii=False,indent=2))
