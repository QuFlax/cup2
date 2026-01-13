#include <stdint.h>
#include <stdio.h>

int64_t fib(int64_t n) {
  if (n <= 1)
    return n;
  return fib(n - 1) + fib(n - 2);
}

int main() { printf("%ld\n", fib(14)); }
