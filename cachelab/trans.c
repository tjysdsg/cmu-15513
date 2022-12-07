/**
 * @file trans.c
 * @brief Contains various implementations of matrix transpose
 *
 * Each transpose function must have a prototype of the form:
 *   void trans(size_t M, size_t N, double A[N][M], double B[M][N],
 *              double tmp[TMPCOUNT]);
 *
 * All transpose functions take the following arguments:
 *
 *   @param[in]     M    Width of A, height of B
 *   @param[in]     N    Height of A, width of B
 *   @param[in]     A    Source matrix
 *   @param[out]    B    Destination matrix
 *   @param[in,out] tmp  Array that can store temporary double values
 *
 * A transpose function is evaluated by counting the number of hits and misses,
 * using the cache parameters and score computations described in the writeup.
 *
 * Programming restrictions:
 *   - No out-of-bounds references are allowed
 *   - No alterations may be made to the source array A
 *   - Data in tmp can be read or written
 *   - This file cannot contain any local or global doubles or arrays of doubles
 *   - You may not use unions, casting, global variables, or
 *     other tricks to hide array data in other forms of local or global memory.
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "cachelab.h"

/**
 * @brief Checks if B is the transpose of A.
 *
 * You can call this function inside of an assertion, if you'd like to verify
 * the correctness of a transpose function.
 *
 * @param[in]     M    Width of A, height of B
 * @param[in]     N    Height of A, width of B
 * @param[in]     A    Source matrix
 * @param[out]    B    Destination matrix
 *
 * @return True if B is the transpose of A, and false otherwise.
 */
#ifndef NDEBUG
static bool is_transpose(size_t M, size_t N, double A[N][M], double B[M][N]) {
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                fprintf(stderr,
                        "Transpose incorrect.  Fails for B[%zd][%zd] = %.3f, "
                        "A[%zd][%zd] = %.3f\n",
                        j, i, B[j][i], i, j, A[i][j]);
                return false;
            }
        }
    }
    return true;
}
#endif

/**
 * @brief Return the minimum number
 */
size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

/**
 * @brief The solution transpose function that will be graded.
 *
 * You can call other transpose functions from here as you please.
 * It's OK to choose different functions based on array size, but
 * this function must be correct for all values of M and N.
 */
static void transpose_submit(size_t M, size_t N, double A[N][M], double B[M][N],
                             double tmp[TMPCOUNT]) {
    assert(M > 0);
    assert(N > 0);

#define BLOCK_SIZE 8
    for (size_t i = 0; i < N; i += BLOCK_SIZE) {
        size_t max_i = min(i + BLOCK_SIZE, N);
        for (size_t j = 0; j < M; j += BLOCK_SIZE) {
            size_t max_j = min(j + BLOCK_SIZE, M);

            if (i != j) { // no evictions
                for (size_t ii = i; ii < max_i; ii++) {
                    for (size_t jj = j; jj < max_j; jj++) {
                        assert(jj != ii);
                        B[jj][ii] = A[ii][jj];
                    }
                }
            } else { // evictions caused by diagonal and other elements
                for (size_t ii = i; ii < max_i; ii++) {
                    for (size_t jj = j; jj < max_j; jj++) {
                        if (ii != jj)
                            B[jj][ii] = A[ii][jj];
                    }

                    // put this at the end so in the next iteration B[ii] is
                    // already loaded into cache (A[ii] is evicted)
                    // copy this block will result in 2 * BLOCK_SIZE misses
                    if (ii < max_j)
                        B[ii][ii] = A[ii][ii];
                }
            }
        }
    }

    assert(is_transpose(M, N, A, B));
#undef BLOCK_SIZE
}

/**
 * @brief Registers all transpose functions with the driver.
 *
 * At runtime, the driver will evaluate each function registered here, and
 * and summarize the performance of each. This is a handy way to experiment
 * with different transpose strategies.
 */
void registerFunctions(void) {
    // Register the solution function. Do not modify this line!
    registerTransFunction(transpose_submit, SUBMIT_DESCRIPTION);
}
