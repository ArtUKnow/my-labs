import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'scripts'))
from web_native import main

if __name__=='__main__':
    main()
