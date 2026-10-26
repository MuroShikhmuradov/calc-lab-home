// Murad Shikhmuradov 251ADB070
// (optional) https://github.com/MuroShikhmuradov/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm

// calc: reads expressions from .txt files calculates them and writes the
// results (or ERROR:<pos>) to an output file
// resource: https://craftinginterpreters.com/parsing-expressions.html 
// I used this website to understand how to parse mathematical expressions handle operator precedence
// and evaluate expressions correctly in my C calculator program
// resource: https://www.cprogramming.com/tutorial/c-tutorial.html
// to learn C programming basics including functions arrays pointers recursion and file handling 

// needed so mkdir/opendir/stat are declared with -std=c17
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MY_NAME     "murad"
#define MY_LASTNAME "shikhmuradov"
#define MY_ID       "251adb070"

// int or float - I keep track of which since 7 / 2 = 3 but 7 / 2.0 = 3.5
typedef struct {
    int       is_float;
    long long i;
    double    f;
} Number;

static double to_double(Number n)
{
    if (n.is_float)
        return n.f;
    return (double)n.i;
}

// parser state

// Positions: `text` is the whole file and `pos` indexes into it not into
// the line So a token's position is just its index + 1 and comments and
// '\n's before it are already counted
static const char *text;
static size_t      pos;
static size_t      line_end;  // index of this line's '\n' (or file length)

enum { T_NUM, T_PLUS, T_MINUS, T_MUL, T_DIV, T_POW, T_LPAR, T_RPAR, T_END, T_BAD };

static int    tok;
static size_t tok_start;
static Number tok_num;

static int    has_error;
static size_t error_index;

// only the first error counts
static void error_at(size_t index)
{
    if (!has_error) {
        has_error = 1;
        error_index = index;
    }
}

// tokenizer

