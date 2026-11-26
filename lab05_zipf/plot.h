#ifndef IR_LAB05_ZIPF_PLOT_H
#define IR_LAB05_ZIPF_PLOT_H
int plot_distribution(const char *svg, Entry **ranked, size_t n, double classical, double c, double exponent) {
    FILE *f = fopen(svg, "w");
    if (!f) {
        perror(svg);
        return 1;
    }
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" "
          "height=\"560\" viewBox=\"0 0 900 560\"><rect width=\"900\" "
          "height=\"560\" fill=\"white\"/><g font-family=\"sans-serif\" "
          "font-size=\"14\"><text x=\"65\" y=\"25\">Term frequencies: log rank / "
          "log frequency</text><text x=\"65\" y=\"545\">Blue: corpus; red: "
          "classical c/r; green: fitted c/r^s</text>",
          f);
    double xmax = log10((double)(n > 1 ? n : 2)), ymax = log10(classical > 1 ? classical : 2);
    for (int e = 0; e <= (int)ceil(xmax); e++) {
        double x = 65 + 760 * e / xmax;
        fprintf(f,
                "<path d=\"M %.2f 45 V 495\" stroke=\"#ddd\"/><text x=\"%.2f\" "
                "y=\"520\">10^%d</text>",
                x, x, e);
    }
    for (int e = 0; e <= (int)ceil(ymax); e++) {
        double y = 495 - 450 * e / ymax;
        fprintf(f,
                "<path d=\"M 65 %.2f H 825\" stroke=\"#ddd\"/><text x=\"10\" "
                "y=\"%.2f\">10^%d</text>",
                y, y, e);
    }
    const char *colors[] = {"#2563eb", "#dc2626", "#16a34a"};
    for (int curve = 0; curve < 3; curve++) {
        fprintf(f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\" points=\"",
                colors[curve]);
        size_t last = (size_t)-1;
        for (size_t point = 0; point < 400 && n; point++) {
            size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
            if (at >= n)
                at = n - 1;
            if (at == last)
                continue;
            double rank = at + 1, y = curve == 0   ? (double)ranked[at]->count
                                      : curve == 1 ? classical / rank
                                                   : c / pow(rank, exponent);
            fprintf(f, "%.2f,%.2f ", 65 + 760 * log10(rank) / xmax, 495 - 450 * log10(y) / ymax);
            last = at;
        }
        fputs("\"/>", f);
    }
    fputs("</g></svg>\n", f);
    int result = fclose(f) ? 1 : 0;
    return result;
}
#endif
