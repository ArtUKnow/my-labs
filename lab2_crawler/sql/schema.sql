PRAGMA journal_mode=WAL;
          CREATE TABLE IF NOT EXISTS raw_documents(
            url TEXT PRIMARY KEY, record_id TEXT NOT NULL, record_url TEXT NOT NULL,
            source TEXT NOT NULL, source_format TEXT NOT NULL, content_type TEXT NOT NULL,
            raw_gzip BLOB NOT NULL, raw_bytes INTEGER NOT NULL, sha256 TEXT NOT NULL,
            crawled_at INTEGER NOT NULL, checked_at INTEGER NOT NULL,
            etag TEXT, last_modified TEXT);
          CREATE TABLE IF NOT EXISTS raw_queue(
            url TEXT PRIMARY KEY, payload TEXT NOT NULL, state TEXT NOT NULL DEFAULT 'pending',
            attempts INTEGER NOT NULL DEFAULT 0, next_attempt INTEGER NOT NULL DEFAULT 0,
            last_error TEXT, planned_at INTEGER NOT NULL);
          CREATE INDEX IF NOT EXISTS raw_queue_ready ON raw_queue(state,next_attempt);