// A bad char like & becomes T_BAD instead of an error right away The parser
// reads one token ahead so in "1 / 0 &" it already sees '&' while the
// division by zero (pos 5) should be the reported error
static void next_token(void) // resource used to understand how invalid characters are handled by the tokenizer.
{
    // '\r' skipped too for files with Windows line endings
    while (pos < line_end && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r'))
        pos++;

    tok_start = pos;

    // end of line is a token sitting on the '\n' so "3 +" reports the '\n'
    // (or file length + 1 if there's no '\n')
    if (pos >= line_end) {
        tok = T_END;
        return;
    }

    char c = text[pos];

    if (isdigit((unsigned char)c)) {
        unsigned long long value = 0;  // unsigned: no UB on huge numbers
        while (pos < line_end && isdigit((unsigned char)text[pos])) {
            value = value * 10 + (unsigned)(text[pos] - '0');
            pos++;
        }
        tok = T_NUM;

        // float only if a digit follows the '.' Otherwise the number stops
        // and the '.' becomes T_BAD next time so "5." errors at the '.'
        if (pos + 1 < line_end && text[pos] == '.' && isdigit((unsigned char)text[pos + 1])) {
            pos++;
            while (pos < line_end && isdigit((unsigned char)text[pos]))
                pos++;
            tok_num.is_float = 1;
            tok_num.f = strtod(text + tok_start, NULL);
        } else {
            tok_num.is_float = 0;
            tok_num.i = (long long)value;
        }
        return;
    }

    pos++;
    if (c == '+')      tok = T_PLUS;
    else if (c == '-') tok = T_MINUS;
    else if (c == '/') tok = T_DIV;
    else if (c == '(') tok = T_LPAR;
    else if (c == ')') tok = T_RPAR;
    else if (c == '*') {
        if (pos < line_end && text[pos] == '*') {
            pos++;
            tok = T_POW;
        } else {
            tok = T_MUL;
        }
    } else {
        tok = T_BAD;
    }
}

// math

// Both ints -> long long math If one is a float -> double math
// A float result that is inf/NaN is reported at the operator since the
// operator is what produced it

// Division by zero is reported at the right operand because that's the part
// that is zero (for "8 / (4 - 2 * 2)" that's the '(')
static Number calc(Number a, int op, Number b, size_t op_index, size_t right_index)
{
    Number r = {0, 0, 0.0};

    if (op == T_DIV && ((b.is_float && b.f == 0.0) || (!b.is_float && b.i == 0))) {
        error_at(right_index);
        return r;
    }

    if (!a.is_float && !b.is_float) {
        if (op == T_PLUS)       r.i = a.i + b.i;
        else if (op == T_MINUS) r.i = a.i - b.i;
        else if (op == T_MUL)   r.i = a.i * b.i;
        else                    r.i = a.i / b.i;
        return r;
    }

    double x = to_double(a), y = to_double(b);
    r.is_float = 1;
    if (op == T_PLUS)       r.f = x + y;
    else if (op == T_MINUS) r.f = x - y;
    else if (op == T_MUL)   r.f = x * y;
    else                    r.f = x / y;

    if (!isfinite(r.f))
        error_at(op_index);
    return r;
}

// fast power by squaring; skip the last squaring so it can't overflow
// e == 0 gives 1 also for 0 ** 0
static long long int_power(long long base, long long e) // resource used to understand exponentiation by squaring for faster integer powers.
{
    long long result = 1;
    while (e > 0) {
        if (e % 2 == 1)
            result *= base;
        e /= 2;
        if (e > 0)
            base *= base;
    }
    return result;
}

// negative int exponent -> error at the exponent that's what's wrong
static Number calc_power(Number a, Number b, size_t op_index, size_t exp_index)
{
    Number r = {0, 0, 0.0};

    if (!a.is_float && !b.is_float) {
        if (b.i < 0) {
            error_at(exp_index);
            return r;
        }
        r.i = int_power(a.i, b.i);
        return r;
    }

    r.is_float = 1;
    r.f = pow(to_double(a), to_double(b));
    if (!isfinite(r.f))          // 10.0 ** 400 or (0 - 8) ** 0.5
        error_at(op_index);
    return r;
}

// parser
//
//   expr    := term  { ('+' | '-') term }
//   term    := power { ('*' | '/') power }
//   power   := primary [ '**' power ]
//   primary := NUMBER | '(' expr ')'
//
// Precedence: each level only works with finished results from the level
// below so in "3 + 2 * 5" expr gets 2 * 5 already done
// + - * / are left to right because of the while loops
//
// Errors are always at tok_start the token the parser got stuck on:
// a bad char the 2nd of two operators T_END for a missing operand or ')'
// or a leftover ')' after the line is done

static Number parse_expr(void);

static Number parse_primary(void)
{
    Number n = {0, 0, 0.0};

    if (tok == T_NUM) {
        n = tok_num;
        next_token();
        return n;
    }
    if (tok == T_LPAR) {
        next_token();
        n = parse_expr();
        if (has_error)
            return n;
        if (tok != T_RPAR) {
            error_at(tok_start);
            return n;
        }
        next_token();
        return n;
    }
    // no number here (no unary minus either so -5 fails at the '-')
    error_at(tok_start);
    return n;
}

// ** is right associative: 2 ** 3 ** 2 = 2 ** 9
// The recursive version crashed on a really long "1 ** 1 ** ..." line
// (stack overflow) so I collect the chain into arrays and calculate from
// the right end instead - same result
static Number parse_power(void) // resource used to understand right-associative exponentiation and avoid deep recursion.
{
    Number first = parse_primary();
    if (has_error || tok != T_POW)
        return first;

    size_t cap = 16, count = 0;
    Number *nums   = malloc(cap * sizeof *nums);
    size_t *ops    = malloc(cap * sizeof *ops);     // position of each **
    size_t *starts = malloc(cap * sizeof *starts);  // start of each exponent
    if (!nums || !ops || !starts) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    nums[count++] = first;

    while (tok == T_POW) {
        if (count == cap) {
            cap *= 2;
            Number *n2 = realloc(nums, cap * sizeof *nums);
            size_t *o2 = realloc(ops, cap * sizeof *ops);
            size_t *s2 = realloc(starts, cap * sizeof *starts);
            if (!n2 || !o2 || !s2) {
                fprintf(stderr, "calc: out of memory\n");
                exit(1);
            }
            nums = n2; ops = o2; starts = s2;
        }
        ops[count] = tok_start;
        next_token();
        starts[count] = tok_start;
        nums[count] = parse_primary();
        if (has_error)
            break;
        count++;
    }

    Number result = nums[count - 1];
    if (!has_error) {
        for (size_t k = count - 1; k > 0 && !has_error; k--)
            result = calc_power(nums[k - 1], result, ops[k], starts[k]);
    }
    free(nums);
    free(ops);
    free(starts);
    return result;
}

static Number parse_term(void)
{
    Number left = parse_power();
    while (!has_error && (tok == T_MUL || tok == T_DIV)) {
        int op = tok;
        size_t op_index = tok_start;
        next_token();
        size_t right_index = tok_start;
        Number right = parse_power();
        if (has_error)
            break;
        left = calc(left, op, right, op_index, right_index);
    }
    return left;
}

static Number parse_expr(void)
{
    Number left = parse_term();
    while (!has_error && (tok == T_PLUS || tok == T_MINUS)) {
        int op = tok;
        size_t op_index = tok_start;
        next_token();
        Number right = parse_term();
        if (has_error)
            break;
        left = calc(left, op, right, op_index, 0);
    }
    return left;
}

// output buffer

// Results wait here because an error later in the file means none of them
// get written Size doubles when full so big outputs aren't copied over
// and over
static char  *out_buf;
static size_t out_len, out_cap;

static void out_add(const char *s)
{
    size_t n = strlen(s);
    if (out_len + n + 1 > out_cap) {
        size_t new_cap = out_cap ? out_cap * 2 : 4096;
        while (out_len + n + 1 > new_cap)
            new_cap *= 2;
        char *p = realloc(out_buf, new_cap);
        if (!p) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
        out_buf = p;
        out_cap = new_cap;
    }
    memcpy(out_buf + out_len, s, n + 1);
    out_len += n;
}

// files

// whole file in one read (size from ftell) plus a '\0' at the end
static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *data = malloc((size_t)len + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)len, f);
    fclose(f);

    data[got] = '\0';
    *size = got;
    return data;
}

