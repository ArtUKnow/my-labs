
import argparse
import csv
import hashlib
import html
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parent.parent


def run(binary, *arguments, input=None):
    command=[str(binary), *map(str,arguments)]
    completed=subprocess.run(command,input=input,text=True,capture_output=True)
    if completed.returncode:raise RuntimeError(' '.join(command)+'\n'+completed.stderr)
    return completed.stdout


def evaluate(binary,manifest,index_base,output):
    with manifest.open(newline='',encoding='utf-8') as file:rows=list(csv.DictReader(file,delimiter='\t'))
    groups={}
    for row in rows:
        words=re.findall(r"[A-Za-z][A-Za-z'-]*",html.unescape(re.sub('<[^>]+>','',row['title'])))
        if len(words)>=6:groups.setdefault(row['journal'],[]).append((row,words))
    queries=[]
    for journal in sorted(groups):
        choices=sorted(groups[journal],key=lambda item:hashlib.sha256(item[0]['url'].encode()).hexdigest())
        for row,words in choices[:4]:queries.append({'query_id':'q'+str(len(queries)+1),'query':' '.join(words[:8]),'target':int(row['id']),'title':row['title'],'url':row['url'],'journal':journal})
    output.mkdir(parents=True,exist_ok=True)
    protocol={'task':'known_item_title_retrieval','corpus_documents':len(rows),'selection':'four per journal by SHA256(source URL), at least six title words','query_rule':'first eight title words','labels':'one original target article per query, identified by source record; other documents are non-targets in this identity task','limitations':'Automatic identity labels, not independent human topical judgments. Titles occur in indexed documents. Search uses all corpus documents. Boolean results ordered by document ID; no TF-IDF ranking is claimed.','queries':queries}
    (output/'protocol.json').write_text(json.dumps(protocol,ensure_ascii=False,indent=2)+'\n')
    qrels=output/'qrels.tsv';qrels.write_text(''.join(f"{q['query_id']}\t{q['target']}\t3\n" for q in queries))
    queries_path=output/'queries.tsv';queries_path.write_text(''.join(f"{q['query_id']}\t{q['query']}\n" for q in queries))
    systems=[]
    for mode in ['raw','stem','lemma']:
        index=index_base if mode=='stem' else index_base.with_name(index_base.name+'_'+mode)
        if not index.is_dir():raise FileNotFoundError(f'Build {mode} index first: {index}')
        responses=[json.loads(line) for line in run(binary,'search',index,'--json','--limit','5',input=''.join(q['query']+'\n' for q in queries)).splitlines()]
        if len(responses)!=len(queries):raise ValueError('C++ search response count does not match queries')
        run_path=output/(mode+'_run.tsv');lines=[]
        for query,response in zip(queries,responses):
            if 'error' in response:raise RuntimeError(response['error'])
            ids=response.get('ids',[d['id'] for d in response['documents']])
            for rank,doc_id in enumerate(ids,1):lines.append(f"{query['query_id']}\t{doc_id}\t{rank}\n")
        run_path.write_text(''.join(lines))
        metrics=json.loads(run(binary,'metrics',qrels,run_path,'5'))
        metrics['name']=mode;metrics['results']=responses;systems.append(metrics)
    report={'implementation':'C++ without STL','evaluation_documents':len(rows),'query_count':len(queries),'task':protocol['task'],'k':5,'systems':systems}
    (output/'metrics.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    quality={'implementation':report['implementation'],'evaluation_documents':len(rows),'query_count':len(queries),'task':protocol['task'],'k':5,'modes':[{'mode':s['name'],'avg_precision':s['average']['p'],'avg_recall':s['average']['r'],'avg_ndcg':s['average']['ndcg'],'queries':s['queries']} for s in systems]}
    (ROOT/'lab4_stemmer/results/quality.json').write_text(json.dumps(quality,ensure_ascii=False,indent=2)+'\n')
    ranked_rows=[json.loads(line) for line in run(binary,'rank',manifest,queries_path,'stem').splitlines()]
    if len(ranked_rows)!=len(queries) or any(row['corpus_documents']!=len(rows) for row in ranked_rows):
        raise ValueError('Ranked C++ search did not evaluate the full corpus')
    comparisons=[]
    for name in ['boolean','tfidf','bm25']:
        method_run=output/(name+'_baseline_run.tsv')
        method_run.write_text(''.join(f"{query['query_id']}\t{doc_id}\t{rank}\n" for query,response in zip(queries,ranked_rows) for rank,doc_id in enumerate(response[name],1)))
        cutoffs={}
        for cutoff in [1,3,5]:cutoffs[str(cutoff)]=json.loads(run(binary,'metrics',qrels,method_run,str(cutoff)))
        comparisons.append({'name':name,'cutoffs':cutoffs})
    comparison={'implementation':'C++ without STL','evaluation_documents':len(rows),'query_count':len(queries),'task':protocol['task'],'label_limitations':protocol['limitations'],'systems':comparisons,
                'tfidf_formula':'sum(qtf(t) * tf(t,d) / |d| * (1 + ln((N+1)/(df(t)+1)))^2)',
                'bm25_formula':'sum(qtf(t) * ln(1 + (N-df(t)+0.5)/(df(t)+0.5)) * tf(t,d)*(k1+1)/(tf(t,d)+k1*(1-b+b*|d|/avgdl)))',
                'bm25_parameters':{'k1':1.2,'b':0.75},'notes':'Three algorithms over the same corpus and labels; these are not external web search services.'}
    (output/'comparison_metrics.json').write_text(json.dumps(comparison,ensure_ascii=False,indent=2)+'\n')
    for s in systems:print(s['name'],s['average'])
    for s in comparisons:print(s['name'],'@5',s['cutoffs']['5']['average'])
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'build/ir');p.add_argument('--manifest',type=Path,default=ROOT/'lab1_corpus/data/native_documents.tsv');p.add_argument('--index-dir',type=Path,default=ROOT/'lab1_corpus/data/native_index');p.add_argument('--output',type=Path,default=ROOT/'lab6_search_quality/results');a=p.parse_args();evaluate(a.binary,a.manifest,a.index_dir,a.output)
