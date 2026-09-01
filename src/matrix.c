#include "cacti.h"

/*
# Input format

The matrix program loads 2 numbers, `k` and `n` from the standard input, each in
a separate line. These numbers mean the number of rows and columns of a given
matrix respectively. Next, the program takes `k * n` lines of input, each
consisting of 2 numbers separated by a single space `v t`, where `v` is the next
entry in the matrix (traversing rows left to right, going row by row) and `t` is
the time in milliseconds required to calculate `v`. For example for inputs in
test/matrix/matrix.in, the matrix should look the following way:

|  1 | 1 | 12
| 23 | 3 | 7

# Computation

The program uses an actor system with the number of actors equal to the number
of columns in the matrix. Each actor will be responsible for values in a single
column assigned to it. Actors receive messages with a row number and a so far
accumulated sum of that row, then calculate the number in their column in that
row. Next, they add the number to the sum and pass it along to the next agent.

# Output format

The program prints `k` lines, each containing a single number which is the sum
of numbers in that matrix' row. For inputs in test/test_cases/matrix.in the
output is in test/matrix/matrix.out.
*/

int main() { return 0; }
