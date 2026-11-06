#include <unity.h>
#include <cstring>
#include "WiFiCredentials.h"

static void test_copy_string_copies_and_terminates(){
	char dst[33];
	memset(dst, 'x', sizeof(dst));

	TEST_ASSERT_TRUE(WiFiCredentials::copyString(dst, sizeof(dst), "HomeNet", 64));
	TEST_ASSERT_EQUAL_STRING("HomeNet", dst);
}

static void test_copy_string_accepts_a_value_that_exactly_fills_the_buffer(){
	char dst[33];
	const char* ssid32 = "0123456789abcdef0123456789ABCDEF";

	TEST_ASSERT_TRUE(WiFiCredentials::copyString(dst, sizeof(dst), ssid32, 64));
	TEST_ASSERT_EQUAL_STRING(ssid32, dst);
}

static void test_copy_string_rejects_an_overlong_value_and_leaves_the_buffer_empty(){
	char guarded[33 + 4];
	memset(guarded, 'x', sizeof(guarded));
	const char* ssid33 = "0123456789abcdef0123456789ABCDEFG";

	TEST_ASSERT_FALSE(WiFiCredentials::copyString(guarded, 33, ssid33, 64));
	TEST_ASSERT_EQUAL_STRING("", guarded);
	// Nothing written past the destination.
	TEST_ASSERT_EQUAL_MEMORY("xxxx", guarded + 33, 4);
}

static void test_copy_string_stops_at_src_max_for_unterminated_rc_data(){
	// RCData fields are char[16] with no guaranteed terminator; the bytes after them are not part of the value.
	struct { char ssid[16]; char next[4]; } rc;
	memset(rc.ssid, 'A', sizeof(rc.ssid));
	memcpy(rc.next, "ZZZ", 4);
	char dst[17];

	TEST_ASSERT_TRUE(WiFiCredentials::copyString(dst, sizeof(dst), rc.ssid, sizeof(rc.ssid)));
	TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAA", dst);
}

static void test_copy_string_rejects_a_null_source(){
	char dst[33] = "stale";

	TEST_ASSERT_FALSE(WiFiCredentials::copyString(dst, sizeof(dst), nullptr, 64));
	TEST_ASSERT_EQUAL_STRING("", dst);
}

static void test_copy_to_field_stores_a_full_length_ssid_without_terminator(){
	struct { uint8_t ssid[32]; uint8_t password[64]; } sta;
	memset(&sta, 0xAA, sizeof(sta));
	const char* ssid32 = "0123456789abcdef0123456789ABCDEF";

	TEST_ASSERT_TRUE(WiFiCredentials::copyToField(sta.ssid, sizeof(sta.ssid), ssid32));
	TEST_ASSERT_EQUAL_MEMORY(ssid32, sta.ssid, 32);
	// The old strcpy wrote the terminator into password[0].
	TEST_ASSERT_EQUAL_HEX8(0xAA, sta.password[0]);
}

static void test_copy_to_field_zero_fills_after_a_short_value(){
	uint8_t password[64];
	memset(password, 0xAA, sizeof(password));
	uint8_t expected[64] = {};
	memcpy(expected, "hunter22", 8);

	TEST_ASSERT_TRUE(WiFiCredentials::copyToField(password, sizeof(password), "hunter22"));
	TEST_ASSERT_EQUAL_MEMORY(expected, password, sizeof(password));
}

static void test_copy_to_field_rejects_an_overlong_value_and_zeroes_the_field(){
	uint8_t password[64];
	memset(password, 0xAA, sizeof(password));
	const uint8_t zeros[64] = {};
	char password65[66];
	memset(password65, 'p', 65);
	password65[65] = '\0';

	TEST_ASSERT_FALSE(WiFiCredentials::copyToField(password, sizeof(password), password65));
	TEST_ASSERT_EQUAL_MEMORY(zeros, password, sizeof(password));
}

static void test_copy_to_field_rejects_a_null_source(){
	uint8_t ssid[32];
	memset(ssid, 0xAA, sizeof(ssid));
	const uint8_t zeros[32] = {};

	TEST_ASSERT_FALSE(WiFiCredentials::copyToField(ssid, sizeof(ssid), nullptr));
	TEST_ASSERT_EQUAL_MEMORY(zeros, ssid, sizeof(ssid));
}

