
import csv
import gzip
import hashlib
import json
import ipaddress
import signal
import sqlite3
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
from pathlib import Path

HEADERS={'User-Agent':'MAI-IR-coursework/2.0 (open-access journal corpus)'}


def normalize_url(url):
    parts=urllib.parse.urlsplit(url.strip())
    if parts.scheme.lower() not in ('http','https') or not parts.hostname:
        raise ValueError('Only absolute HTTP(S) source URLs are accepted')
    host=parts.netloc.lower()
    if parts.scheme.lower()=='https' and host.endswith(':443'): host=host[:-4]
    if parts.scheme.lower()=='http' and host.endswith(':80'): host=host[:-3]
    return urllib.parse.urlunsplit((parts.scheme.lower(),host,parts.path or '/',parts.query,''))


class RawStore:
    def __init__(self,path):
        Path(path).parent.mkdir(parents=True,exist_ok=True)
        self.connection=sqlite3.connect(path)
        self.connection.row_factory=sqlite3.Row
        schema_path=Path(__file__).resolve().parent/'sql'/'schema.sql'
        self.connection.executescript(schema_path.read_text(encoding='utf-8'))
        self.connection.execute("UPDATE raw_queue SET state='pending' WHERE state='inflight'")
        self.connection.commit()

    def plan(self,rows,recrawl_after,now=None):
        now=int(time.time()) if now is None else now
        for row in rows:
            url=normalize_url(row['source_url']); previous=self.get(url)
            if previous is not None and previous['checked_at']>now-recrawl_after: continue
            self.connection.execute("INSERT INTO raw_queue(url,payload,state,planned_at) VALUES(?,?,'pending',?) ON CONFLICT(url) DO UPDATE SET payload=excluded.payload,state=CASE WHEN raw_queue.state='done' THEN 'pending' ELSE raw_queue.state END",(url,json.dumps(row,ensure_ascii=False),now))
        self.connection.commit()

    def get(self,url):
        return self.connection.execute('SELECT * FROM raw_documents WHERE url=?',(url,)).fetchone()

    def ready(self,limit):
        order='DESC' if getattr(self, 'reverse_queue', False) else 'ASC'
        return self.connection.execute("SELECT * FROM raw_queue WHERE state='pending' AND next_attempt<=? ORDER BY planned_at,url "+order+" LIMIT ?",(int(time.time()),limit)).fetchall()

    def mark_inflight(self,url):
        self.connection.execute("UPDATE raw_queue SET state='inflight' WHERE url=?",(url,));self.connection.commit()

    def success(self,row,response):
        url=normalize_url(row['source_url']);previous=self.get(url);now=int(time.time())
        if response['not_modified']:
            if previous is None: raise ValueError('HTTP 304 for a document not present in the database')
            self.connection.execute('UPDATE raw_documents SET checked_at=? WHERE url=?',(now,url)); status='unchanged'
        else:
            body=response['body'];digest=hashlib.sha256(body).hexdigest()
            if previous is not None and digest==previous['sha256']:
                self.connection.execute('UPDATE raw_documents SET checked_at=?,etag=?,last_modified=? WHERE url=?',(now,response['etag'],response['last_modified'],url));status='unchanged'
            else:
                values=(url,row['record_id'],row['url'],row['journal'],row['source_format'],response['content_type'],response['raw_gzip'] if 'raw_gzip' in response else gzip.compress(body,compresslevel=6,mtime=0),len(body),digest,now,now,response['etag'],response['last_modified'])
                self.connection.execute('INSERT OR REPLACE INTO raw_documents VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)',values);status='updated' if previous is not None else 'downloaded'
        self.connection.execute("UPDATE raw_queue SET state='done',last_error=NULL WHERE url=?",(url,));self.connection.commit();return status

    def failure(self,url,error,retry_delay):
        self.connection.execute("UPDATE raw_queue SET state='pending',attempts=attempts+1,next_attempt=?,last_error=? WHERE url=?",(int(time.time())+retry_delay,str(error)[:1000],url));self.connection.commit()

    def counts(self):
        return {'stored':self.connection.execute('SELECT COUNT(*) FROM raw_documents').fetchone()[0],
                'articles':self.connection.execute('SELECT COUNT(DISTINCT record_id) FROM raw_documents').fetchone()[0],
                'formats':dict(self.connection.execute('SELECT source_format,COUNT(*) FROM raw_documents GROUP BY source_format')),
                'pending':self.connection.execute("SELECT COUNT(*) FROM raw_queue WHERE state!='done'").fetchone()[0],
                'sources':dict(self.connection.execute('SELECT source,COUNT(*) FROM raw_documents GROUP BY source')),
                'raw_bytes':self.connection.execute('SELECT COALESCE(SUM(raw_bytes),0) FROM raw_documents').fetchone()[0],
                'stored_gzip_bytes':self.connection.execute('SELECT COALESCE(SUM(LENGTH(raw_gzip)),0) FROM raw_documents').fetchone()[0]}

    def close(self): self.connection.close()


