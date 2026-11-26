#ifndef IR_LAB06_QUALITY_RANK_STRUCTURES_H
#define IR_LAB06_QUALITY_RANK_STRUCTURES_H
void *checked(void *p) {
    if (!p) {
        fprintf(stderr, "rank: allocation failed\n");
        exit(2);
    }
    return p;
}
inline char *copy(const char *p) {
    size_t n = strlen(p) + 1;
    char *result = (char *)checked(malloc(n));
    memcpy(result, p, n);
    return result;
}
struct Buffer {
    char *data;
    size_t size, capacity;
    Buffer() : data(NULL), size(0), capacity(0) {
    }
    ~Buffer() {
        free(data);
    }
    void reserve(size_t n) {
        if (n <= capacity)
            return;
        size_t c = capacity ? capacity : 256;
        while (c < n)
            c *= 2;
        data = (char *)checked(realloc(data, c));
        capacity = c;
    }
    void append(const char *p, size_t n) {
        reserve(size + n + 1);
        memcpy(data + size, p, n);
        size += n;
        data[size] = 0;
    }
    void clear() {
        size = 0;
        if (data)
            data[0] = 0;
    }
};
inline uint64_t hash(const char *p) {
    uint64_t result = 1469598103934665603ULL;
    for (; *p; p++) {
        result ^= (unsigned char)*p;
        result *= 1099511628211ULL;
    }
    return result;
}
struct Slot {
    char *key;
    int value;
};
struct Map {
    Slot *slots;
    size_t capacity, used;
    Map() : slots(NULL), capacity(0), used(0) {
        resize(128);
    }
    ~Map() {
        for (size_t i = 0; i < capacity; i++)
            free(slots[i].key);
        free(slots);
    }
    void resize(size_t n) {
        Slot *old = slots;
        size_t oldn = capacity;
        slots = (Slot *)checked(calloc(n, sizeof(Slot)));
        capacity = n;
        for (size_t i = 0; i < oldn; i++)
            if (old[i].key) {
                size_t at = hash(old[i].key) & (n - 1);
                while (slots[at].key)
                    at = (at + 1) & (n - 1);
                slots[at] = old[i];
            }
        free(old);
    }
    bool get(const char *key, int &value) const {
        size_t at = hash(key) & (capacity - 1);
        while (slots[at].key) {
            if (!strcmp(slots[at].key, key)) {
                value = slots[at].value;
                return true;
            }
            at = (at + 1) & (capacity - 1);
        }
        return false;
    }
    void put(const char *key, int value) {
        if ((used + 1) * 10 >= capacity * 7)
            resize(capacity * 2);
        size_t at = hash(key) & (capacity - 1);
        while (slots[at].key && strcmp(slots[at].key, key))
            at = (at + 1) & (capacity - 1);
        if (!slots[at].key) {
            slots[at].key = copy(key);
            used++;
        }
        slots[at].value = value;
    }
};

#endif
