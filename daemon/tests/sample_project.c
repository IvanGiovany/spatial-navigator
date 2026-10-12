#include <stdio.h>

static int leaf_a(int x) {
    return x * 2;
}

static int leaf_b(int y) {
    return y + 42;
}

static int helper(int val) {
    int a = leaf_a(val);
    int b = leaf_b(val);
    return a + b;
}

int compute(int input) {
    int result = helper(input);
    printf("Compute result: %d\n", result);
    return result;
}

int main(void) {
    int final_val = compute(10);
    return final_val > 0 ? 0 : 1;
}
