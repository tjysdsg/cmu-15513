/*
 * CS:APP Data Lab
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 *
 * bits.c - Source file with your solutions to the Lab.
 *          This is the file you will hand in to your instructor.
 */

/* Instructions to Students:

You will provide your solution to the Data Lab by
editing the collection of functions in this source file.

INTEGER CODING RULES:

  Replace the "return" statement in each function with one
  or more lines of C code that implements the function. Your code
  must conform to the following style:

  long Funct(long arg1, long arg2, ...) {
      // brief description of how your implementation works
      long var1 = Expr1;
      ...
      long varM = ExprM;

      varJ = ExprJ;
      ...
      varN = ExprN;
      return ExprR;
  }

  Each "Expr" is an expression using ONLY the following:
  1. (Long) integer constants 0 through 255 (0xFFL), inclusive. You are
      not allowed to use big constants such as 0xffffffffL.
  2. Function arguments and local variables (no global variables).
  3. Local variables of type int and long
  4. Unary integer operations ! ~
     - Their arguments can have types int or long
     - Note that ! always returns int, even if the argument is long
  5. Binary integer operations & ^ | + << >>
     - Their arguments can have types int or long
  6. Casting from int to long and from long to int

  Some of the problems restrict the set of allowed operators even further.
  Each "Expr" may consist of multiple operators. You are not restricted to
  one operator per line.

  You are expressly forbidden to:
  1. Use any control constructs such as if, do, while, for, switch, etc.
  2. Define or use any macros.
  3. Define any additional functions in this file.
  4. Call any functions.
  5. Use any other operations, such as &&, ||, -, or ?:
  6. Use any form of casting other than between int and long.
  7. Use any data type other than int or long.  This implies that you
     cannot use arrays, structs, or unions.

  You may assume that your machine:
  1. Uses 2s complement representations for int and long.
  2. Data type int is 32 bits, long is 64.
  3. Performs right shifts arithmetically.
  4. Has unpredictable behavior when shifting if the shift amount
     is less than 0 or greater than 31 (int) or 63 (long)

EXAMPLES OF ACCEPTABLE CODING STYLE:
  //
  // pow2plus1 - returns 2^x + 1, where 0 <= x <= 63
  //
  long pow2plus1(long x) {
     // exploit ability of shifts to compute powers of 2
     // Note that the 'L' indicates a long constant
     return (1L << x) + 1L;
  }

  //
  // pow2plus4 - returns 2^x + 4, where 0 <= x <= 63
  //
  long pow2plus4(long x) {
     // exploit ability of shifts to compute powers of 2
     long result = (1L << x);
     result += 4L;
     return result;
  }

NOTES:
  1. Use the dlc (data lab checker) compiler (described in the handout) to
     check the legality of your solutions.
  2. Each function has a maximum number of operations (integer, logical,
     or comparison) that you are allowed to use for your implementation
     of the function.  The max operator count is checked by dlc.
     Note that assignment ('=') is not counted; you may use as many of
     these as you want without penalty.
  3. Use the btest test harness to check your functions for correctness.
  4. Use the BDD checker to formally verify your functions
  5. The maximum number of ops for each function is given in the
     header comment for each function. If there are any inconsistencies
     between the maximum ops in the writeup and in this file, consider
     this file the authoritative source.

CAUTION:
  Do not add an #include of <stdio.h> (or any other C library header)
  to this file.  C library headers almost always contain constructs
  that dlc does not understand.  For debugging, you can use printf,
  which is declared for you just below.  It is normally bad practice
  to declare C library functions by hand, but in this case it's less
  trouble than any alternative.

  dlc will consider each call to printf to be a violation of the
  coding style (function calls, after all, are not allowed) so you
  must remove all your debugging printf's again before submitting your
  code or testing it with dlc or the BDD checker.  */

extern int printf(const char *, ...);

/* Edit the functions below.  Good luck!  */
// 1
/*
 * bitMatch - Create mask indicating which bits in x match those in y
 *            using only ~ and &
 *   Example: bitMatch(0x7L, 0xEL) = 0xFFFFFFFFFFFFFFF6L
 *   Legal ops: ~ &
 *   Max ops: 14
 *   Rating: 1
 */
long bitMatch(long x, long y) {
    // long a = x & y; // match 1s
    // long b = ~x & ~y; // match 0s
    // return ~(~a & ~b); // a | b

    // inversion of xor
    long a = x & ~y;
    long b = ~x & y;
    return ~a & ~b;
}
// 2
/*
 * leastBitPos - return a mask that marks the position of the
 *               least significant 1 bit. If x == 0, return 0
 *   Example: leastBitPos(96L) = 0x20L
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 6
 *   Rating: 2
 */
long leastBitPos(long x) {
    long a = ~x + 1; // least sig bit becomes 1, bits below it are all 0, bits
                     // above it are all inverted
    return a & x;
}
/*
 * dividePower2 - Compute x/(2^n), for 0 <= n <= 62
 *  Round toward zero
 *   Examples: dividePower2(15L,1L) = 7L, dividePower2(-33L,4L) = -2L
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 15
 *   Rating: 2
 */
long dividePower2(long x, long n) {
    long a = x >> n;
    long two_n = 1L << n;
    long b = two_n + ~0L; // all bits below n-th position is 1, others 0
    long sign = x >> 63;  // all zeros or all ones
    long should_add1 = b & x & sign; // b & x check if there is remain
    return a + !!should_add1;
}
/*
 * implication - return x -> y in propositional logic - 0 for false, 1
 * for true
 *   Example: implication(1L,1L) = 1L
 *            implication(1L,0L) = 0L
 *   Legal ops: ! ~ ^ |
 *   Max ops: 5
 *   Rating: 2
 */
