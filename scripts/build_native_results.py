
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent.parent
BINARY=ROOT/'build/ir'
MANIFEST=ROOT/'lab1_corpus/data/native_documents.tsv'


def execute(arguments):
    print(' '.join(map(str,arguments)),flush=True)
    subprocess.run(list(map(str,arguments)),cwd=ROOT,check=True)


def build_mode(mode):
    target=ROOT/'lab1_corpus/data'/('native_index' if mode=='stem' else 'native_index_'+mode)
    temporary=Path(tempfile.mkdtemp(prefix='native_build_',dir=target.parent))
    try:
        execute([BINARY,'build',MANIFEST,temporary,mode,'500000'])
        backup=target.with_name(target.name+'.previous')
        if backup.exists():raise FileExistsError(f'Unresolved previous build: {backup}')
        if target.exists():target.rename(backup)
        try:temporary.rename(target)
        except BaseException:
            if backup.exists():backup.rename(target)
            raise
        if backup.exists():shutil.rmtree(backup)
    finally:
        if temporary.exists():shutil.rmtree(temporary)


def main():
    execute(['make','-j2'])
    execute(['python3','scripts/prepare_corpus.py','--apply'])
    execute(['python3','lab1_corpus/corpus_stats.py'])
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures=[pool.submit(execute,[BINARY,'text-stats',MANIFEST,ROOT/'lab3_tokenizer/results/stats.json']),pool.submit(execute,[BINARY,'zipf',MANIFEST,ROOT/'lab5_zipf/results/zipf.json',ROOT/'lab5_zipf/results/zipf.svg'])]
        for future in futures:future.result()
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures=[pool.submit(build_mode,mode) for mode in ['raw','stem','lemma']]
        for future in futures:future.result()
    shutil.copy2(ROOT/'lab1_corpus/data/native_index/stats.json',ROOT/'lab7_boolean_index/results/stats.json')
    execute(['python3','scripts/evaluate_native.py'])
    execute(['python3','scripts/benchmark_native.py'])
    execute(['python3','scripts/package_index.py'])
    print('Native lab artifacts rebuilt. Generate reports after raw corpus capture is complete.',flush=True)

if __name__=='__main__':main()
