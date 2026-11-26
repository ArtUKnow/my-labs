# Native binary index, version 1

The C++ index and query processor use custom dynamic arrays, an open-addressed
hash table, merge sort and a binary min-heap. They do not use STL containers,
algorithms or a third-party search engine. zlib only decompresses corpus files.

All integers are unsigned, little-endian, with no native padding. Every file
starts with an eight-byte ASCII magic followed by a four-byte version (`1`).
Offsets are absolute byte offsets from the beginning of their file. Every
string is a four-byte UTF-8 byte length followed by those bytes, without NUL.

## dictionary.bin

Magic `IRDICT01`; after the common 12-byte header: eight-byte term count.
Then terms in ascending UTF-8 byte order:

| Field | Bytes |
|---|---:|
| Term byte length | 4 |
| Term UTF-8 bytes | variable |
| Document frequency | 4 |
| Offset in postings.bin | 8 |

The reader loads this compact dictionary and uses binary search. It does not
read the corpus or all postings while loading the index.

## postings.bin

Magic `IRPOST01`; after the common header: concatenated posting lists. A list
contains `document_frequency` four-byte document IDs, strictly increasing.
There is no text representation and no ready-made database in the index.
Only the needed lists are read using their dictionary offset.

## documents.bin

Magic `IRDOCS01`; after the common header: eight-byte document count.
For each document in ID order: four-byte ID, title string, original URL string,
and journal string. IDs are contiguous, beginning with zero. This is the
forward index used to render search results. It contains no full article text.

## meta.bin

Magic `IRMETA01`; after the common header:

| Field | Bytes |
|---|---:|
| Normalization: raw=0, stem=1, lemma=2 | 4 |
| Documents | 8 |
| Tokens | 8 |
| Terms | 8 |
| Postings | 8 |

An unrecognized magic or version is rejected. A later version can add record
fields or change posting encodings without interpreting those files as v1.

## Construction

Documents are streamed individually from gzip. A custom hash table accumulates
term postings. A repeated term in the same document creates no duplicate ID.
A block flush occurs after the configured token limit, at a document boundary.
One very large document may therefore exceed that limit; memory is bounded by
one document plus one block, rather than the corpus size.

Each temporary binary run is sorted by the implemented merge sort. The final
merge uses a custom heap ordered by term and run number. Runs cover disjoint,
ascending document ranges, so their same-term postings can be concatenated
without another sort. Temporary runs are removed after successful merging.
The final format consists only of the four binary files above; stats.json is
an external measurement report, not part of the search data format.

## Query language and interface

`search INDEX_DIR --json [--offset N] [--limit N]` processes one query per stdin
line. Adjacent terms or `&&` mean AND, `||` means OR, and `!` means NOT. Parentheses
and square brackets group expressions; precedence is NOT, AND, OR. Operators
work over sorted lists with two-pointer intersection, union and difference.
NOT complements against the complete document universe. Nesting is limited to
256 recursive levels so malformed input cannot exhaust the call stack.

Terms are tokenized as they were at indexing and normalized exactly once.
The JSON response includes query, full result count, offset, limit, the page's
IDs, and documents with ID, title, URL and journal. ID ordering is deterministic;
Boolean search does not rank by thematic relevance. A malformed query emits an
error object, continues with later lines and makes the process return status 1.
I/O failures and invalid/corrupt index data terminate with status 2.

## Validation

Native index tests cover multi-block construction, duplicate suppression,
precedence, implicit AND, NOT and double NOT, UTF-8 terms, missing terms, empty
queries, malformed expressions, nesting limits, JSON escaping, pagination and
raw/stem rebuilds. Tests compare actual query IDs with explicitly specified
fixture documents; no production corpus or expected result is hardcoded.
