#include <catch_amalgamated.hpp>

TEST_CASE("The test target compiles as standard C++20", "[foundation]")
{
    STATIC_REQUIRE(__cplusplus >= 202002L);
    SUCCEED();
}
