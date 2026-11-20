#include <unity.h>
#include "RobotRemote.h"

void setUp() {}
void tearDown() {}

void test_mixer() {
    auto d = mixTracks(100, 50);
    TEST_ASSERT_EQUAL_INT(255, d.a);
    TEST_ASSERT_EQUAL_INT(-85, d.b);
    d = mixTracks(0, 0);
    TEST_ASSERT_EQUAL_INT(0, d.a);
    TEST_ASSERT_EQUAL_INT(0, d.b);
}

void test_timeout_closes_lease_even_at_wrap() {
    RemoteLease lease;
    TEST_ASSERT_FALSE(lease.update(41, 1, 0, 100, 0));
    TEST_ASSERT_TRUE(lease.open(41, UINT32_MAX - 100));
    TEST_ASSERT_TRUE(lease.update(41, 1, 0, 100, UINT32_MAX - 50));
    TEST_ASSERT_EQUAL_INT(255, lease.output(198).a);
    TEST_ASSERT_FALSE(lease.update(41, 2, 0, 100, 199));
    TEST_ASSERT_EQUAL_INT(0, lease.output(199).a);
    TEST_ASSERT_TRUE(lease.open(42, 200));
    TEST_ASSERT_FALSE(lease.update(41, 3, 0, 100, 201));
    TEST_ASSERT_TRUE(lease.update(42, 6, 0, 100, 201));
    TEST_ASSERT_FALSE(lease.update(42, 5, 0, -100, 202));
    TEST_ASSERT_FALSE(lease.update(42, 7, 101, 100, 202));
    lease.stop();
    TEST_ASSERT_FALSE(lease.update(42, 8, 0, 100, 203));
}

void test_reversal_passes_through_neutral() {
    TrackOutput track;
    TEST_ASSERT_EQUAL_INT(100, track.apply(100, 0));
    TEST_ASSERT_EQUAL_INT(0, track.apply(-100, 10));
    TEST_ASSERT_EQUAL_INT(0, track.apply(-100, 59));
    TEST_ASSERT_EQUAL_INT(-100, track.apply(-100, 60));
}

void test_battery_does_not_guess() {
    TEST_ASSERT_EQUAL_INT(0, batteryMillivolts(2100, 0));
    TEST_ASSERT_EQUAL_INT(0, batteryMillivolts(0, 1000));
    TEST_ASSERT_EQUAL_INT(4118, batteryMillivolts(2059, 1000));
    TEST_ASSERT_TRUE(batteryLow(3290));
    TEST_ASSERT_FALSE(batteryLow(0));
    TEST_ASSERT_FALSE(batteryLow(3700));
}

void test_bucket_requires_valid_endpoints_and_starts_at_rest() {
    Bucket bucket;
    TEST_ASSERT_EQUAL_INT(0, bucket.next(0, 1700));
    TEST_ASSERT_EQUAL_INT(0, bucket.next(1500, 2400));
    TEST_ASSERT_EQUAL_INT(1300, bucket.next(1300, 1700));
    TEST_ASSERT_EQUAL_INT(1700, bucket.next(1300, 1700));
    TEST_ASSERT_EQUAL_INT(1300, bucket.next(1300, 1700));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mixer);
    RUN_TEST(test_timeout_closes_lease_even_at_wrap);
    RUN_TEST(test_reversal_passes_through_neutral);
    RUN_TEST(test_battery_does_not_guess);
    RUN_TEST(test_bucket_requires_valid_endpoints_and_starts_at_rest);
    return UNITY_END();
}