class Fetcher:
    def __init__(self,delay,timeout,max_bytes=64*1024*1024):
        self.delay=delay;self.timeout=timeout;self.max_bytes=max_bytes;self.lock=threading.Lock();self.next_request=0.0

    def fetch(self,row,previous=None):
        with self.lock:
            pause=max(0,self.next_request-time.monotonic())
            if pause:time.sleep(pause)
            self.next_request=time.monotonic()+self.delay
        headers=dict(HEADERS)
        if previous is not None:
            if previous['etag']:headers['If-None-Match']=previous['etag']
            if previous['last_modified']:headers['If-Modified-Since']=previous['last_modified']
        request=urllib.request.Request(normalize_url(row['source_url']),headers=headers)
        hostname=urllib.parse.urlsplit(row['source_url']).hostname or ''
        try:loopback=ipaddress.ip_address(hostname).is_loopback
        except ValueError:loopback=hostname=='localhost'
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({})) if loopback else urllib.request.build_opener()
        try:
            with opener.open(request,timeout=self.timeout) as response:
                declared=response.headers.get('Content-Length')
                if declared and int(declared)>self.max_bytes:raise ValueError('Source exceeds configured max_source_bytes')
                body=response.read(self.max_bytes+1)
                if len(body)>self.max_bytes:raise ValueError('Source exceeds configured max_source_bytes')
                return {'not_modified':False,'body':body,'raw_gzip':gzip.compress(body,compresslevel=6,mtime=0),'content_type':response.headers.get('Content-Type','application/octet-stream'),
                        'etag':response.headers.get('ETag'),'last_modified':response.headers.get('Last-Modified')}
        except urllib.error.HTTPError as error:
            if error.code==304:return {'not_modified':True}
            raise


def load_sources(manifest,samples_only=False,include_landing=False,landing_only=False):
    with Path(manifest).open(encoding='utf-8',newline='') as file:rows=list(csv.DictReader(file,delimiter='\t'))
    if samples_only:
        samples={}
        for row in rows:samples.setdefault(row['journal'],row)
        rows=list(samples.values())
    result=[]
    for row in rows:
        source_format=row['source_format']
        row=dict(row);row['source_url']=row[source_format+'_url']
        if not landing_only:result.append(row)
        if include_landing or landing_only:
            landing=dict(row);landing['source_format']='html';landing['source_url']=row['url'];result.append(landing)
    return result


def crawl(config,config_path,fetcher=None):
    base=Path(config_path).resolve().parent
    db_path=Path(config['db']['path']);db_path=db_path if db_path.is_absolute() else base/db_path
    manifest=Path(config['corpus']['manifest']);manifest=manifest if manifest.is_absolute() else base/manifest
    options=config['logic'];store=RawStore(db_path)
    store.reverse_queue=bool(options.get('reverse_queue',False))
    sources=load_sources(manifest,bool(config['corpus'].get('samples_only',False)),bool(config['corpus'].get('include_landing',False)),bool(config['corpus'].get('landing_only',False)))
    store.plan(sources,int(options.get('recrawl_after',86400)))
    downloader=fetcher or Fetcher(float(options.get('delay',1)),int(options.get('timeout',30)),int(options.get('max_source_bytes',64*1024*1024)))
    workers=max(1,int(options.get('workers',4)));max_pages=int(options.get('max_pages',0));retry_delay=int(options.get('retry_delay',60))
    stop=threading.Event()
    def request_stop(*args):stop.set()
    old_handlers={}
    if threading.current_thread() is threading.main_thread():
        for signum in (signal.SIGINT,signal.SIGTERM):old_handlers[signum]=signal.signal(signum,request_stop)
    counters={'downloaded':0,'updated':0,'unchanged':0,'errors':0};processed=0
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            pending={}
            while not stop.is_set() or pending:
                if not stop.is_set():
                    allowance=workers-len(pending)
                    if max_pages:allowance=min(allowance,max_pages-processed-len(pending))
                    for task in store.ready(max(0,allowance)):
                        row=json.loads(task['payload']);previous=store.get(task['url']);store.mark_inflight(task['url'])
                        pending[pool.submit(downloader.fetch,row,dict(previous) if previous else None)]=row
                if not pending:break
                done,_=wait(pending,timeout=1,return_when=FIRST_COMPLETED)
                for future in done:
                    row=pending.pop(future);processed+=1
                    try:counters[store.success(row,future.result())]+=1
                    except Exception as error:
                        counters['errors']+=1;store.failure(row['source_url'],error,retry_delay)
                        print('raw fetch error',row['record_id'],str(error)[:120],flush=True)
                    if processed%100==0:print('raw processed',processed,store.counts(),flush=True)
                if max_pages and processed>=max_pages:stop.set()
        result={'processed':processed,**counters,**store.counts(),'database':str(db_path),'manifest':str(manifest),
                'capture_time_note':'crawled_at records actual raw fetch time, not historical clean-text download time.'}
        print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)
        return result
    finally:
        store.close()
        for signum,handler in old_handlers.items():signal.signal(signum,handler)


