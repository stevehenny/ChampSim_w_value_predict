#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define N (1<<5)   // 1M elements

int main() {
    uint64_t *arr = aligned_alloc(64, N * sizeof(uint64_t));
    if (!arr) return 1;

    // initialize array as a simple stride pattern
    for (size_t i = 0; i < N; ++i) arr[i] = i;

    volatile uint64_t sum = 0;
    // do several passes to generate lots of memory ops
    for (int pass = 0; pass < 100; ++pass) {
        for (size_t i = 0; i < N; i += 16) {
            sum += arr[i];
            arr[i] = arr[i] + 1;
        }
    }

    printf("sum=%lu\n", (unsigned long)sum);
    free(arr);
    return 0;
}
