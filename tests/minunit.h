/*
 * minunit.h
 *
 * Minimal unit testing framework for C
 *
 * Original from: https://github.com/travisdowns/minunit
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef MINUNIT_H
#define MINUNIT_H

#include <stdio.h>
#include <stdlib.h>

#define MINUNIT_TEST(name) int name(void)
#define MINUNIT_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, message); \
            return 1; \
        } \
    } while (0)

#define MINUNIT_RUN_TEST(test) \
    do { \
        printf("Running: %s... ", #test); \
        fflush(stdout); \
        int result = test(); \
        tests_run++; \
        if (result == 0) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
        } \
    } while (0)

#define MINUNIT_SUMMARY() \
    do { \
        printf("\n=== Test Summary ===\n"); \
        printf("Total: %d tests\n", tests_run); \
        printf("Passed: %d tests\n", tests_passed); \
        printf("Failed: %d tests\n", tests_run - tests_passed); \
        return tests_run != tests_passed; \
    } while (0)

// Global counters
extern int tests_run;
extern int tests_passed;

#endif /* MINUNIT_H */
