// Mert Ozari 251ADB057
// https://github.com/mertozari/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: w3schools.com C tutorials

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define NAME "mert"
#define LASTNAME "ozari"
#define STUDENT_ID "251ADB057"

typedef struct
{
    int is_float;
    long long i;
    double d;
} Value;

// The whole file stays in one buffer and the parser walks it with the pointer p.
// Because p points into that buffer, the 1-based file position of any token is
// simply (p - buf) + 1, so newlines and comment lines are counted automatically
// and no separate position counter can drift out of sync.
static char *buf;
static char *p;
static char *line_end;
static size_t err_pos;

static Value expr(void);

// Only the first error is kept, because the output must report the first one.
static void fail(char *at)
{
    if (err_pos == 0)
    {
        err_pos = (size_t)(at - buf) + 1;
    }
}

static void skip_spaces(void)
{
    while (p < line_end && (*p == ' ' || *p == '\t'))
    {
        p++;
    }
}

static double as_double(Value v)
{
    return v.is_float ? v.d : (double)v.i;
}

// A number is a float only if it has a '.' followed by a digit. For "5." or ".5"
// the number stops before the dot, so the caller reports the error at the dot.
static Value number(void)
{
    Value v = {0, 0, 0.0};
    char *start = p;
    while (isdigit((unsigned char)*p))
    {
        p++;
    }
    if (*p == '.' && isdigit((unsigned char)p[1]))
    {
        p++;
        while (isdigit((unsigned char)*p))
        {
            p++;
        }
        // strtod needs a terminated string, and cutting the buffer here avoids copying the token.
        char saved = *p;
        *p = '\0';
        v.is_float = 1;
        v.d = strtod(start, NULL);
        *p = saved;
        return v;
    }
    for (char *q = start; q < p; q++)
    {
        v.i = v.i * 10 + (*q - '0');
    }
    return v;
}

// Errors are reported where the parser is looking when it cannot continue:
// a bad character, a second operator or an extra ')' is at that character,
// and a missing operand or missing ')' is at the end of the line.
static Value primary(void)
{
    Value v = {0, 0, 0.0};
    skip_spaces();
    if (p >= line_end)
    {
        fail(p);
        return v;
    }
    if (*p == '(')
    {
        p++;
        v = expr();
        if (err_pos)
        {
            return v;
        }
        skip_spaces();
        if (p < line_end && *p == ')')
        {
            p++;
        }
        else
        {
            fail(p);
        }
        return v;
    }
    if (isdigit((unsigned char)*p))
    {
        return number();
    }
    fail(p);
    return v;
}

// A division by zero is reported at the right operand, and an infinite or nan
// float result is reported at the operator, as the rules say.
static Value arith(char op, Value a, Value b, char *op_at, char *rhs_at)
{
    Value r = {0, 0, 0.0};
    if (op == '/' && as_double(b) == 0.0)
    {
        fail(rhs_at);
        return r;
    }
    // Integer arithmetic is used only when both operands are integers,
    // so a single float operand makes the whole operation a double operation.
    if (!a.is_float && !b.is_float)
    {
        if (op == '+')
        {
            r.i = a.i + b.i;
        }
        else if (op == '-')
        {
            r.i = a.i - b.i;
        }
        else if (op == '*')
        {
            r.i = a.i * b.i;
        }
        else
        {
            r.i = a.i / b.i;
        }
        return r;
    }
    double x = as_double(a);
    double y = as_double(b);
    double d;
    if (op == '+')
    {
        d = x + y;
    }
    else if (op == '-')
    {
        d = x - y;
    }
    else if (op == '*')
    {
        d = x * y;
    }
    else
    {
        d = x / y;
    }
    if (!isfinite(d))
    {
        fail(op_at);
        return r;
    }
    r.is_float = 1;
    r.d = d;
    return r;
}

static Value apply_power(Value a, Value b, char *op_at, char *rhs_at)
{
    Value r = {0, 0, 0.0};
    if (!a.is_float && !b.is_float)
    {
        if (b.i < 0)
        {
            fail(rhs_at);
            return r;
        }

        //   I used the square-and-multiply idea so a huge exponent like 1 ** 1000000000 is still fast.
        // Unsigned math is used so that squaring past the last needed step cannot be signed overflow.
        unsigned long long result = 1;
        unsigned long long base = (unsigned long long)a.i;
        long long e = b.i;
        while (e > 0)
        {
            if (e & 1)
            {
                result *= base;
            }
            base *= base;
            e >>= 1;
        }
        r.i = (long long)result;
        return r;
    }
    double d = pow(as_double(a), as_double(b));
    if (!isfinite(d))
    {
        fail(op_at);
        return r;
    }
    r.is_float = 1;
    r.d = d;
    return r;
}

