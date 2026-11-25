# Лабораторные работы по курсу «Информационный поиск» (МАИ)

Корпус: **30 000 реальных документов из пяти коллекций**. Это статьи
Википедии, аннотации астрономических исследований arXiv и вопросы с двух
площадок Stack Exchange. Темы: космос, астрономия и общие энциклопедические
сюжеты. Документы разных жанров и языков показывают, как поиск ведёт себя
на неоднородном материале.

Все лабы написаны на чистом Python 3 (стандартная библиотека, без сторонних
пакетов). Все структуры данных для индекса сделаны самостоятельно.

## Структура

```
lab1_corpus/             добыча корпуса, импорт дампов, статистика; общий корпус в data/
lab2_crawler/            поисковый робот (yaml-конфиг, sqlite, возобновление, переобкачка)
lab3_tokenizer/          токенизация и замер скорости
lab4_stemmer/            стеммер Портера (ru/en), лемматизатор, сравнение качества поиска
lab5_zipf/               закон Ципфа и Мандельброта, график svg
lab6_search_quality/     метрики P, R, MAP, DCG, NDCG, ERR и сравнение систем
lab7_boolean_index/      булев инвертированный индекс (блочная сборка + слияние, бинарный формат)
run_all.sh               запуск всех автотестов (или пересборка результатов: ./run_all.sh build)
```

## Запуск

Требуется только `python3` (3.8+). Для повторного скачивания корпуса нужен
доступ к сети.

```
./run_all.sh            # прогон тестовых планов всех лаб, отчёты в labXX/results/test_report.txt
./run_all.sh build      # пересчитать все результаты по корпусу
```

Для робота `lab2_crawler` параметр `logic.retry_delay` в `config.yaml` задаёт
задержку перед повторной попыткой после временной ошибки сети или HTTP 408,
429, 5xx. Остальные ошибки HTTP 4xx завершают обработку URL.

## Происхождение корпуса

Готовые тексты лежат в `lab1_corpus/data/text`, отдельно по источникам:

| Коллекция | Документов | Содержимое |
|---|---:|---|
| `ru_wikipedia` | 10 000 | русские статьи; 40 исходных HTML-страниц |
| `simple_wikipedia` | 5 000 | статьи на простом английском; 20 исходных HTML-страниц |
| `arxiv_astronomy` | 5 000 | заголовки и аннотации астрономических работ |
| `space_stackexchange` | 5 000 | вопросы о космических полётах |
| `astronomy_stackexchange` | 5 000 | вопросы об астрономии |

Аннотация или вопрос считается отдельным документом; это **не полный текст
научной статьи и не ответы**. В `lab1_corpus/data/provenance/*.tsv` для
каждого нового файла есть ID, ссылка на оригинал, авторы и SHA-256 текста.
Для Stack Exchange взяты только вопросы с явно указанной в API лицензией
CC BY-SA 4.0; очищенный текст является преобразованной версией страницы.
При распространении соблюдайте
[условия атрибуции Википедии](https://foundation.wikimedia.org/wiki/Policy:Terms_of_Use),
[условия arXiv для метаданных](https://info.arxiv.org/help/oa/index.html) и
[лицензии Stack Exchange](https://stackoverflow.com/help/licensing).

Для воспроизведения импорта скачайте первые 128 МиБ каждого дампа (команды
ниже выполняются из корня репозитория), затем запустите импортёр. Снимки
фиксированы на 01.11.2026; частичных архивов в репозитории нет.

```sh
curl -fL -r 0-134217727 -o /tmp/ruwiki-20261101.bz2 https://dumps.wikimedia.org/ruwiki/20261101/ruwiki-20261101-pages-articles-multistream1.xml-p1p224167.bz2
curl -fL -r 0-134217727 -o /tmp/simplewiki-20261101.bz2 https://dumps.wikimedia.org/simplewiki/20261101/simplewiki-20261101-pages-articles-multistream.xml.bz2
python3 lab1_corpus/import_wikimedia_dump.py /tmp/ruwiki-20261101.bz2 ru_wikipedia 9960
python3 lab1_corpus/import_wikimedia_dump.py /tmp/simplewiki-20261101.bz2 simple_wikipedia 4980
python3 lab1_corpus/import_arxiv.py 5000
python3 lab1_corpus/import_stackexchange.py space 5000
python3 lab1_corpus/import_stackexchange.py astronomy 5000
```

SHA-256 скачанных частей: `7e4a51fb5b8e9449472973f04d8d7961f697471e89a4fbe0350463639597e1c2`
(русская), `f0e269b79d78f39d050b876e08dc4992ba2873377280dc733211c0d6cd696708`
(Simple English). Импортёр Wikimedia пропускает перенаправления, пустые статьи и
заголовки уже имеющихся HTML-примеров. Данные API arXiv и Stack Exchange
собраны 26.11.2026; повторный запуск позже может выбрать другие записи.
`corpus_stats.py`, токенизатор, анализ
Ципфа и булев индекс работают со всеми 30 000 текстами. Метрики качества
поиска считаются отдельно на 60 ранее размеченных документах: разметки
релевантности для новых статей нет. Выходные файлы токенизатора для новых
статей не хранятся в Git; `./run_all.sh build` создаёт их из текстов корпуса.

Для демонстрации общего индекса: `printf 'mars rover\n' |
python3 lab7_boolean_index/index_reader.py`. В результатах есть документы
arXiv, Astronomy Stack Exchange и Википедии.

Утилиты командной строки (stdin -> stdout):

```
python3 lab3_tokenizer/tokenizer.py < text.txt
python3 lab4_stemmer/stemmer.py < words.txt
python3 lab4_stemmer/lemmatizer.py < words.txt
python3 lab4_stemmer/search.py stem < queries.txt
python3 lab6_search_quality/evaluate.py --stdin < run.txt
python3 lab7_boolean_index/index_reader.py < queries.txt
python3 lab2_crawler/crawler.py lab2_crawler/config.yaml
```
