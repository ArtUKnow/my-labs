
import argparse
import os
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent

def main():
    p=argparse.ArgumentParser();p.add_argument('--index-dir',default=str(ROOT/'lab1_corpus/data/native_index'));p.add_argument('--limit',type=int,default=50);p.add_argument('--offset',type=int,default=0);p.add_argument('--all',action='store_true');p.add_argument('--jsonl',action='store_true');a=p.parse_args()
    args=[str(ROOT/'build/ir'),'search',a.index_dir,'--offset',str(a.offset)]
    if not a.all:args+=['--limit',str(a.limit)]
    if a.jsonl:args+=['--json']
    os.execv(args[0],args)
if __name__=='__main__':main()