// power() calls itself for the exponent, so 2 ** 3 ** 2 groups as 2 ** (3 ** 2).
// That recursion is what makes ** right-associative, while the loops in term()
// and expr() make * / + - left-associative.
static Value power(void)
{
    Value base = primary();
    if (err_pos)
    {
        return base;
    }
    skip_spaces();
    if (p + 1 < line_end && p[0] == '*' && p[1] == '*')
    {
        char *op_at = p;
        p += 2;
        skip_spaces();
        char *rhs_at = p;
        Value exponent = power();
        if (err_pos)
        {
            return base;
        }
        return apply_power(base, exponent, op_at, rhs_at);
    }
    return base;
}

// term() calls power() and expr() calls term(), so the operators that bind
// tighter (**, then * /, then + -) are always evaluated deeper in the call chain.
static Value term(void)
{
    Value left = power();
    while (!err_pos)
    {
        skip_spaces();
        if (p >= line_end || (*p != '*' && *p != '/'))
        {
            break;
        }
        // power() already consumed every "**", so a '*' seen here is a single multiplication.
        char op = *p;
        char *op_at = p;
        p++;
        skip_spaces();
        char *rhs_at = p;
        Value right = power();
        if (err_pos)
        {
            break;
        }
        left = arith(op, left, right, op_at, rhs_at);
    }
    return left;
}

static Value expr(void)
{
    Value left = term();
    while (!err_pos)
    {
        skip_spaces();
        if (p >= line_end || (*p != '+' && *p != '-'))
        {
            break;
        }
        char op = *p;
        char *op_at = p;
        p++;
        skip_spaces();
        char *rhs_at = p;
        Value right = term();
        if (err_pos)
        {
            break;
        }
        left = arith(op, left, right, op_at, rhs_at);
    }
    return left;
}

static void *grow(void *ptr, size_t size)
{
    void *q = realloc(ptr, size);
    if (q == NULL)
    {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    return q;
}

// Results are appended to one output buffer that doubles when full, and the
// input is scanned in place with memchr, so nothing is copied per line and
// large files take linear time. Nothing is written until the whole file
// is checked, because after an error only the ERROR line may be output.
static char *evaluate(char *data, size_t len, size_t *out_len)
{
    buf = data;
    err_pos = 0;
    size_t cap = 4096;
    size_t n = 0;
    char *out = grow(NULL, cap);
    char *stop = data + len;
    char *line = data;
    while (line < stop)
    {
        char *nl = memchr(line, '\n', (size_t)(stop - line));
        line_end = nl ? nl : stop;
        p = line;
        skip_spaces();
        // A '#' is a comment only as the first non-blank character; anywhere else the parser rejects it.
        if (p < line_end && *p != '#')
        {
            Value v = expr();
            if (!err_pos)
            {
                skip_spaces();
                if (p < line_end)
                {
                    fail(p);
                }
            }
            if (err_pos)
            {
                break;
            }
            if (n + 64 > cap)
            {
                cap *= 2;
                out = grow(out, cap);
            }
            if (v.is_float)
            {
                n += (size_t)snprintf(out + n, cap - n, "%.15g\n", v.d);
            }
            else
            {
                n += (size_t)snprintf(out + n, cap - n, "%lld\n", v.i);
            }
        }
        line = nl ? nl + 1 : stop;
    }
    if (err_pos)
    {
        n = (size_t)snprintf(out, cap, "ERROR:%zu\n", err_pos);
    }
    *out_len = n;
    return out;
}

//   I used the realloc doubling idea. w3schools "C Files" was used for fopen/fread/fclose.
static char *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "calc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = 65536;
    size_t len = 0;
    char *data = grow(NULL, cap + 1);
    while (1)
    {
        size_t got = fread(data + len, 1, cap - len, f);
        len += got;
        if (len < cap)
        {
            break;
        }
        cap *= 2;
        data = grow(data, cap + 1);
    }
    if (ferror(f))
    {
        fprintf(stderr, "calc: cannot read '%s': %s\n", path, strerror(errno));
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    // The terminating zero stops digit scanning at the end of the file without a bounds check.
    data[len] = '\0';
    *len_out = len;
    return data;
}

// Returns a newly allocated copy of the last path component, without trailing
// slashes and optionally without ".txt".
static char *base_of(const char *path, int strip_txt)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/')
    {
        end--;
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/')
    {
        start--;
    }
    size_t n = end - start;
    if (strip_txt && n > 4 && memcmp(path + end - 4, ".txt", 4) == 0)
    {
        n -= 4;
    }
    char *base = grow(NULL, n + 1);
    memcpy(base, path + start, n);
    base[n] = '\0';
    return base;
}

static int ensure_dir(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            return 0;
        }
        fprintf(stderr, "calc: '%s' exists and is not a folder\n", dir);
        return -1;
    }
    if (mkdir(dir, 0777) != 0)
    {
        fprintf(stderr, "calc: cannot create folder '%s': %s\n", dir, strerror(errno));
        return -1;
    }
    return 0;
}

