"""Extract/verify the active corpus against the robot's exact raw SQLite bodies.

Default is check-only. --apply updates changed extracted documents and manifest
hash/word/byte fields, so a subsequent native build sees recrawled content.
Missing raw rows or articles that no longer meet the corpus contract are errors,
never silently replaced with historical clean text.
"""
import argparse
import csv
import gzip
import hashlib
import json
import sqlite3
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'lab2_crawler'))
from scoap3 import extract_body,extract_pdf_body,WORD
from raw_scoap3 import normalize_url


def extract(database,manifest,output,apply=False):
    connection=sqlite3.connect(database);connection.row_factory=sqlite3.Row
    with manifest.open(encoding='utf-8',newline='') as file:
        reader=csv.DictReader(file,delimiter='\t');fields=reader.fieldnames;rows=list(reader)
    missing=[row['record_id'] for row in rows if connection.execute('SELECT 1 FROM raw_documents WHERE url=?',(normalize_url(row[row['source_format']+'_url']),)).fetchone() is None]
    if missing:
        connection.close()
        raise RuntimeError('Raw database incomplete: '+str(len(missing))+' missing; examples '+str(missing[:10]))
    changed=[];provenance=[];staged=[]
    staging=Path(tempfile.mkdtemp(prefix='raw_extract_',dir=output.parent)) if apply else None
    for row in rows:
        url=normalize_url(row[row['source_format']+'_url'])
        raw=connection.execute('SELECT * FROM raw_documents WHERE url=?',(url,)).fetchone()
        if raw is None:missing.append(row['record_id']);continue
        source=gzip.decompress(raw['raw_gzip'])
        if len(source)!=raw['raw_bytes'] or hashlib.sha256(source).hexdigest()!=raw['sha256']:
            raise ValueError('Corrupt original source in database: '+row['record_id'])
        body=extract_pdf_body(source) if row['source_format']=='pdf' else extract_body(source)
        if body is None or len(WORD.findall(body))<3000:raise ValueError('Current source no longer meets 3000-word contract: '+row['record_id'])
        content=(' '.join(row['title'].split())+'\n'+body+'\n').encode()
        digest=hashlib.sha256(content).hexdigest()
        if digest!=row['sha256']:changed.append(row['record_id'])
        if apply:
            if digest!=row['sha256']:
                (staging/row['file']).write_bytes(gzip.compress(content,compresslevel=6,mtime=0));staged.append(row['file'])
            row['sha256']=digest;row['words']=str(len(WORD.findall(body)));row['source_bytes']=str(len(source))
        provenance.append({'record_id':row['record_id'],'source_url':url,'source_sha256':raw['sha256'],
                           'source_bytes':len(source),'text_sha256':digest,'crawled_at':raw['crawled_at'],'checked_at':raw['checked_at']})
    connection.close()
    summary={'documents':len(rows),'verified_sources':len(provenance),'missing_raw_documents':missing,
             'changed_clean_documents':changed,'applied':apply,'raw_provenance':provenance}
    if missing:raise RuntimeError('Raw database incomplete: '+str(len(missing))+' missing; examples '+str(missing[:10]))
    if apply:
        for name in staged:(staging/name).replace(output/name)
        staging.rmdir()
        temporary=manifest.with_suffix('.tmp')
        with temporary.open('w',encoding='utf-8',newline='') as file:
            writer=csv.DictWriter(file,fields,delimiter='\t',lineterminator='\n');writer.writeheader();writer.writerows(rows)
        temporary.replace(manifest)
    return summary


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database',type=Path)
    parser.add_argument('--manifest',type=Path,default=ROOT/'lab1_corpus/data/provenance/scoap3.tsv')
    parser.add_argument('--output',type=Path,default=ROOT/'lab1_corpus/data/text/scoap3')
    parser.add_argument('--report',type=Path,default=ROOT/'lab1_corpus/results/raw_validation.json')
    parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    result=extract(args.database,args.manifest,args.output,args.apply)
    args.report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:value for key,value in result.items() if key!='raw_provenance'},ensure_ascii=False,indent=2))
