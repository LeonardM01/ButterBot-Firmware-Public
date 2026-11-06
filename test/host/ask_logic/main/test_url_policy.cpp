#include <unity.h>
#include "UrlPolicy.h"

static void test_https_is_always_allowed(){
	TEST_ASSERT_EQUAL(UrlPolicy::Https, checkApiUrl("https://api.example.com", false));
	TEST_ASSERT_EQUAL(UrlPolicy::Https, checkApiUrl("https://api.example.com", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Https, checkApiUrl("https://api.example.com:8443", false));
}

static void test_http_needs_the_dev_insecure_switch(){
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("http://192.168.1.20:8080", false));
	TEST_ASSERT_EQUAL(UrlPolicy::DevInsecureHttp, checkApiUrl("http://192.168.1.20:8080", true));
}

static void test_other_schemes_are_rejected(){
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("ftp://api.example.com", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("HTTPS://api.example.com", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("api.example.com", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("", true));
}

static void test_a_host_is_required(){
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https:///v1", true));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("http://", true));
}

static void test_credentials_in_the_url_are_rejected(){
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://user:pass@api.example.com", false));
}

static void test_the_url_is_a_base_without_query_fragment_or_trailing_slash(){
	// The client appends "/v1/ask", so the base must end on the host or a path segment.
	TEST_ASSERT_EQUAL(UrlPolicy::Https, checkApiUrl("https://example.com/butterbot", false));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://api.example.com/", false));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://api.example.com?x=1", false));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://api.example.com#frag", false));
}

static void test_whitespace_and_control_characters_are_rejected(){
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://api.example.com x", false));
	TEST_ASSERT_EQUAL(UrlPolicy::Rejected, checkApiUrl("https://api.example.com\r\nX: y", false));
}

static void test_the_check_runs_at_compile_time(){
	static_assert(checkApiUrl("https://api.example.invalid", false) == UrlPolicy::Https);
	static_assert(checkApiUrl("http://10.0.0.2:8080", false) == UrlPolicy::Rejected);
	TEST_PASS();
}

static void test_the_host_to_resolve_drops_scheme_port_and_path(){
	TEST_ASSERT_TRUE(apiHost("https://api.example.com") == "api.example.com");
	TEST_ASSERT_TRUE(apiHost("https://api.example.com:8443/butterbot") == "api.example.com");
	TEST_ASSERT_TRUE(apiHost("http://192.168.1.2:8080") == "192.168.1.2");
}

static void test_the_host_of_an_ipv6_literal_has_no_brackets(){
	TEST_ASSERT_TRUE(apiHost("https://[2001:db8::1]:8443") == "2001:db8::1");
	TEST_ASSERT_TRUE(apiHost("https://[2001:db8::1]") == "2001:db8::1");
}

static void test_there_is_no_host_without_a_known_scheme(){
	TEST_ASSERT_TRUE(apiHost("ftp://api.example.com").empty());
	TEST_ASSERT_TRUE(apiHost("api.example.com").empty());
	TEST_ASSERT_TRUE(apiHost("https://[2001:db8::1").empty());
	static_assert(apiHost("https://api.example.invalid") == "api.example.invalid");
}

void runUrlPolicyTests(){
	RUN_TEST(test_https_is_always_allowed);
	RUN_TEST(test_http_needs_the_dev_insecure_switch);
	RUN_TEST(test_other_schemes_are_rejected);
	RUN_TEST(test_a_host_is_required);
	RUN_TEST(test_credentials_in_the_url_are_rejected);
	RUN_TEST(test_the_url_is_a_base_without_query_fragment_or_trailing_slash);
	RUN_TEST(test_whitespace_and_control_characters_are_rejected);
	RUN_TEST(test_the_check_runs_at_compile_time);
	RUN_TEST(test_the_host_to_resolve_drops_scheme_port_and_path);
	RUN_TEST(test_the_host_of_an_ipv6_literal_has_no_brackets);
	RUN_TEST(test_there_is_no_host_without_a_known_scheme);
}