static void test_load_home_accepts_typical_credentials(){
	WiFiCredentials::Home home;

	TEST_ASSERT_TRUE(WiFiCredentials::loadHome(home, "HomeNet", "correct horse"));
	TEST_ASSERT_EQUAL_STRING("HomeNet", home.ssid);
	TEST_ASSERT_EQUAL_STRING("correct horse", home.password);
}

static void assertHomeEmpty(const WiFiCredentials::Home& home){
	TEST_ASSERT_EQUAL_STRING("", home.ssid);
	TEST_ASSERT_EQUAL_STRING("", home.password);
}

static void test_load_home_rejects_an_empty_ssid(){
	WiFiCredentials::Home home;

	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "", "correct horse"));
	assertHomeEmpty(home);
}

static void test_load_home_accepts_a_32_byte_ssid_and_rejects_33(){
	WiFiCredentials::Home home;

	TEST_ASSERT_TRUE(WiFiCredentials::loadHome(home, "0123456789abcdef0123456789ABCDEF", "correct horse"));
	TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789ABCDEF", home.ssid);

	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "0123456789abcdef0123456789ABCDEFG", "correct horse"));
	assertHomeEmpty(home);
}

static void test_load_home_requires_a_wpa2_passphrase_of_8_to_63_characters(){
	WiFiCredentials::Home home;

	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "HomeNet", ""));
	assertHomeEmpty(home);
	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "HomeNet", "1234567"));
	assertHomeEmpty(home);

	TEST_ASSERT_TRUE(WiFiCredentials::loadHome(home, "HomeNet", "12345678"));
	TEST_ASSERT_EQUAL_STRING("12345678", home.password);

	const char* pass63 = "passphrase with spaces and punctuation!? ......................";
	TEST_ASSERT_EQUAL(63, strlen(pass63));
	TEST_ASSERT_TRUE(WiFiCredentials::loadHome(home, "HomeNet", pass63));
	TEST_ASSERT_EQUAL_STRING(pass63, home.password);
}

static void test_load_home_accepts_64_hex_digits_as_a_raw_psk_only(){
	WiFiCredentials::Home home;
	const char* hex64 = "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdef0123";
	const char* text64 = "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdef012g";
	TEST_ASSERT_EQUAL(64, strlen(hex64));
	TEST_ASSERT_EQUAL(64, strlen(text64));

	TEST_ASSERT_TRUE(WiFiCredentials::loadHome(home, "HomeNet", hex64));
	TEST_ASSERT_EQUAL_STRING(hex64, home.password);

	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "HomeNet", text64));
	assertHomeEmpty(home);
}

static void test_load_home_rejects_a_65_character_password(){
	WiFiCredentials::Home home;
	const char* hex65 = "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdef01234";

	TEST_ASSERT_FALSE(WiFiCredentials::loadHome(home, "HomeNet", hex65));
	assertHomeEmpty(home);
}

void runCredentialTests(){
	RUN_TEST(test_copy_string_copies_and_terminates);
	RUN_TEST(test_copy_string_accepts_a_value_that_exactly_fills_the_buffer);
	RUN_TEST(test_copy_string_rejects_an_overlong_value_and_leaves_the_buffer_empty);
	RUN_TEST(test_copy_string_stops_at_src_max_for_unterminated_rc_data);
	RUN_TEST(test_copy_string_rejects_a_null_source);
	RUN_TEST(test_copy_to_field_stores_a_full_length_ssid_without_terminator);
	RUN_TEST(test_copy_to_field_zero_fills_after_a_short_value);
	RUN_TEST(test_copy_to_field_rejects_an_overlong_value_and_zeroes_the_field);
	RUN_TEST(test_copy_to_field_rejects_a_null_source);
	RUN_TEST(test_load_home_accepts_typical_credentials);
	RUN_TEST(test_load_home_rejects_an_empty_ssid);
	RUN_TEST(test_load_home_accepts_a_32_byte_ssid_and_rejects_33);
	RUN_TEST(test_load_home_requires_a_wpa2_passphrase_of_8_to_63_characters);
	RUN_TEST(test_load_home_accepts_64_hex_digits_as_a_raw_psk_only);
	RUN_TEST(test_load_home_rejects_a_65_character_password);
}
