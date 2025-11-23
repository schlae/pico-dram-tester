#include "unity.h"
/* #include "pmemtest.h" */

void setUp(void) {}
void tearDown(void) {}

void test_Addition(void) {
    TEST_ASSERT_EQUAL(4, 2 + 3);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Addition);
    return UNITY_END();
}