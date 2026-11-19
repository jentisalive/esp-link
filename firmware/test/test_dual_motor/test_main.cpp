#include <unity.h>
#include "DualMotorTest.h"

void setUp() {}
void tearDown() {}

void expect(uint32_t elapsed, int a, int b) {
    const auto duty = dualMotorTestDuties(elapsed);
    TEST_ASSERT_EQUAL_INT(a, duty.a);
    TEST_ASSERT_EQUAL_INT(b, duty.b);
}

void test_ramp_and_pause() {
    expect(0, 0, 0);
    expect(19, 0, 0);
    expect(20, 5, 0);
    expect(1019, 250, 0);
    expect(1020, 255, 0);
    expect(2519, 255, 0);
    expect(2520, 0, 0);
    expect(3319, 0, 0);
    expect(3320, 0, 0);
    expect(3340, -5, 0);
}

void test_full_sequence() {
    expect(4340, -255, 0);
    expect(5840, 0, 0);
    expect(7660, 0, 255);
    expect(9160, 0, 0);
    expect(10980, 0, -255);
    expect(12480, 0, 0);
    expect(13279, 0, 0);
    expect(13280, 200, 200);
    expect(15279, 200, 200);
    expect(15280, 0, 0);
    expect(16079, 0, 0);
    expect(16080, 200, -200);
    expect(18079, 200, -200);
    TEST_ASSERT_EQUAL_UINT32(18080, DUAL_MOTOR_TEST_MS);
    expect(18080, 0, 0);
    expect(60000, 0, 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ramp_and_pause);
    RUN_TEST(test_full_sequence);
    return UNITY_END();
}
