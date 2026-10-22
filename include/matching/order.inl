namespace matching {

inline Order::Order(OrderId order_id,
                    SymbolId symbol,
                    OrderType order_type,
                    OrderSide order_side,
                    Price order_price,
                    Price order_stop_price,
                    Quantity order_quantity,
                    OrderTimeInForce tif,
                    std::int64_t order_trailing_distance,
                    std::int64_t order_trailing_step,
                    Price order_slippage,
                    Quantity order_max_display_quantity) noexcept
    : id(order_id),
      symbol_id(symbol),
      type(order_type),
      side(order_side),
      time_in_force(tif),
      price(order_price),
      stop_price(order_stop_price),
      quantity(order_quantity),
      max_display_quantity(order_max_display_quantity),
      slippage(order_slippage),
      trailing_distance(order_trailing_distance),
      trailing_step(order_trailing_step)
{
}

inline Order Order::Market(OrderId order_id,
                           SymbolId symbol,
                           OrderSide order_side,
                           Quantity order_quantity,
                           OrderTimeInForce tif,
                           Price order_slippage) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::MARKET,
                 order_side,
                 0,
                 0,
                 order_quantity,
                 tif,
                 0,
                 0,
                 order_slippage);
}

inline Order Order::BuyMarket(OrderId order_id,
                              SymbolId symbol,
                              Quantity order_quantity,
                              OrderTimeInForce tif,
                              Price order_slippage) noexcept
{
    return Market(order_id, symbol, OrderSide::BUY, order_quantity, tif, order_slippage);
}

inline Order Order::SellMarket(OrderId order_id,
                               SymbolId symbol,
                               Quantity order_quantity,
                               OrderTimeInForce tif,
                               Price order_slippage) noexcept
{
    return Market(order_id, symbol, OrderSide::SELL, order_quantity, tif, order_slippage);
}

inline Order Order::Limit(OrderId order_id,
                          SymbolId symbol,
                          OrderSide order_side,
                          Price order_price,
                          Quantity order_quantity,
                          OrderTimeInForce tif,
                          Quantity order_max_display_quantity) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::LIMIT,
                 order_side,
                 order_price,
                 0,
                 order_quantity,
                 tif,
                 0,
                 0,
                 UNLIMITED_SLIPPAGE,
                 order_max_display_quantity);
}

inline Order Order::BuyLimit(OrderId order_id,
                             SymbolId symbol,
                             Price order_price,
                             Quantity order_quantity,
                             OrderTimeInForce tif,
                             Quantity order_max_display_quantity) noexcept
{
    return Limit(order_id,
                 symbol,
                 OrderSide::BUY,
                 order_price,
                 order_quantity,
                 tif,
                 order_max_display_quantity);
}

inline Order Order::SellLimit(OrderId order_id,
                              SymbolId symbol,
                              Price order_price,
                              Quantity order_quantity,
                              OrderTimeInForce tif,
                              Quantity order_max_display_quantity) noexcept
{
    return Limit(order_id,
                 symbol,
                 OrderSide::SELL,
                 order_price,
                 order_quantity,
                 tif,
                 order_max_display_quantity);
}

inline Order Order::Stop(OrderId order_id,
                         SymbolId symbol,
                         OrderSide order_side,
                         Price order_stop_price,
                         Quantity order_quantity,
                         OrderTimeInForce tif,
                         Price order_slippage) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::STOP,
                 order_side,
                 0,
                 order_stop_price,
                 order_quantity,
                 tif,
                 0,
                 0,
                 order_slippage);
}

inline Order Order::BuyStop(OrderId order_id,
                            SymbolId symbol,
                            Price order_stop_price,
                            Quantity order_quantity,
                            OrderTimeInForce tif,
                            Price order_slippage) noexcept
{
    return Stop(order_id,
                symbol,
                OrderSide::BUY,
                order_stop_price,
                order_quantity,
                tif,
                order_slippage);
}

inline Order Order::SellStop(OrderId order_id,
                             SymbolId symbol,
                             Price order_stop_price,
                             Quantity order_quantity,
                             OrderTimeInForce tif,
                             Price order_slippage) noexcept
{
    return Stop(order_id,
                symbol,
                OrderSide::SELL,
                order_stop_price,
                order_quantity,
                tif,
                order_slippage);
}

inline Order Order::StopLimit(OrderId order_id,
                              SymbolId symbol,
                              OrderSide order_side,
                              Price order_stop_price,
                              Price order_price,
                              Quantity order_quantity,
                              OrderTimeInForce tif,
                              Quantity order_max_display_quantity) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::STOP_LIMIT,
                 order_side,
                 order_price,
                 order_stop_price,
                 order_quantity,
                 tif,
                 0,
                 0,
                 UNLIMITED_SLIPPAGE,
                 order_max_display_quantity);
}

