import os
import sqlite3
import hashlib
import time


def make_hash(html):
    return hashlib.md5(html.encode("utf-8")).hexdigest()


class Storage:
    def __init__(self, path):
        folder = os.path.dirname(path)
        if folder != "" and not os.path.isdir(folder):
            os.makedirs(folder)
        self.connection = sqlite3.connect(path)
        self.create_tables()

    def create_tables(self):
        cursor = self.connection.cursor()
        cursor.execute(
            "CREATE TABLE IF NOT EXISTS documents ("
            "url TEXT PRIMARY KEY, "
            "html TEXT, "
            "source TEXT, "
            "crawled_at INTEGER, "
            "content_hash TEXT)"
        )
        cursor.execute(
            "CREATE TABLE IF NOT EXISTS queue ("
            "url TEXT PRIMARY KEY, "
            "source TEXT, "
            "added_at INTEGER, "
            "done INTEGER DEFAULT 0)"
        )
        self.connection.commit()

    def add_to_queue(self, url, source, added_at):
        cursor = self.connection.cursor()
        cursor.execute(
            "INSERT OR IGNORE INTO queue (url, source, added_at, done) VALUES (?, ?, ?, 0)",
            (url, source, added_at),
        )
        self.connection.commit()
        return cursor.rowcount > 0

    def requeue(self, url, added_at):
        cursor = self.connection.cursor()
        cursor.execute("UPDATE queue SET done = 0, added_at = ? WHERE url = ?", (added_at, url))
        self.connection.commit()

    def next_in_queue(self):
        cursor = self.connection.cursor()
        cursor.execute(
            "SELECT url, source FROM queue WHERE done = 0 AND added_at <= ? "
            "ORDER BY added_at, url LIMIT 1",
            (int(time.time()),),
        )
        row = cursor.fetchone()
        return row

    def mark_done(self, url):
        cursor = self.connection.cursor()
        cursor.execute("UPDATE queue SET done = 1 WHERE url = ?", (url,))
        self.connection.commit()

    def queue_size(self):
        cursor = self.connection.cursor()
        cursor.execute("SELECT COUNT(*) FROM queue WHERE done = 0")
        return cursor.fetchone()[0]

    def save_document(self, url, html, source, crawled_at):
        cursor = self.connection.cursor()
        cursor.execute(
            "INSERT OR REPLACE INTO documents (url, html, source, crawled_at, content_hash) VALUES (?, ?, ?, ?, ?)",
            (url, html, source, crawled_at, make_hash(html)),
        )
        self.connection.commit()

    def get_document(self, url):
        cursor = self.connection.cursor()
        cursor.execute("SELECT url, html, source, crawled_at, content_hash FROM documents WHERE url = ?", (url,))
        row = cursor.fetchone()
        if row is None:
            return None
        return {"url": row[0], "html": row[1], "source": row[2], "crawled_at": row[3], "content_hash": row[4]}

    def has_document(self, url):
        return self.get_document(url) is not None

    def count_documents(self):
        cursor = self.connection.cursor()
        cursor.execute("SELECT COUNT(*) FROM documents")
        return cursor.fetchone()[0]

    def old_documents(self, older_than):
        cursor = self.connection.cursor()
        cursor.execute("SELECT url, source FROM documents WHERE crawled_at <= ? ORDER BY crawled_at", (older_than,))
        return cursor.fetchall()

    def all_urls(self):
        cursor = self.connection.cursor()
        cursor.execute("SELECT url FROM documents ORDER BY url")
        return [row[0] for row in cursor.fetchall()]

    def close(self):
        self.connection.close()
