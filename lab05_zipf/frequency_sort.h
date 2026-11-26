#ifndef IR_LAB05_ZIPF_FREQUENCY_SORT_H
#define IR_LAB05_ZIPF_FREQUENCY_SORT_H
void merge_entries(Entry **a, Entry **temp, size_t start, size_t end) {
    if (end - start < 2)
        return;
    size_t middle = start + (end - start) / 2;
    merge_entries(a, temp, start, middle);
    merge_entries(a, temp, middle, end);
    size_t i = start, j = middle, k = start;
    while (i < middle && j < end) {
        bool left = a[i]->count > a[j]->count ||
                    (a[i]->count == a[j]->count && strcmp(a[i]->key, a[j]->key) < 0);
        temp[k++] = left ? a[i++] : a[j++];
    }
    while (i < middle)
        temp[k++] = a[i++];
    while (j < end)
        temp[k++] = a[j++];
    for (i = start; i < end; i++)
        a[i] = temp[i];
}
#endif
