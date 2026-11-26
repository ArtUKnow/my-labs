
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
ROOT=Path(__file__).resolve().parent.parent
NAMES={'dictionary.bin','postings.bin','documents.bin','meta.bin','stats.json'}

def package(index,archive):
    for name in NAMES:
        if not (index/name).is_file():raise FileNotFoundError(index/name)
    archive.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive,'w:gz',compresslevel=6) as tar:
        for name in sorted(NAMES):tar.add(index/name,arcname=name)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix+'.sha256').write_text(digest+'  '+archive.name+'\n')
    print(archive,archive.stat().st_size,digest)

def extract(archive,index):
    expected=archive.with_suffix(archive.suffix+'.sha256').read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:raise ValueError('Index archive SHA256 mismatch')
    index.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive,'r:gz') as tar:
        members=tar.getmembers()
        if {m.name for m in members}!=NAMES or any(not m.isfile() for m in members):raise ValueError('Unexpected archive member')
        for member in members:
            data=tar.extractfile(member).read()
            (index/member.name).write_bytes(data)
    print('Restored',json.loads((index/'stats.json').read_text())['documents'],'documents')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--extract',action='store_true')
    parser.add_argument('--index-dir',type=Path,default=ROOT/'lab1_corpus/data/native_index')
    parser.add_argument('--archive',type=Path,default=ROOT/'lab7_boolean_index/results/native_index.tar.gz')
    args=parser.parse_args()
    if args.extract:
        extract(args.archive,args.index_dir)
    else:
        package(args.index_dir,args.archive)
