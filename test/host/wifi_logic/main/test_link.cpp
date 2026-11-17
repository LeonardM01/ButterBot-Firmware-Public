#include <unity.h>
#include "WiFiLink.h"

// An IPv4 address as esp_ip4_addr_t::addr holds it (ESP_IP4TOADDR): the first octet in the low byte.
static constexpr uint32_t ip4(uint32_t a, uint32_t b, uint32_t c, uint32_t d){
	return a | (b << 8) | (c << 16) | (d << 24);
}

static constexpr uint32_t Leased = ip4(192, 168, 1, 23);

static void test_drops_retry_after_one_second_then_double(){
	WiFiLink link("HomeNet");

	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs);
	TEST_ASSERT_EQUAL_UINT32(2000, link.disconnected("HomeNet").retryInMs);
	TEST_ASSERT_EQUAL_UINT32(4000, link.disconnected("HomeNet").retryInMs);
}

static void test_retries_forever_at_the_sixty_second_cap(){
	WiFiLink link("HomeNet");
	// 1, 2, 4, 8, 16, 32 s, then the cap.
	static constexpr uint32_t Expected[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000};

	for(const uint32_t want : Expected){
		TEST_ASSERT_EQUAL_UINT32(want, link.disconnected("HomeNet").retryInMs);
	}

	// A router that stays off for hours: never gives up, never exceeds the cap.
	for(int i = 0; i < 10000; ++i){
		TEST_ASSERT_EQUAL_UINT32(60000, link.disconnected("HomeNet").retryInMs);
	}
}

static void test_a_lease_resets_the_schedule(){
	WiFiLink link("HomeNet");
	link.disconnected("HomeNet");
	link.disconnected("HomeNet");
	link.disconnected("HomeNet");

	link.gotIP(Leased);

	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs);
}

static void test_online_only_once_an_address_is_leased(){
	WiFiLink link("HomeNet");
	TEST_ASSERT_FALSE(link.online());

	const WiFiLink::Step step = link.gotIP(Leased);

	TEST_ASSERT_TRUE(link.online());
	TEST_ASSERT_TRUE(step.onlineChanged);
	TEST_ASSERT_EQUAL_UINT32(0, step.retryInMs);
}

static void test_online_reports_the_leased_address(){
	WiFiLink link("HomeNet");

	link.gotIP(Leased);

	TEST_ASSERT_EQUAL_HEX32(Leased, link.address());
}

static void test_a_stale_rc_static_address_after_leaving_rc_is_not_online(){
	// RC's static 11.0.0.2 raises GOT_IP too; one still queued when the home profile is back is not a lease.
	WiFiLink link("HomeNet");
	link.useController();
	link.useHome();

	const WiFiLink::Step step = link.gotIP(ip4(11, 0, 0, 2));

	TEST_ASSERT_FALSE(step.onlineChanged);
	TEST_ASSERT_FALSE(link.online());

	// The home lease that follows still counts.
	TEST_ASSERT_TRUE(link.gotIP(Leased).onlineChanged);
	TEST_ASSERT_EQUAL_HEX32(Leased, link.address());
}

static void test_each_transition_is_reported_once(){
	WiFiLink link("HomeNet");
	link.gotIP(Leased);

	TEST_ASSERT_FALSE(link.gotIP(Leased).onlineChanged); // a renewed lease is not a new transition

	TEST_ASSERT_TRUE(link.disconnected("HomeNet").onlineChanged);
	TEST_ASSERT_FALSE(link.online());
	TEST_ASSERT_FALSE(link.disconnected("HomeNet").onlineChanged); // a failed retry while already offline
}

static void test_a_lost_lease_goes_offline_without_a_retry(){
	// Still associated: DHCP renews on its own and raises GOT_IP again.
	WiFiLink link("HomeNet");
	link.gotIP(Leased);

	const WiFiLink::Step step = link.lostIP();

	TEST_ASSERT_FALSE(link.online());
	TEST_ASSERT_TRUE(step.onlineChanged);
	TEST_ASSERT_EQUAL_UINT32(0, step.retryInMs);
}

static void test_a_connect_that_fails_to_start_retries_on_the_same_schedule(){
	WiFiLink link("HomeNet");

	TEST_ASSERT_EQUAL_UINT32(1000, link.connectFailed().retryInMs);
	TEST_ASSERT_EQUAL_UINT32(2000, link.disconnected("HomeNet").retryInMs);
	TEST_ASSERT_EQUAL_UINT32(4000, link.connectFailed().retryInMs);
}