static int process_file(const char *path, const char *outdir)
{
    size_t len = 0;
    char *data = read_file(path, &len);
    if (data == NULL)
    {
        return -1;
    }
    size_t out_len = 0;
    char *out = evaluate(data, len, &out_len);

    if (ensure_dir(outdir) != 0)
    {
        free(data);
        free(out);
        return -1;
    }
    char *base = base_of(path, 1);
    size_t size = strlen(outdir) + strlen(base) + 64;
    char *outpath = grow(NULL, size);
    snprintf(outpath, size, "%s/%s_%s_%s_%s.txt", outdir, base, NAME, LASTNAME, STUDENT_ID);

    int rc = 0;
    FILE *f = fopen(outpath, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "calc: cannot write '%s': %s\n", outpath, strerror(errno));
        rc = -1;
    }
    else
    {
        if (fwrite(out, 1, out_len, f) != out_len)
        {
            fprintf(stderr, "calc: cannot write '%s': %s\n", outpath, strerror(errno));
            rc = -1;
        }
        if (fclose(f) != 0)
        {
            fprintf(stderr, "calc: cannot write '%s': %s\n", outpath, strerror(errno));
            rc = -1;
        }
    }
    free(outpath);
    free(base);
    free(out);
    free(data);
    return rc;
}

static int process_dir(const char *indir, const char *outdir)
{
    DIR *d = opendir(indir);
    if (d == NULL)
    {
        fprintf(stderr, "calc: cannot open folder '%s': %s\n", indir, strerror(errno));
        return -1;
    }
    if (ensure_dir(outdir) != 0)
    {
        closedir(d);
        return -1;
    }
    int rc = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        const char *name = entry->d_name;
        size_t n = strlen(name);
        if (n <= 4 || strcmp(name + n - 4, ".txt") != 0)
        {
            continue;
        }
        size_t size = strlen(indir) + n + 2;
        char *full = grow(NULL, size);
        snprintf(full, size, "%s/%s", indir, name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode))
        {
            if (process_file(full, outdir) != 0)
            {
                rc = -1;
            }
        }
        free(full);
    }
    closedir(d);
    return rc;
}

static int usage(void)
{
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 1;
}

int main(int argc, char **argv)
{
    const char *indir = NULL;
    const char *infile = NULL;
    const char *outdir = NULL;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        int is_dir = strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0;
        int is_out = strcmp(arg, "-o") == 0 || strcmp(arg, "--output-dir") == 0;
        if (is_dir || is_out)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "calc: option '%s' needs a value\n", arg);
                return usage();
            }
            if (is_dir)
            {
                indir = argv[++i];
            }
            else
            {
                outdir = argv[++i];
            }
        }
        else if (arg[0] == '-')
        {
            fprintf(stderr, "calc: unknown option '%s'\n", arg);
            return usage();
        }
        else if (infile != NULL)
        {
            fprintf(stderr, "calc: only one input file is allowed\n");
            return usage();
        }
        else
        {
            infile = arg;
        }
    }
    if ((indir == NULL) == (infile == NULL))
    {
        return usage();
    }

    char *default_out = NULL;
    if (outdir == NULL)
    {
        char *base = base_of(indir ? indir : infile, indir == NULL);
        size_t size = strlen(base) + 64;
        default_out = grow(NULL, size);
        snprintf(default_out, size, "%s_%s_%s", base, NAME, STUDENT_ID);
        free(base);
        outdir = default_out;
    }

    int rc;
    if (indir != NULL)
    {
        rc = process_dir(indir, outdir);
    }
    else
    {
        rc = process_file(infile, outdir);
    }
    free(default_out);
    return rc == 0 ? 0 : 1;
}
