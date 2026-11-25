# Лабораторные работы по курсу «Информационный поиск» (МАИ)

Корпус: статьи из категории «Космонавтика» русской Википедии и категории
«Spaceflight» Simple English Wikipedia (два источника).

Все лабы написаны на чистом Python 3 (стандартная библиотека, без сторонних
пакетов). Все структуры данных для индекса сделаны самостоятельно.

## Структура

```
labs/
  data/                  корпус (raw html, text, tokens, index) и запросы с оценками
  lab01_corpus/          добыча корпуса: скачивание, выделение текста, статистика
  lab02_crawler/         поисковый робот (yaml-конфиг, sqlite, возобновление, переобкачка)
  lab03_tokenizer/       токенизация и замер скорости
  lab04_stemmer/         стеммер Портера (ru/en), лемматизатор, сравнение качества поиска
  lab05_zipf/            закон Ципфа и Мандельброта, график svg
  lab06_quality/         метрики P, R, MAP, DCG, NDCG, ERR и сравнение систем
  lab07_boolean_index/   булев инвертированный индекс (блочная сборка + слияние, бинарный формат)
  run_all.sh             запуск всех автотестов (или пересборка результатов: ./run_all.sh build)
```

## Запуск

Требуется только `python3` (3.8+).

```
./run_all.sh            # прогон тестовых планов всех лаб, отчёты в labXX/results/test_report.txt
./run_all.sh build      # пересчитать все результаты по корпусу
```

Для робота `lab02_crawler` параметр `logic.retry_delay` в `config.yaml` задаёт
задержку перед повторной попыткой после временной ошибки сети или HTTP 408,
429, 5xx. Остальные ошибки HTTP 4xx завершают обработку URL.

Утилиты командной строки (stdin -> stdout):

```
python3 lab03_tokenizer/tokenizer.py < text.txt
python3 lab04_stemmer/stemmer.py < words.txt
python3 lab04_stemmer/lemmatizer.py < words.txt
python3 lab04_stemmer/search.py stem < queries.txt
python3 lab06_quality/evaluate.py --stdin < run.txt
python3 lab07_boolean_index/index_reader.py < queries.txt
python3 lab02_crawler/crawler.py lab02_crawler/config.yaml
```