static void test_rc_mode_goes_offline_and_never_retries_home(){
	WiFiLink link("HomeNet");
	link.gotIP(Leased);

	TEST_ASSERT_TRUE(link.useController().onlineChanged);
	TEST_ASSERT_FALSE(link.online());
	TEST_ASSERT_FALSE(link.homeActive());

	// The controller network's own events: its drops are WiFiStation's business, its static address isn't online.
	const WiFiLink::Step drop = link.disconnected("ButterCtrl");
	TEST_ASSERT_EQUAL_UINT32(0, drop.retryInMs);
	TEST_ASSERT_FALSE(drop.onlineChanged);
	TEST_ASSERT_EQUAL_UINT32(0, link.connectFailed().retryInMs);
	TEST_ASSERT_FALSE(link.gotIP(Leased).onlineChanged);
	TEST_ASSERT_FALSE(link.online());
}

static void test_leaving_rc_rejoins_home_on_a_fresh_schedule(){
	WiFiLink link("HomeNet");
	link.disconnected("HomeNet");
	link.disconnected("HomeNet");
	link.useController();

	link.useHome();

	TEST_ASSERT_TRUE(link.homeActive());
	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs);
}

static void test_the_controller_drop_after_leaving_rc_is_not_a_home_failure(){
	WiFiLink link("HomeNet");
	link.useController();
	link.useHome();

	const WiFiLink::Step step = link.disconnected("ButterCtrl");

	TEST_ASSERT_EQUAL_UINT32(0, step.retryInMs);
	// The schedule did not advance either.
	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs);
}

static void test_an_unnamed_drop_in_rc_mode_is_not_a_home_failure(){
	// WiFiService leaves the controller network while the link is still in RC mode, then switches to home and
	// connects in one step: the drop that disconnect raises, often without a name, must not arm a retry.
	WiFiLink link("HomeNet");
	link.useController();

	TEST_ASSERT_EQUAL_UINT32(0, link.disconnected("").retryInMs);

	link.useHome();
	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs); // the schedule did not advance
}

static void test_a_drop_without_a_network_name_counts_as_home(){
	WiFiLink link("HomeNet");

	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("").retryInMs);
}

static void test_a_full_32_byte_home_ssid_matches_its_unterminated_event_name(){
	// wifi_event_sta_disconnected_t::ssid is 32 bytes with ssid_len; a 32-byte name has no terminator.
	const char home[33] = "0123456789abcdef0123456789ABCDEF";
	const char eventSsid[32] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f',
								'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
	WiFiLink link(home);

	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected(std::string_view(eventSsid, sizeof(eventSsid))).retryInMs);
	// A longer or different name is someone else's network.
	TEST_ASSERT_EQUAL_UINT32(0, link.disconnected(std::string_view(eventSsid, 31)).retryInMs);
}

static void test_the_link_keeps_its_own_copy_of_the_home_ssid(){
	char ssid[33] = "HomeNet";
	WiFiLink link(ssid);

	ssid[0] = 'X'; // the caller's buffer changes, or goes away

	TEST_ASSERT_EQUAL_UINT32(1000, link.disconnected("HomeNet").retryInMs);
	TEST_ASSERT_EQUAL_UINT32(0, link.disconnected("XomeNet").retryInMs);
}

void runLinkTests(){
	RUN_TEST(test_the_link_keeps_its_own_copy_of_the_home_ssid);
	RUN_TEST(test_rc_mode_goes_offline_and_never_retries_home);
	RUN_TEST(test_leaving_rc_rejoins_home_on_a_fresh_schedule);
	RUN_TEST(test_the_controller_drop_after_leaving_rc_is_not_a_home_failure);
	RUN_TEST(test_an_unnamed_drop_in_rc_mode_is_not_a_home_failure);
	RUN_TEST(test_a_drop_without_a_network_name_counts_as_home);
	RUN_TEST(test_a_full_32_byte_home_ssid_matches_its_unterminated_event_name);
	RUN_TEST(test_a_lost_lease_goes_offline_without_a_retry);
	RUN_TEST(test_a_connect_that_fails_to_start_retries_on_the_same_schedule);
	RUN_TEST(test_online_only_once_an_address_is_leased);
	RUN_TEST(test_each_transition_is_reported_once);
	RUN_TEST(test_online_reports_the_leased_address);
	RUN_TEST(test_a_stale_rc_static_address_after_leaving_rc_is_not_online);
	RUN_TEST(test_drops_retry_after_one_second_then_double);
	RUN_TEST(test_retries_forever_at_the_sixty_second_cap);
	RUN_TEST(test_a_lease_resets_the_schedule);
}
