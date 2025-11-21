#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_Addition(void) {
    TEST_ASSERT_EQUAL(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Addition);
    return UNITY_END();
}