long implication(long x, long y) {
    long ret = y | !x; // equivalent: x -> y <=> not(x) | y
    return ret;        // no need to use !!
}
/*
 * oddBits - return word with all odd-numbered bits set to 1
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 8
 *   Rating: 2
 */
long oddBits(void) {
    long a = 0xAA; // 0b10101010
    a = a | a << 8;
    a = a | a << 16;
    a = a | a << 32;
    return a;
}
// 3
/*
 * rotateLeft - Rotate x to the left by n
 *   Can assume that 0 <= n <= 63
 *   Examples:
 *      rotateLeft(0x8765432187654321L,4L) = 0x7654321876543218L
 *   Legal ops: ~ & ^ | + << >> !
 *   Max ops: 25
 *   Rating: 3
 */
long rotateLeft(long x, long n) {
    // mask for bits that need to be moved
    long mask = 1L << n;
    mask = mask + ~0L;

    long n_lo_bits = ~n + 65;
    long remain = x >> n_lo_bits;
    remain = remain & mask;
    x = x << n;
    return x + remain;
}
/*
 * isLess - if x < y  then return 1, else return 0
 *   Example: isLess(4L,5L) = 1L.
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 24
 *   Rating: 3
 */
long isLess(long x, long y) {
    long sub = ~x + 1 + y; // y - x

    long sign_sub = sub >> 63;
    long nsign_sub = ~sign_sub;
    long sign_x = x >> 63;
    long sign_y = y >> 63;
    long nsign_y = ~sign_y;

    // long overflow1 = ~sign_x & sign_y & nsign_sub;
    long noverflow1 = sign_x | nsign_y | sign_sub;
    long overflow2 = sign_x & nsign_y & sign_sub;
    // long ret = sub & nsign_sub & ~overflow1;
    long ret = sub & nsign_sub & noverflow1;

    ret = ret | overflow2;
    return !!ret;
}
// 4
/*
 * leftBitCount - returns count of number of consective 1's in
 *     left-hand (most significant) end of word.
 *   Examples: leftBitCount(-1L) = 64L, leftBitCount(0xFFF0F0F000000000L) = 12L
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 60
 *   Rating: 4
 */
long leftBitCount(long x) {
    return 2L;
}
/*
 * integerLog2 - return floor(log base 2 of x), where x > 0
 *   Example: integerLog2(16L) = 4L, integerLog2(31L) = 4L
 *   Legal ops: ! ~ & ^ | + << >>
 *   Max ops: 60
 *   Rating: 4
 */
long integerLog2(long x) {
    return 2L;
}
/*
 * trueThreeFourths - multiplies by 3/4 rounding toward 0,
 *   avoiding errors due to overflow
 *   Examples:
 *    trueThreeFourths(11L) = 8
 *    trueThreeFourths(-9L) = -6
 *    trueThreeFourths(4611686018427387904L) = 3458764513820540928L (no
 * overflow) Legal ops: ! ~ & ^ | + << >>
 * Max ops: 20
 * Rating: 4
 */
long trueThreeFourths(long x) {
    // dividePower2(x, 2) but round towards infinity
    long a = x >> 2;
    long mask = 3;                       // 0b11, mask of remaining bits
    long sign = x >> 63;                 // all zeros or all ones
    long should_add1 = mask & x & ~sign; // b & x check if there is remain

    // printf("should_add1: %ld\n", should_add1);

    long divide_by4 = a + !!should_add1;
    return x + ~divide_by4 + 1;
}
/* howManyBits - return the minimum number of bits required to represent x in
 *             two's complement
 *  Examples: howManyBits(12L) = 5L
 *            howManyBits(298L) = 10L
 *            howManyBits(-5L) = 4L
 *            howManyBits(0L)  = 1L
 *            howManyBits(-1L) = 1L
 *            howManyBits(0x8000000000000000L) = 64L
 *  Legal ops: ! ~ & ^ | + << >>
 *  Max ops: 70
 *  Rating: 4
 */
long howManyBits(long x) {
    return 0L;
}
// float
/*
 * floatIsEqual - Compute f == g for floating point arguments f and g.
 *   Both the arguments are passed as unsigned int's, but
 *   they are to be interpreted as the bit-level representations of
 *   single-precision floating point values.
 *   If either argument is NaN, return 0.
 *   +0 and -0 are considered equal.
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. also if, while
 *   Max ops: 25
 *   Rating: 2
 */
int floatIsEqual(unsigned uf, unsigned ug) {
    return 2;
}
/*
 * floatScale2 - Return bit-level equivalent of expression 2*f for
 *   floating point argument f.
 *   Both the argument and result are passed as unsigned int's, but
 *   they are to be interpreted as the bit-level representation of
 *   single-precision floating point values.
 *   When argument is NaN, return argument
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. also if, while
 *   Max ops: 30
 *   Rating: 4
 */
unsigned floatScale2(unsigned uf) {
    return 2;
}
/*
 * floatUnsigned2Float - Return bit-level equivalent of expression (float) u
 *   Result is returned as unsigned int, but
 *   it is to be interpreted as the bit-level representation of a
 *   single-precision floating point values.
 *   Legal ops: Any integer/unsigned operations incl. ||, &&. also if, while
 *   Max ops: 30
 *   Rating: 4
 */
unsigned floatUnsigned2Float(unsigned u) {
    return 2;
}
