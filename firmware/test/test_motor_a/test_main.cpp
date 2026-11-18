#include <unity.h>
#include "MotorATest.h"

void setUp() {}
void tearDown() {}

void test_motor_a_sequence() {
    TEST_ASSERT_EQUAL_INT(150, motorATestDuty(0));
    TEST_ASSERT_EQUAL_INT(150, motorATestDuty(1999));
    TEST_ASSERT_EQUAL_INT(0, motorATestDuty(2000));
    TEST_ASSERT_EQUAL_INT(0, motorATestDuty(2999));
    TEST_ASSERT_EQUAL_INT(-255, motorATestDuty(3000));
    TEST_ASSERT_EQUAL_INT(-255, motorATestDuty(4999));
    TEST_ASSERT_EQUAL_INT(0, motorATestDuty(5000));
    TEST_ASSERT_EQUAL_INT(0, motorATestDuty(10000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_a_sequence);
    return UNITY_END();
}
