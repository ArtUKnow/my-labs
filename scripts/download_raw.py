import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=ROOT/'lab2_crawler/data/scoap3_raw.sqlite')
    args=parser.parse_args()
    if args.output.exists():
        raise FileExistsError('Output already exists: '+str(args.output))
    folder=Path(tempfile.mkdtemp(prefix='mai_raw_download_'))
    subprocess.run(['gh','release','download','cpp-labs-v1','--repo','ArtUKnow/my-labs','--pattern','scoap3_raw.sqlite.part*','--pattern','SHA256SUMS','--pattern','raw_crawl_summary.json','--dir',str(folder)],check=True)
    parts=[]
    for line in (folder/'SHA256SUMS').read_text().splitlines():
        expected,name=line.split()
        if Path(name).name!=name:
            raise ValueError('Invalid part name')
        path=folder/name
        digest=hashlib.sha256()
        with path.open('rb') as source:
            while True:
                data=source.read(1024*1024)
                if not data:break
                digest.update(data)
        if digest.hexdigest()!=expected:
            raise ValueError('Checksum mismatch: '+name)
        parts.append(path)
    if not parts:
        raise ValueError('No raw parts in checksum list')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    temporary=args.output.with_suffix('.download')
    with temporary.open('wb') as target:
        for part in sorted(parts):
            with part.open('rb') as source:shutil.copyfileobj(source,target)
    temporary.rename(args.output)
    shutil.copy2(folder/'raw_crawl_summary.json',ROOT/'lab2_crawler/results/raw_crawl_summary.json')
    shutil.rmtree(folder)
    print('Raw SQLite corpus restored:',args.output)

if __name__=='__main__':main()