// Big files: one pass over the file memchr jumps to each '\n' and lines
// are parsed where they are in memory - nothing is copied per line
static void evaluate(const char *data, size_t size)
{
    text = data;
    has_error = 0;
    out_len = 0;

    size_t line_start = 0;
    while (line_start < size && !has_error) {
        const char *nl = memchr(data + line_start, '\n', size - line_start);
        line_end = nl ? (size_t)(nl - data) : size;

        // skip blank lines and lines starting with '#'
        size_t k = line_start;
        while (k < line_end && (data[k] == ' ' || data[k] == '\t' || data[k] == '\r'))
            k++;

        if (k < line_end && data[k] != '#') {
            pos = line_start;
            next_token();
            Number n = parse_expr();
            if (!has_error && tok != T_END)   // like extra ')'
                error_at(tok_start);

            if (!has_error) {
                char line[64];
                if (n.is_float)
                    snprintf(line, sizeof line, "%.15g\n", n.f);
                else
                    snprintf(line, sizeof line, "%lld\n", n.i);
                out_add(line);
            }
        }
        line_start = line_end + 1;
    }

    if (has_error) {   // drop all results keep only the error
        char line[64];
        snprintf(line, sizeof line, "ERROR:%zu\n", error_index + 1);
        out_len = 0;
        out_add(line);
    }
}

