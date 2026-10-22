#include "matching/order.h"

#include <catch_amalgamated.hpp>

TEST_CASE("Plain stop factories initialize trigger orders", "[order]")
{
    const matching::Order buy = matching::Order::BuyStop(11, 7, 18'600, 25);
    const matching::Order sell = matching::Order::SellStop(12, 7, 18'400, 30);

    REQUIRE(buy.id == 11);
    REQUIRE(buy.symbol_id == 7);
    REQUIRE(buy.is_stop());
    REQUIRE(buy.is_buy());
    REQUIRE(buy.price == 0);
    REQUIRE(buy.stop_price == 18'600);
    REQUIRE(buy.quantity == 25);
    REQUIRE(buy.is_gtc());
    REQUIRE(buy.validate() == matching::ErrorCode::OK);

    REQUIRE(sell.id == 12);
    REQUIRE(sell.symbol_id == 7);
    REQUIRE(sell.is_stop());
    REQUIRE(sell.is_sell());
    REQUIRE(sell.price == 0);
    REQUIRE(sell.stop_price == 18'400);
    REQUIRE(sell.quantity == 30);
    REQUIRE(sell.is_gtc());
    REQUIRE(sell.validate() == matching::ErrorCode::OK);
}

TEST_CASE("Stop-limit factories preserve trigger and limit prices", "[order]")
{
    const matching::Order buy =
        matching::Order::BuyStopLimit(21, 8, 18'600, 18'610, 40);
    const matching::Order sell =
        matching::Order::SellStopLimit(22, 8, 18'400, 18'390, 45);

    REQUIRE(buy.is_stop_limit());
    REQUIRE(buy.is_buy());
    REQUIRE(buy.stop_price == 18'600);
    REQUIRE(buy.price == 18'610);
    REQUIRE(buy.quantity == 40);
    REQUIRE(buy.validate() == matching::ErrorCode::OK);

    REQUIRE(sell.is_stop_limit());
    REQUIRE(sell.is_sell());
    REQUIRE(sell.stop_price == 18'400);
    REQUIRE(sell.price == 18'390);
    REQUIRE(sell.quantity == 45);
    REQUIRE(sell.validate() == matching::ErrorCode::OK);
}

TEST_CASE("Validation rejects inconsistent price fields", "[order]")
{
    const matching::Order stop_without_trigger(
        31, 9, matching::OrderType::STOP, matching::OrderSide::BUY, 0, 0, 10);
    const matching::Order stop_with_limit_price(
        32, 9, matching::OrderType::STOP, matching::OrderSide::BUY, 18'610, 18'600, 10);
    const matching::Order stop_limit_without_limit_price(
        33, 9, matching::OrderType::STOP_LIMIT, matching::OrderSide::BUY, 0, 18'600, 10);
    const matching::Order limit_with_trigger(
        34, 9, matching::OrderType::LIMIT, matching::OrderSide::BUY, 18'500, 18'600, 10);

    REQUIRE(stop_without_trigger.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop_with_limit_price.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop_limit_without_limit_price.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(limit_with_trigger.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
}

TEST_CASE("Trailing factories initialize absolute and percentage parameters", "[order]")
{
    const matching::Order buy =
        matching::Order::TrailingBuyStop(41, 7, 18'600, 25, 100, 25);
    const matching::Order sell = matching::Order::TrailingSellStopLimit(
        42, 7, 18'400, 18'390, 30, -100, -25);

    REQUIRE(buy.is_trailing_stop());
    REQUIRE(buy.is_buy());
    REQUIRE(buy.price == 0);
    REQUIRE(buy.stop_price == 18'600);
    REQUIRE(buy.trailing_distance == 100);
    REQUIRE(buy.trailing_step == 25);
    REQUIRE(buy.validate() == matching::ErrorCode::OK);

    REQUIRE(sell.is_trailing_stop_limit());
    REQUIRE(sell.is_sell());
    REQUIRE(sell.price == 18'390);
    REQUIRE(sell.stop_price == 18'400);
    REQUIRE(sell.trailing_distance == -100);
    REQUIRE(sell.trailing_step == -25);
    REQUIRE(sell.validate() == matching::ErrorCode::OK);
}

TEST_CASE("Trailing validation enforces distance and step units", "[order]")
{
    matching::Order zero_distance =
        matching::Order::TrailingBuyStop(51, 7, 18'600, 10, 100);
    zero_distance.trailing_distance = 0;

    const matching::Order absolute_step_too_large =
        matching::Order::TrailingBuyStop(52, 7, 18'600, 10, 100, 100);
    const matching::Order mixed_absolute_signs =
        matching::Order::TrailingBuyStop(53, 7, 18'600, 10, 100, -1);
    const matching::Order percentage_boundary =
        matching::Order::TrailingSellStop(54, 7, 18'400, 10, -10'000, -9'999);
    const matching::Order percentage_too_large =
        matching::Order::TrailingSellStop(55, 7, 18'400, 10, -10'001, -1);
    const matching::Order mixed_percentage_signs =
        matching::Order::TrailingSellStop(56, 7, 18'400, 10, -100, 1);
    const matching::Order percentage_step_too_large =
        matching::Order::TrailingSellStop(57, 7, 18'400, 10, -100, -100);

    REQUIRE(zero_distance.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(absolute_step_too_large.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(mixed_absolute_signs.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(percentage_boundary.validate() == matching::ErrorCode::OK);
    REQUIRE(percentage_too_large.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(mixed_percentage_signs.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(percentage_step_too_large.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);

    matching::Order ordinary_limit = matching::Order::BuyLimit(58, 7, 18'500, 10);
    ordinary_limit.trailing_distance = 100;
    REQUIRE(ordinary_limit.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
}

TEST_CASE("Market factories preserve execution slippage bounds", "[order]")
{
    const matching::Order unlimited = matching::Order::BuyMarket(61, 7, 10);
    const matching::Order bounded = matching::Order::SellMarket(
        62, 7, 20, matching::OrderTimeInForce::FOK, 25);

    REQUIRE(unlimited.slippage == matching::Order::UNLIMITED_SLIPPAGE);
    REQUIRE(unlimited.is_ioc());
    REQUIRE(unlimited.validate() == matching::ErrorCode::OK);

    REQUIRE(bounded.slippage == 25);
    REQUIRE(bounded.is_fok());
    REQUIRE(bounded.validate() == matching::ErrorCode::OK);
}

TEST_CASE("Slippage is rejected for price-constrained order types", "[order]")
{
    matching::Order limit = matching::Order::BuyLimit(71, 7, 18'500, 10);
    matching::Order stop_limit =
        matching::Order::BuyStopLimit(72, 7, 18'600, 18'610, 10);
    matching::Order trailing_stop_limit =
        matching::Order::TrailingSellStopLimit(73, 7, 18'400, 18'390, 10, 100);

    limit.slippage = 0;
    stop_limit.slippage = 10;
    trailing_stop_limit.slippage = 10;

    REQUIRE(limit.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop_limit.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(trailing_stop_limit.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
}

TEST_CASE("All-or-none is valid only for persistent price-constrained orders",
          "[order]")
{
    const matching::Order limit = matching::Order::BuyLimit(
        81, 7, 18'500, 25, matching::OrderTimeInForce::AON);
    const matching::Order stop_limit = matching::Order::SellStopLimit(
        82, 7, 18'400, 18'390, 30, matching::OrderTimeInForce::AON);
    const matching::Order market = matching::Order::BuyMarket(
        83, 7, 10, matching::OrderTimeInForce::AON);
    const matching::Order stop = matching::Order::BuyStop(
        84, 7, 18'600, 10, matching::OrderTimeInForce::AON);
    const matching::Order trailing_stop = matching::Order::TrailingSellStop(
        85, 7, 18'400, 10, 100, 0, matching::OrderTimeInForce::AON);

    REQUIRE(limit.is_aon());
    REQUIRE(limit.validate() == matching::ErrorCode::OK);
    REQUIRE(stop_limit.is_aon());
    REQUIRE(stop_limit.validate() == matching::ErrorCode::OK);
    REQUIRE(market.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(trailing_stop.validate() ==
            matching::ErrorCode::ORDER_PARAMETER_INVALID);
}

TEST_CASE("Display limits separate executable and published quantity", "[order]")
{
    const matching::Order iceberg = matching::Order::BuyLimit(
        91, 7, 18'500, 100, matching::OrderTimeInForce::GTC, 10);
    const matching::Order hidden = matching::Order::SellLimit(
        92, 7, 18'510, 40, matching::OrderTimeInForce::GTC, 0);
    const matching::Order fully_visible = matching::Order::BuyLimit(
        93, 7, 18'500, 25, matching::OrderTimeInForce::GTC, 50);

    REQUIRE(iceberg.quantity == 100);
    REQUIRE(iceberg.displayed_quantity() == 10);
    REQUIRE(iceberg.hidden_quantity() == 90);
    REQUIRE(iceberg.is_display_limited());
    REQUIRE_FALSE(iceberg.is_hidden());

    REQUIRE(hidden.displayed_quantity() == 0);
    REQUIRE(hidden.hidden_quantity() == 40);
    REQUIRE(hidden.is_hidden());

    REQUIRE(fully_visible.displayed_quantity() == 25);
    REQUIRE(fully_visible.hidden_quantity() == 0);
    REQUIRE(fully_visible.is_display_limited());

    REQUIRE(iceberg.validate() == matching::ErrorCode::OK);
    REQUIRE(hidden.validate() == matching::ErrorCode::OK);
    REQUIRE(fully_visible.validate() == matching::ErrorCode::OK);
}

TEST_CASE("Display limits are rejected for order types that cannot rest visibly",
          "[order]")
{
    matching::Order market = matching::Order::BuyMarket(101, 7, 10);
    matching::Order stop = matching::Order::SellStop(102, 7, 18'400, 10);
    matching::Order trailing =
        matching::Order::TrailingBuyStop(103, 7, 18'600, 10, 100);
    const matching::Order stop_limit = matching::Order::BuyStopLimit(
        104, 7, 18'600, 18'610, 20, matching::OrderTimeInForce::GTC, 5);
    const matching::Order trailing_stop_limit =
        matching::Order::TrailingSellStopLimit(
            105, 7, 18'400, 18'390, 20, 100, 0,
            matching::OrderTimeInForce::GTC, 5);

    market.max_display_quantity = 1;
    stop.max_display_quantity = 1;
    trailing.max_display_quantity = 1;

    REQUIRE(market.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(trailing.validate() == matching::ErrorCode::ORDER_PARAMETER_INVALID);
    REQUIRE(stop_limit.validate() == matching::ErrorCode::OK);
    REQUIRE(trailing_stop_limit.validate() == matching::ErrorCode::OK);
}