inline Order Order::BuyStopLimit(OrderId order_id,
                                 SymbolId symbol,
                                 Price order_stop_price,
                                 Price order_price,
                                 Quantity order_quantity,
                                 OrderTimeInForce tif,
                                 Quantity order_max_display_quantity) noexcept
{
    return StopLimit(order_id,
                     symbol,
                     OrderSide::BUY,
                     order_stop_price,
                     order_price,
                     order_quantity,
                     tif,
                     order_max_display_quantity);
}

inline Order Order::SellStopLimit(OrderId order_id,
                                  SymbolId symbol,
                                  Price order_stop_price,
                                  Price order_price,
                                  Quantity order_quantity,
                                  OrderTimeInForce tif,
                                  Quantity order_max_display_quantity) noexcept
{
    return StopLimit(order_id,
                     symbol,
                     OrderSide::SELL,
                     order_stop_price,
                     order_price,
                     order_quantity,
                     tif,
                     order_max_display_quantity);
}

inline Order Order::TrailingStop(OrderId order_id,
                                 SymbolId symbol,
                                 OrderSide order_side,
                                 Price order_stop_price,
                                 Quantity order_quantity,
                                 std::int64_t order_trailing_distance,
                                 std::int64_t order_trailing_step,
                                 OrderTimeInForce tif,
                                 Price order_slippage) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::TRAILING_STOP,
                 order_side,
                 0,
                 order_stop_price,
                 order_quantity,
                 tif,
                 order_trailing_distance,
                 order_trailing_step,
                 order_slippage);
}

inline Order Order::TrailingBuyStop(OrderId order_id,
                                    SymbolId symbol,
                                    Price order_stop_price,
                                    Quantity order_quantity,
                                    std::int64_t order_trailing_distance,
                                    std::int64_t order_trailing_step,
                                    OrderTimeInForce tif,
                                    Price order_slippage) noexcept
{
    return TrailingStop(order_id,
                        symbol,
                        OrderSide::BUY,
                        order_stop_price,
                        order_quantity,
                        order_trailing_distance,
                        order_trailing_step,
                        tif,
                        order_slippage);
}

inline Order Order::TrailingSellStop(OrderId order_id,
                                     SymbolId symbol,
                                     Price order_stop_price,
                                     Quantity order_quantity,
                                     std::int64_t order_trailing_distance,
                                     std::int64_t order_trailing_step,
                                     OrderTimeInForce tif,
                                     Price order_slippage) noexcept
{
    return TrailingStop(order_id,
                        symbol,
                        OrderSide::SELL,
                        order_stop_price,
                        order_quantity,
                        order_trailing_distance,
                        order_trailing_step,
                        tif,
                        order_slippage);
}

inline Order Order::TrailingStopLimit(OrderId order_id,
                                      SymbolId symbol,
                                      OrderSide order_side,
                                      Price order_stop_price,
                                      Price order_price,
                                      Quantity order_quantity,
                                      std::int64_t order_trailing_distance,
                                      std::int64_t order_trailing_step,
                                      OrderTimeInForce tif,
                                      Quantity order_max_display_quantity) noexcept
{
    return Order(order_id,
                 symbol,
                 OrderType::TRAILING_STOP_LIMIT,
                 order_side,
                 order_price,
                 order_stop_price,
                 order_quantity,
                 tif,
                 order_trailing_distance,
                 order_trailing_step,
                 UNLIMITED_SLIPPAGE,
                 order_max_display_quantity);
}

inline Order Order::TrailingBuyStopLimit(OrderId order_id,
                                         SymbolId symbol,
                                         Price order_stop_price,
                                         Price order_price,
                                         Quantity order_quantity,
                                         std::int64_t order_trailing_distance,
                                         std::int64_t order_trailing_step,
                                         OrderTimeInForce tif,
                                         Quantity order_max_display_quantity) noexcept
{
    return TrailingStopLimit(order_id,
                             symbol,
                             OrderSide::BUY,
                             order_stop_price,
                             order_price,
                             order_quantity,
                             order_trailing_distance,
                             order_trailing_step,
                             tif,
                             order_max_display_quantity);
}

inline Order Order::TrailingSellStopLimit(OrderId order_id,
                                          SymbolId symbol,
                                          Price order_stop_price,
                                          Price order_price,
                                          Quantity order_quantity,
                                          std::int64_t order_trailing_distance,
                                          std::int64_t order_trailing_step,
                                          OrderTimeInForce tif,
                                          Quantity order_max_display_quantity) noexcept
{
    return TrailingStopLimit(order_id,
                             symbol,
                             OrderSide::SELL,
                             order_stop_price,
                             order_price,
                             order_quantity,
                             order_trailing_distance,
                             order_trailing_step,
                             tif,
                             order_max_display_quantity);
}

inline OrderNode::OrderNode(const Order& order) noexcept
    : Order(order)
{
}

} // namespace matching
