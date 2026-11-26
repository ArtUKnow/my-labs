#ifndef IR_LAB05_ZIPF_FREQUENCY_TABLE_H
#define IR_LAB05_ZIPF_FREQUENCY_TABLE_H
struct Entry {
    char *key;
    uint64_t count;
};
struct Frequencies {
    Entry *slots;
    size_t capacity, used;
    Frequencies() : slots(NULL), capacity(0), used(0) {
        grow(2048);
    }
    ~Frequencies() {
        for (size_t i = 0; i < capacity; i++)
            free(slots[i].key);
        free(slots);
    }
    void grow(size_t n) {
        Entry *old = slots;
        size_t oldn = capacity;
        slots = (Entry *)checked(calloc(n, sizeof(Entry)));
        capacity = n;
        for (size_t i = 0; i < oldn; i++)
            if (old[i].key) {
                size_t at = hash(old[i].key) & (capacity - 1);
                while (slots[at].key)
                    at = (at + 1) & (capacity - 1);
                slots[at] = old[i];
            }
        free(old);
    }
    void add(const char *key) {
        if ((used + 1) * 10 >= capacity * 7)
            grow(capacity * 2);
        size_t at = hash(key) & (capacity - 1);
        while (slots[at].key && strcmp(slots[at].key, key))
            at = (at + 1) & (capacity - 1);
        if (!slots[at].key) {
            slots[at].key = clone(key);
            used++;
        }
        slots[at].count++;
    }
};
#endif
