#include <unity.h>
#include "PasswordPolicy.h"
#include <string>
void setUp() {}
void tearDown() {}
void test_valid_passwords() {
    TEST_ASSERT_TRUE(validWifiPassword("Robot-strong-Password!42"));
    TEST_ASSERT_TRUE(validWifiPassword(std::string(8, 'x').c_str()));
    TEST_ASSERT_TRUE(validWifiPassword(std::string(63, 'x').c_str()));
}
void test_rejected_passwords() {
    TEST_ASSERT_FALSE(validWifiPassword(""));
    TEST_ASSERT_FALSE(validWifiPassword("1234567"));
    TEST_ASSERT_FALSE(validWifiPassword(std::string(64, 'x').c_str()));
    TEST_ASSERT_FALSE(validWifiPassword("pass word"));
    TEST_ASSERT_FALSE(validWifiPassword("password\n"));
    TEST_ASSERT_FALSE(validWifiPassword("password\t"));
}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_passwords);
    RUN_TEST(test_rejected_passwords);
    return UNITY_END();
}
