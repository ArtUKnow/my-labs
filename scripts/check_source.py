import ast
import io
from pathlib import Path
import re
import tokenize

ROOT=Path(__file__).resolve().parent.parent

def code_only(text):
    pattern=r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    return re.sub(pattern,' ',text,flags=re.S)

def check():
    forbidden=re.compile(r'\bstd\s*::|#\s*include\s*<(?:vector|map|unordered_map|set|unordered_set|string|algorithm|queue|stack|list|deque|array|functional)>')
    files=list((ROOT/'native').glob('*.cpp'))+list((ROOT/'native').glob('*.h'))
    for path in files:
        text=path.read_text()
        without_strings=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',' ',text,flags=re.S)
        if re.search(r'//|/\*',without_strings):
            raise ValueError('Unexpected C++ comment: '+str(path))
        if forbidden.search(code_only(text)):
            raise ValueError('Forbidden STL use: '+str(path))
    for folder in ['scripts','lab1_corpus','lab2_crawler','lab8_boolean_search']:
        for path in (ROOT/folder).glob('*.py'):
            text=path.read_text()
            ast.parse(text)
            for token in tokenize.generate_tokens(io.StringIO(text).readline):
                if token.type==tokenize.COMMENT:
                    raise ValueError('Unexpected source comment: '+str(path))
    print('Source check: no STL in',len(files),'C++ files; Python syntax and comments checked')

if __name__=='__main__':check()