// "labs/task1.txt" -> "task1" and "../in/" -> "in"
static char *base_name(const char *path)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/')
        end--;
    size_t start = end;
    while (start > 0 && path[start - 1] != '/')
        start--;

    size_t n = end - start;
    if (n > 4 && strncmp(path + end - 4, ".txt", 4) == 0)
        n -= 4;

    char *s = malloc(n + 1);
    if (!s) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(s, path + start, n);
    s[n] = '\0';
    return s;
}

// returns 0 if ok (an ERROR:<pos> result still counts as ok)
static int do_file(const char *in_path, const char *out_dir)
{
    size_t size = 0;
    char *data = read_file(in_path, &size);
    if (!data) {
        fprintf(stderr, "calc: cannot read '%s'\n", in_path);
        return 1;
    }
    evaluate(data, size);
    free(data);

    char *base = base_name(in_path);
    size_t len = strlen(out_dir) + strlen(base) + 64;
    char *out_path = malloc(len);
    if (!out_path) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    snprintf(out_path, len, "%s/%s_%s_%s_%s.txt", out_dir, base, MY_NAME, MY_LASTNAME, MY_ID);
    free(base);

    int result = 0;
    FILE *f = fopen(out_path, "w");
    if (!f || fwrite(out_buf, 1, out_len, f) != out_len) {
        fprintf(stderr, "calc: cannot write '%s'\n", out_path);
        result = 1;
    }
    if (f && fclose(f) != 0)
        result = 1;
    free(out_path);
    return result;
}

// every *.txt directly in the folder subfolders ignored
static int do_dir(const char *dir, const char *out_dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "calc: cannot open folder '%s'\n", dir);
        return 1;
    }

    int result = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0)
            continue;

        size_t len = strlen(dir) + n + 2;
        char *path = malloc(len);
        if (!path) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
        snprintf(path, len, "%s/%s", dir, e->d_name);

        struct stat st;   // skip a folder named like x.txt
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (do_file(path, out_dir) != 0)
                result = 1;
        }
        free(path);
    }
    closedir(d);
    return result;
}

// main

static void usage(void)
{
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char **argv)
{
    const char *dir = NULL;
    const char *out_dir = NULL;
    const char *input = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int is_dir = strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0;
        int is_out = strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0;

        if (is_dir || is_out) {
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder name\n", a);
                usage();
                return 1;
            }
            if (is_dir)
                dir = argv[++i];
            else
                out_dir = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", a);
            usage();
            return 1;
        } else if (input == NULL) {
            input = a;
        } else {
            fprintf(stderr, "calc: only one input file allowed\n");
            usage();
            return 1;
        }
    }

    if (dir == NULL && input == NULL) {
        fprintf(stderr, "calc: no input given\n");
        usage();
        return 1;
    }
    if (dir != NULL && input != NULL) {
        fprintf(stderr, "calc: use -d DIR or an input file, not both\n");
        usage();
        return 1;
    }

    // check the input first so we don't create a folder for nothing
    const char *src = dir ? dir : input;
    struct stat st;
    if (stat(src, &st) != 0 || (dir && !S_ISDIR(st.st_mode)) || (!dir && !S_ISREG(st.st_mode))) {
        fprintf(stderr, "calc: '%s' not found\n", src);
        return 1;
    }

    // no -o: use <input_base>_<name>_<id>
    char *default_dir = NULL;
    if (out_dir == NULL) {
        char *base = base_name(src);
        size_t len = strlen(base) + 64;
        default_dir = malloc(len);
        if (!default_dir) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
        snprintf(default_dir, len, "%s_%s_%s", base, MY_NAME, MY_ID);
        free(base);
        out_dir = default_dir;
    }

    int result = 0;
    if (mkdir(out_dir, 0777) != 0 && errno != EEXIST) {   // existing is fine
        fprintf(stderr, "calc: cannot create folder '%s'\n", out_dir);
        result = 1;
    } else if (dir) {
        result = do_dir(dir, out_dir);
    } else {
        result = do_file(input, out_dir);
    }

    free(default_dir);
    free(out_buf);
    return result;
}