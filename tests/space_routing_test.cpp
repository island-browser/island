#include "space_routing.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "space.h"
#include "tab_id.h"

namespace island {
namespace {

constexpr SpaceColor kTestColor{0xFF4488FF};

Space MakeSpace(std::uint64_t id, const std::string& name) {
    return Space(SpaceId{id}, name, kTestColor);
}

// Builds spaces A (id 1) and B (id 2) with the given host patterns already routed to A.
std::vector<Space> TwoSpaces(const std::vector<std::string>& routes_for_a = {}) {
    std::vector<Space> spaces;
    spaces.push_back(MakeSpace(1, "A"));
    spaces.push_back(MakeSpace(2, "B"));
    spaces[0].SetRoutes(routes_for_a);
    return spaces;
}

TEST(SpaceRouting, SubdomainMatches) {
    const std::vector<RouteRule> rules = RouteRulesFromSpaces(TwoSpaces({"github.com"}));
    EXPECT_EQ(RouteForUrl("https://gist.github.com/x", rules), std::optional<SpaceId>(SpaceId{1}));
    EXPECT_EQ(RouteForUrl("https://github.com/", rules), std::optional<SpaceId>(SpaceId{1}));
}

TEST(SpaceRouting, LongestPatternWins) {
    std::vector<Space> spaces = TwoSpaces({"google.com"});
    spaces[1].SetRoutes({"docs.google.com"});
    const std::vector<RouteRule> rules = RouteRulesFromSpaces(spaces);
    EXPECT_EQ(RouteForUrl("https://docs.google.com/d/1", rules),
              std::optional<SpaceId>(SpaceId{2}));
    EXPECT_EQ(RouteForUrl("https://mail.google.com/", rules), std::optional<SpaceId>(SpaceId{1}));
}

TEST(SpaceRouting, OneSpacePerHost) {
    std::vector<Space> spaces = TwoSpaces({"github.com"});
    const RouteMove move = AssignRoute(spaces, SpaceId{2}, "github.com");
    EXPECT_EQ(move.moved_from, std::optional<SpaceId>(SpaceId{1}));
    EXPECT_TRUE(spaces[0].routes().empty());
    EXPECT_EQ(spaces[1].routes(), std::vector<std::string>{"github.com"});

    // Assigning a host that no space has yet moves nothing.
    const RouteMove fresh = AssignRoute(spaces, SpaceId{1}, "linear.app");
    EXPECT_FALSE(fresh.moved_from.has_value());
    EXPECT_EQ(spaces[0].routes(), std::vector<std::string>{"linear.app"});
}

TEST(SpaceRouting, AssignToMissingSpaceLeavesTheRuleWhereItIs) {
    std::vector<Space> spaces = TwoSpaces({"github.com"});
    // Space 9 does not exist: the rule must not be taken from space 1 and lost.
    const RouteMove move = AssignRoute(spaces, SpaceId{9}, "github.com");
    EXPECT_FALSE(move.moved_from.has_value());
    EXPECT_EQ(spaces[0].routes(), std::vector<std::string>{"github.com"});
    EXPECT_TRUE(spaces[1].routes().empty());
}

TEST(SpaceRouting, RemoveRouteDropsTheRuleFromItsSpace) {
    std::vector<Space> spaces = TwoSpaces({"github.com"});
    EXPECT_TRUE(RemoveRoute(spaces, "github.com"));
    EXPECT_TRUE(spaces[0].routes().empty());
    EXPECT_FALSE(RemoveRoute(spaces, "github.com"));
}

TEST(SpaceRouting, IgnoresNonWebUrls) {
    const std::vector<RouteRule> rules =
        RouteRulesFromSpaces(TwoSpaces({"example.com", "localhost"}));
    EXPECT_FALSE(RouteForUrl("file:///Users/a/example.com/index.html", rules).has_value());
    EXPECT_FALSE(RouteForUrl("data:text/html,example.com", rules).has_value());
    EXPECT_FALSE(RouteForUrl("about:blank", rules).has_value());
    EXPECT_FALSE(RouteForUrl("javascript:example.com", rules).has_value());
    EXPECT_FALSE(RouteForUrl("example.com/no-scheme", rules).has_value());
    EXPECT_FALSE(RouteForUrl("", rules).has_value());
}

TEST(SpaceRouting, NormalizesPatternInput) {
    // Review Focus 2: a URL, a port, a trailing dot and a case difference all reduce to one host.
    EXPECT_EQ(NormalizeRoutePattern("https://GitHub.com/x"),
              std::optional<std::string>("github.com"));
    EXPECT_EQ(NormalizeRoutePattern("github.com:443"), std::optional<std::string>("github.com"));
    EXPECT_EQ(NormalizeRoutePattern("github.com."), std::optional<std::string>("github.com"));
    EXPECT_EQ(NormalizeRoutePattern("  www.GitHub.com  "),
              std::optional<std::string>("github.com"));

    // Rejected: IP literals, localhost, and anything without a dot.
    EXPECT_FALSE(NormalizeRoutePattern("127.0.0.1").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("192.168.1.10:8080").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("[::1]").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("localhost").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("intranet").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("").has_value());
    EXPECT_FALSE(NormalizeRoutePattern("file:///etc/hosts").has_value());
}

TEST(SpaceRouting, DoesNotMatchSuffixWithoutDot) {
    // Review Focus 2: a rule never matches a lookalike host.
    const std::vector<RouteRule> rules = RouteRulesFromSpaces(TwoSpaces({"github.com"}));
    EXPECT_FALSE(RouteForUrl("https://notgithub.com/", rules).has_value());
    EXPECT_FALSE(RouteForUrl("https://github.company/", rules).has_value());
    EXPECT_FALSE(RouteForUrl("https://evilgithub.com/", rules).has_value());
}

}  // namespace
}  // namespace island
