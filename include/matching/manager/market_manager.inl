namespace matching {

inline MarketManager::MarketManager()
    : MarketManager(default_handler_)
{
}

inline MarketManager::MarketManager(MarketHandler& market_handler)
    : market_handler_(market_handler),
      level_pool_(),
      symbol_pool_(),
      order_book_pool_(),
      order_pool_(),
      active_orders_(INITIAL_ORDER_BUCKET_COUNT, 0)
{
}

inline const Symbol* MarketManager::find_symbol(SymbolId symbol_id) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(symbol_id);
    return (index < symbols_.size()) ? symbols_[index] : nullptr;
}

inline const OrderBook* MarketManager::find_order_book(SymbolId symbol_id) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(symbol_id);
    return (index < order_books_.size()) ? order_books_[index] : nullptr;
}

inline OrderBook* MarketManager::find_order_book_mutable(SymbolId symbol_id) noexcept
{
    const std::size_t index = static_cast<std::size_t>(symbol_id);
    return (index < order_books_.size()) ? order_books_[index] : nullptr;
}

inline const Order* MarketManager::find_order(OrderId order_id) const noexcept
{
    OrderNode* const* order = active_orders_.find(order_id);
    return (order != nullptr) ? *order : nullptr;
}

} // namespace matching
