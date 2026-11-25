import math


def precision_at_k(results, grades, k):
    hits = 0
    for doc_id in results[:k]:
        if grades.get(doc_id, 0) > 0:
            hits += 1
    return hits / k


def recall_at_k(results, grades, k):
    total = 0
    for doc_id in grades:
        if grades[doc_id] > 0:
            total += 1
    if total == 0:
        return 0.0
    hits = 0
    for doc_id in results[:k]:
        if grades.get(doc_id, 0) > 0:
            hits += 1
    return hits / total


def average_precision(results, grades, k):
    total = 0
    for doc_id in grades:
        if grades[doc_id] > 0:
            total += 1
    if total == 0:
        return 0.0
    hits = 0
    sum_precision = 0.0
    for i in range(min(k, len(results))):
        if grades.get(results[i], 0) > 0:
            hits += 1
            sum_precision += hits / (i + 1)
    return sum_precision / min(total, k)


def dcg_at_k(results, grades, k):
    dcg = 0.0
    for i in range(min(k, len(results))):
        grade = grades.get(results[i], 0)
        dcg += (2 ** grade - 1) / math.log2(i + 2)
    return dcg


def ideal_dcg_at_k(grades, k):
    values = sorted(grades.values(), reverse=True)
    ideal = 0.0
    for i in range(min(k, len(values))):
        ideal += (2 ** values[i] - 1) / math.log2(i + 2)
    return ideal


def ndcg_at_k(results, grades, k):
    ideal = ideal_dcg_at_k(grades, k)
    if ideal == 0:
        return 0.0
    return dcg_at_k(results, grades, k) / ideal


def err_at_k(results, grades, k, max_grade=3):
    err = 0.0
    not_satisfied = 1.0
    for i in range(min(k, len(results))):
        grade = grades.get(results[i], 0)
        probability = (2 ** grade - 1) / (2 ** max_grade)
        err += not_satisfied * probability / (i + 1)
        not_satisfied *= 1 - probability
    return err


def all_metrics(results, grades, k):
    return {
        "p": round(precision_at_k(results, grades, k), 4),
        "r": round(recall_at_k(results, grades, k), 4),
        "ap": round(average_precision(results, grades, k), 4),
        "dcg": round(dcg_at_k(results, grades, k), 4),
        "ndcg": round(ndcg_at_k(results, grades, k), 4),
        "err": round(err_at_k(results, grades, k), 4),
    }


def average_metrics(rows):
    if len(rows) == 0:
        return {}
    result = {}
    for key in rows[0]:
        total = 0.0
        for row in rows:
            total += row[key]
        result[key] = round(total / len(rows), 4)
    return result
