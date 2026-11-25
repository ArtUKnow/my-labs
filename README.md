# Лабораторные работы по курсу «Информационный поиск» (МАИ)

Корпус: статьи из категории «Космонавтика» русской Википедии и категории
«Spaceflight» Simple English Wikipedia (два источника).

Все лабы написаны на чистом Python 3 (стандартная библиотека, без сторонних
пакетов). Все структуры данных для индекса сделаны самостоятельно.

## Структура

```
lab1_corpus/             добыча корпуса, статистика; общий корпус в data/
lab2_crawler/            поисковый робот (yaml-конфиг, sqlite, возобновление, переобкачка)
lab3_tokenizer/          токенизация и замер скорости
lab4_stemmer/            стеммер Портера (ru/en), лемматизатор, сравнение качества поиска
lab5_zipf/               закон Ципфа и Мандельброта, график svg
lab6_search_quality/     метрики P, R, MAP, DCG, NDCG, ERR и сравнение систем
lab7_boolean_index/      булев инвертированный индекс (блочная сборка + слияние, бинарный формат)
run_all.sh               запуск всех автотестов (или пересборка результатов: ./run_all.sh build)
```

## Запуск

Требуется только `python3` (3.8+).

```
./run_all.sh            # прогон тестовых планов всех лаб, отчёты в labXX/results/test_report.txt
./run_all.sh build      # пересчитать все результаты по корпусу
```

Для робота `lab2_crawler` параметр `logic.retry_delay` в `config.yaml` задаёт
задержку перед повторной попыткой после временной ошибки сети или HTTP 408,
429, 5xx. Остальные ошибки HTTP 4xx завершают обработку URL.

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
