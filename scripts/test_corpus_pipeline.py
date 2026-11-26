"""Real HTTP integration tests for raw-body preservation and recrawl state."""
import gzip
import http.server
import json
import socketserver
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'lab2_crawler'))
from raw_scoap3 import RawStore,Fetcher,normalize_url,merge_completed_stores,database_summary


class Handler(http.server.BaseHTTPRequestHandler):
    body=b'<article><body>first version</body></article>'
    etag='"v1"'
    received=[]
    fail_next=False

    def log_message(self,*args):pass

    def do_GET(self):
        type(self).received.append(dict(self.headers))
        if type(self).fail_next:
            type(self).fail_next=False;self.send_error(503);return
        if self.headers.get('If-None-Match')==type(self).etag:
            self.send_response(304);self.end_headers();return
        body=type(self).body
        self.send_response(200);self.send_header('Content-Type','application/xml')
        self.send_header('Content-Length',str(len(body)));self.send_header('ETag',type(self).etag)
        self.send_header('Last-Modified','Wed, 25 Nov 2026 12:00:00 GMT');self.end_headers();self.wfile.write(body)


class TestRawCorpusCrawler(unittest.TestCase):
    def setUp(self):
        Handler.body=b'<article><body>first version</body></article>';Handler.etag='"v1"';Handler.received=[];Handler.fail_next=False
        self.directory=tempfile.TemporaryDirectory()
        self.http=socketserver.TCPServer(('127.0.0.1',0),Handler)
        self.thread=threading.Thread(target=self.http.serve_forever,daemon=True);self.thread.start()
        self.url='http://127.0.0.1:'+str(self.http.server_address[1])+'/article.xml'
        self.row={'source_url':self.url,'url':self.url,'record_id':'fixture','journal':'Fixture Journal','source_format':'xml'}
        self.path=Path(self.directory.name)/'raw.sqlite'
        self.store=RawStore(self.path);self.fetcher=Fetcher(0,5)

    def tearDown(self):
        self.store.close();self.http.shutdown();self.http.server_close();self.thread.join();self.directory.cleanup()

    def test_exact_raw_body_conditional_unchanged_and_changed(self):
        self.store.plan([self.row],0);self.store.mark_inflight(self.url)
        response=self.fetcher.fetch(self.row);self.assertEqual(self.store.success(self.row,response),'downloaded')
        first=dict(self.store.get(self.url));self.assertEqual(gzip.decompress(first['raw_gzip']),Handler.body)
        self.assertLess(abs(time.time()-first['crawled_at']),3)
        self.assertEqual(first['source'],'Fixture Journal');self.assertEqual(first['content_type'],'application/xml')
        self.assertEqual(self.store.success(self.row,self.fetcher.fetch(self.row,first)),'unchanged')
        unchanged=dict(self.store.get(self.url));self.assertEqual(unchanged['crawled_at'],first['crawled_at'])
        self.assertEqual(Handler.received[-1]['If-None-Match'],'"v1"')
        self.assertIn('If-Modified-Since',Handler.received[-1])
        time.sleep(1.02);Handler.body=b'<article><body>modified version</body></article>';Handler.etag='"v2"'
        self.assertEqual(self.store.success(self.row,self.fetcher.fetch(self.row,unchanged)),'updated')
        changed=dict(self.store.get(self.url));self.assertEqual(gzip.decompress(changed['raw_gzip']),Handler.body)
        self.assertNotEqual(changed['sha256'],first['sha256']);self.assertGreater(changed['crawled_at'],first['crawled_at'])

    def test_resume_inflight_and_failed_request(self):
        self.store.plan([self.row],0);self.store.mark_inflight(self.url);self.store.close();self.store=RawStore(self.path)
        self.assertEqual(len(self.store.ready(1)),1)
        Handler.fail_next=True
        try:self.fetcher.fetch(self.row)
        except Exception as error:self.store.failure(self.url,error,0)
        self.assertEqual(self.store.counts()['stored'],0);self.assertEqual(len(self.store.ready(1)),1)
        self.store.mark_inflight(self.url);self.store.success(self.row,self.fetcher.fetch(self.row))
        self.assertEqual(self.store.counts()['stored'],1);self.assertEqual(self.store.counts()['pending'],0)
        self.store.plan([self.row],86400);self.assertEqual(len(self.store.ready(1)),0)

    def test_completed_stores_merge_without_counting_resources_as_articles(self):
        import csv
        landing_url=self.url.replace('article.xml','landing.html')
        original=dict(self.row,url=landing_url)
        self.store.plan([original],0);self.store.success(original,self.fetcher.fetch(original));self.store.close()
        landing_path=Path(self.directory.name)/'landing.sqlite'
        landing=RawStore(landing_path)
        html_row=dict(original,source_url=landing_url,source_format='html')
        landing.plan([html_row],0)
        with self.assertRaises(RuntimeError):merge_completed_stores(self.path,landing_path)
        landing.success(html_row,self.fetcher.fetch(html_row));landing.close()
        merge_completed_stores(self.path,landing_path)
        manifest=Path(self.directory.name)/'manifest.tsv'
        with manifest.open('w',newline='') as file:
            writer=csv.DictWriter(file,['record_id','url','journal','source_format','xml_url','pdf_url'],delimiter='\t')
            writer.writeheader();writer.writerow({'record_id':'fixture','url':landing_url,'journal':'Fixture Journal','source_format':'xml','xml_url':self.url,'pdf_url':''})
        summary=database_summary(self.path,manifest,verify_bodies=True)
        self.assertEqual(summary['status'],'complete')
        self.assertEqual(summary['canonical_articles'],1)
        self.assertEqual(summary['raw_original_documents'],1)
        self.assertEqual(summary['raw_html_documents'],1)
        self.assertEqual(summary['raw_resource_documents'],2)
        self.store=RawStore(self.path)

    def test_normalize_url(self):
        self.assertEqual(normalize_url('HTTPS://EXAMPLE.ORG:443/path#fragment'),'https://example.org/path')
        with self.assertRaises(ValueError):normalize_url('file:///etc/passwd')


if __name__=='__main__':unittest.main(verbosity=2)
