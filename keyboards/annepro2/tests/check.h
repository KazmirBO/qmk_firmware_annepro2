/* A tiny check framework for the tests: no dependencies, one header.
 *
 *   CHECK(cond)            a check that has to hold
 *   CHECK_EQ(a, b)         the same, for integers; prints both values when it fails
 *   CHECK_BYTES(a, b, n)   n bytes of a and b are equal; prints both as hex when they are not
 *   CHECK_KNOWN(cond, why) a check that is expected to FAIL on the current code (a suspected bug): a failure is printed
 *                          as KNOWN and does not fail the run, with `why` saying what was seen; if it passes, FIXED is
 *                          printed, so that the mark is removed
 *   CASE("name")           starts a test case (printed in failure messages)
 *   TEST_RESULT()          last statement of main(): prints the result line run.sh reads, returns the exit status
 */
#pragma once
#include <stdio.h>
#include <string.h>

static int         check_total, check_failed, check_known, check_fixed;
static const char *check_case = "";

#define CASE(name) (check_case = (name))

static inline const char *check_base_(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

#define CHECK_FAIL_(file, line, ...)                                          \
    do {                                                                      \
        check_failed++;                                                       \
        printf("  FAIL %s:%d [%s] ", check_base_(file), line, check_case);    \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
    } while (0)

#define CHECK(cond)                                                 \
    do {                                                            \
        check_total++;                                              \
        if (!(cond)) CHECK_FAIL_(__FILE__, __LINE__, "%s", #cond);  \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const long long a_ = (long long)(a), b_ = (long long)(b);                                         \
        check_total++;                                                                                    \
        if (a_ != b_)                                                                                     \
            CHECK_FAIL_(__FILE__, __LINE__, "%s == %s: got %lld (0x%llx), expected %lld (0x%llx)", #a, #b, \
                        a_, a_, b_, b_);                                                                  \
    } while (0)

static inline void check_hex_(char *out, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) sprintf(out + 3 * i, "%02x ", p[i]);
    out[3 * n] = 0;
}

#define CHECK_BYTES(a, b, n)                                                                        \
    do {                                                                                            \
        const size_t n_ = (n);                                                                      \
        check_total++;                                                                              \
        if (memcmp((a), (b), n_) != 0) {                                                            \
            char ha_[3 * 512 + 1], hb_[3 * 512 + 1];                                                \
            check_hex_(ha_, (const unsigned char *)(a), n_ > 512 ? 512 : n_);                       \
            check_hex_(hb_, (const unsigned char *)(b), n_ > 512 ? 512 : n_);                       \
            CHECK_FAIL_(__FILE__, __LINE__, "bytes differ\n    got:      %s\n    expected: %s", ha_, hb_); \
        }                                                                                           \
    } while (0)

#define CHECK_KNOWN(cond, why)                                                                         \
    do {                                                                                               \
        check_total++;                                                                                 \
        if (!(cond)) {                                                                                 \
            check_known++;                                                                             \
            printf("  KNOWN %s:%d [%s] %s fails: %s\n", check_base_(__FILE__), __LINE__, check_case,   \
                   #cond, (why));                                                                      \
        } else {                                                                                       \
            check_fixed++;                                                                             \
            printf("  FIXED %s:%d [%s] %s now holds: remove the KNOWN mark\n", check_base_(__FILE__), \
                   __LINE__, check_case, #cond);                                                       \
        }                                                                                              \
    } while (0)

#define TEST_RESULT()                                                                                  \
    (printf("RESULT %s checks=%d failed=%d known=%d fixed=%d\n", check_base_(__FILE__), check_total,   \
            check_failed, check_known, check_fixed),                                                   \
     check_failed ? 1 : 0)
