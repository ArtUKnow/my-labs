"""Measure actual C++ query execution; startup measured separately."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parent.parent
CASES=['dark matter','(neutrino || quark) mass','higgs !boson','((dark || black) (matter || hole)) || (neutrino !electron)','(the || a || and) !(particle || field)']


def benchmark(binary,index,output):
    started=time.perf_counter()
    subprocess.run([str(binary),'search',str(index),'--json','--limit','0'],input='',text=True,capture_output=True,check=True)
    startup=(time.perf_counter()-started)*1000
    inputs=''.join((query+'\n')*8 for query in CASES)
    result=subprocess.run([str(binary),'search',str(index),'--json','--limit','0'],input=inputs,text=True,capture_output=True,check=True)
    rows=[json.loads(line) for line in result.stdout.splitlines()]
    if len(rows)!=len(CASES)*8:raise ValueError('Unexpected response count')
    cases=[]
    for index_case,query in enumerate(CASES):
        responses=rows[index_case*8:(index_case+1)*8]
        cases.append({'query':query,'documents':responses[-1]['count'],'median_ms':statistics.median(row['elapsed_ms'] for row in responses[1:]),'min_ms':min(row['elapsed_ms'] for row in responses[1:]),'max_ms':max(row['elapsed_ms'] for row in responses[1:])})
    stats=json.loads((index/'stats.json').read_text())
    report={'implementation':'C++ without STL','index_documents':stats['documents'],'startup_including_process_ms':startup,'repeats':7,'cache':'one warmup per query, then seven repetitions in one process','timing_scope':'C++ lexing, normalization, posting reads and Boolean operations; JSON output and process startup excluded from per-query time','cases':cases}
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'build/ir');p.add_argument('--index-dir',type=Path,default=ROOT/'lab1_corpus/data/native_index');p.add_argument('--output',type=Path,default=ROOT/'lab8_boolean_search/results/benchmark.json');a=p.parse_args();benchmark(a.binary,a.index_dir,a.output)
