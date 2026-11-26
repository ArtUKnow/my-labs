#ifndef IR_LAB05_ZIPF_FITTING_H
#define IR_LAB05_ZIPF_FITTING_H
struct ZipfFit { double exponent, c, classical, error; };
ZipfFit fit_distribution(Entry **ranked, size_t n) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; i++) {
        double x = log((double)i + 1), y = log((double)ranked[i]->count);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    double denominator = n * sxx - sx * sx;
    double exponent = denominator ? -(n * sxy - sx * sy) / denominator : 1;
    double c = n ? exp((sy + exponent * sx) / n) : 0;
    double classical = n ? (double)ranked[0]->count : 0;
    double error = 0;
    for (size_t i = 0; i < n; i++) {
        double d = log((double)ranked[i]->count) - log(c) + exponent * log((double)i + 1);
        error += d * d;
    }
    if (n)
        error /= n;
    return ZipfFit{exponent, c, classical, error};
}
#endif