def export_samples(db_path,directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True);store=RawStore(db_path)
    try:
        selected=store.connection.execute('SELECT * FROM raw_documents ORDER BY source,record_id').fetchall();seen=set();result=[]
        for row in selected:
            key=(row['source'],row['source_format'])
            if key in seen:continue
            seen.add(key);path=directory/(row['record_id']+'.'+row['source_format']);body=gzip.decompress(row['raw_gzip']);path.write_bytes(body)
            result.append({key:row[key] for key in ['url','record_id','source','source_format','content_type','raw_bytes','sha256','crawled_at']})
        (directory/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return result
    finally:store.close()


def merge_completed_stores(destination, landing_database):

    connection=sqlite3.connect(destination)
    try:
        pending=connection.execute("SELECT COUNT(*) FROM raw_queue WHERE state!='done'").fetchone()[0]
        if pending:raise RuntimeError('Original-source queue is not complete')
        connection.execute('ATTACH DATABASE ? AS landing',(str(landing_database),))
        pending=connection.execute("SELECT COUNT(*) FROM landing.raw_queue WHERE state!='done'").fetchone()[0]
        if pending:raise RuntimeError('Landing-page queue is not complete')
        conflicts=connection.execute('SELECT COUNT(*) FROM raw_documents a JOIN landing.raw_documents b USING(url) WHERE a.sha256!=b.sha256').fetchone()[0]
        if conflicts:raise RuntimeError('Conflicting raw body for the same URL')
        connection.execute('INSERT OR IGNORE INTO raw_documents SELECT * FROM landing.raw_documents')
        connection.execute('INSERT OR IGNORE INTO raw_queue SELECT * FROM landing.raw_queue')
        connection.commit();connection.execute('DETACH DATABASE landing')
        connection.execute('PRAGMA wal_checkpoint(TRUNCATE)')
    finally:connection.close()


def database_summary(database,manifest,verify_bodies=False):

    connection=sqlite3.connect('file:'+str(Path(database).resolve())+'?mode=ro',uri=True)
    connection.row_factory=sqlite3.Row
    expected=load_sources(manifest,include_landing=True)
    counts={'canonical_articles':len(expected)//2,'raw_original_documents':0,'raw_html_documents':0,'raw_resource_documents':0}
    missing=[];sources={};raw_bytes=stored_bytes=0;timestamps=[]
    for item in expected:
        url=normalize_url(item['source_url'])
        row=connection.execute('SELECT * FROM raw_documents WHERE url=?',(url,)).fetchone()
        if row is None:missing.append(url);continue
        if verify_bodies:
            body=gzip.decompress(row['raw_gzip'])
            if len(body)!=row['raw_bytes'] or hashlib.sha256(body).hexdigest()!=row['sha256']:
                raise ValueError('Corrupt raw body: '+url)
        html_doc=item['source_format']=='html'
        counts['raw_html_documents' if html_doc else 'raw_original_documents']+=1
        counts['raw_resource_documents']+=1
        source=sources.setdefault(item['journal'],{'articles':0,'original_documents':0,'html_documents':0})
        source['html_documents' if html_doc else 'original_documents']+=1
        if not html_doc:source['articles']+=1
        raw_bytes+=row['raw_bytes'];stored_bytes+=len(row['raw_gzip']);timestamps.append(row['crawled_at'])
    pending=connection.execute("SELECT COUNT(*) FROM raw_queue WHERE state!='done'").fetchone()[0]
    errors=connection.execute("SELECT COUNT(*) FROM raw_queue WHERE last_error IS NOT NULL").fetchone()[0]
    connection.close()
    complete=not missing and not pending and not errors and verify_bodies
    return {'status':'complete' if complete else 'in_progress',**counts,'database':str(database),
            'raw_bytes':raw_bytes,'stored_gzip_bytes':stored_bytes,'sources':sources,
            'raw_capture_unix_min':min(timestamps) if timestamps else None,
            'raw_capture_unix_max':max(timestamps) if timestamps else None,
            'pending':pending,'errors':errors,'missing_resources':len(missing),
            'lossless_body_validation':verify_bodies,
            'capture_time_note':'Actual repeat-download timestamps; earlier historical timestamps are unknown.'}
