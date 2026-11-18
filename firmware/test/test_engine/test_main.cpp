#include <unity.h>
#include "CommandEngine.h"

void setUp() {}
void tearDown() {}

void test_three_pulses_and_completion() {
    CommandEngine engine;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Accepted), static_cast<int>(engine.execute("1", 0)));
    TEST_ASSERT_TRUE(engine.led());
    TEST_ASSERT_TRUE(engine.busy());
    TEST_ASSERT_FALSE(engine.tick(249));
    TEST_ASSERT_FALSE(engine.tick(250));
    TEST_ASSERT_FALSE(engine.led());
    TEST_ASSERT_FALSE(engine.tick(500));
    TEST_ASSERT_TRUE(engine.led());
    TEST_ASSERT_FALSE(engine.tick(750));
    TEST_ASSERT_FALSE(engine.led());
    TEST_ASSERT_FALSE(engine.tick(1000));
    TEST_ASSERT_TRUE(engine.led());
    TEST_ASSERT_TRUE(engine.tick(1250));
    TEST_ASSERT_FALSE(engine.led());
    TEST_ASSERT_FALSE(engine.busy());
    TEST_ASSERT_EQUAL_UINT32(1, engine.completedJob());
    TEST_ASSERT_FALSE(engine.tick(2000));
}

void test_busy_status_and_unknown() {
    CommandEngine engine;
    engine.execute("1", 100);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Busy), static_cast<int>(engine.execute("off", 200)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Busy), static_cast<int>(engine.execute("1", 200)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Status), static_cast<int>(engine.execute("status", 200)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Pong), static_cast<int>(engine.execute("ping", 200)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandEngine::Reply::Unknown), static_cast<int>(engine.execute("2", 200)));
}

void test_on_off_and_sequential_jobs() {
    CommandEngine engine;
    engine.execute("on", 0);
    TEST_ASSERT_TRUE(engine.led());
    engine.execute("off", 0);
    TEST_ASSERT_FALSE(engine.led());
    for (uint32_t job = 1; job <= 2; ++job) {
        const uint32_t start = job * 2000;
        engine.execute("1", start);
        for (uint32_t i = 1; i <= 5; ++i) engine.tick(start + i * 250);
        TEST_ASSERT_EQUAL_UINT32(job, engine.completedJob());
    }
}

void test_clock_overflow() {
    CommandEngine engine;
    engine.execute("1", UINT32_MAX - 100);
    TEST_ASSERT_FALSE(engine.tick(148));
    TEST_ASSERT_TRUE(engine.led());
    TEST_ASSERT_FALSE(engine.tick(149));
    TEST_ASSERT_FALSE(engine.led());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_three_pulses_and_completion);
    RUN_TEST(test_busy_status_and_unknown);
    RUN_TEST(test_on_off_and_sequential_jobs);
    RUN_TEST(test_clock_overflow);
    return UNITY_END();
}
