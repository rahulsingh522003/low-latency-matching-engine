namespace matching {

template <typename Tree>
inline auto OrderBook::find_level(Tree& levels, Price price) noexcept -> decltype(levels.root())
{
    auto* current = levels.root();

    while (current != nullptr)
    {
        if (price < current->price)
            current = current->left;
        else if (price > current->price)
            current = current->right;
        else
            return current;
    }

    return nullptr;
}

inline const Level* OrderBook::find_bid(Price price) const noexcept
{
    return find_level(bids_, price);
}

inline const Level* OrderBook::find_ask(Price price) const noexcept
{
    return find_level(asks_, price);
}

inline const Level* OrderBook::find_buy_stop(Price stop_price) const noexcept
{
    return find_level(buy_stops_, stop_price);
}

inline const Level* OrderBook::find_sell_stop(Price stop_price) const noexcept
{
    return find_level(sell_stops_, stop_price);
}

inline const Level* OrderBook::find_trailing_buy_stop(Price stop_price) const noexcept
{
    return find_level(trailing_buy_stops_, stop_price);
}

inline const Level* OrderBook::find_trailing_sell_stop(Price stop_price) const noexcept
{
    return find_level(trailing_sell_stops_, stop_price);
}

inline const LevelNode* OrderBook::next_level(const LevelNode& level) const noexcept
{
    assert((level.is_bid() || level.is_ask()) && "Price level has an invalid side");
    assert(((level.is_bid() ? find_level(bids_, level.price)
                            : find_level(asks_, level.price)) == &level) &&
           "Price level does not belong to this order book");

    return level.is_bid() ? bids_.previous(level) : asks_.next(level);
}

inline const LevelNode* OrderBook::next_stop_level(const LevelNode& level) const noexcept
{
    assert((level.is_bid() || level.is_ask()) && "Stop level has an invalid type");
    assert(((level.is_bid() ? find_level(sell_stops_, level.price)
                            : find_level(buy_stops_, level.price)) == &level) &&
           "Stop level does not belong to this order book");

    return level.is_bid() ? sell_stops_.previous(level) : buy_stops_.next(level);
}

inline const LevelNode* OrderBook::next_trailing_stop_level(
    const LevelNode& level) const noexcept
{
    assert((level.is_bid() || level.is_ask()) && "Trailing-stop level has an invalid type");
    assert(((level.is_bid() ? find_level(trailing_sell_stops_, level.price)
                            : find_level(trailing_buy_stops_, level.price)) == &level) &&
           "Trailing-stop level does not belong to this order book");

    return level.is_bid() ? trailing_sell_stops_.previous(level)
                          : trailing_buy_stops_.next(level);
}

inline Price OrderBook::market_bid_price() const noexcept
{
    const Price best_price = (best_bid_ != nullptr) ? best_bid_->price : 0;
    return std::max(matching_bid_price_, best_price);
}

inline Price OrderBook::market_ask_price() const noexcept
{
    const Price best_price = (best_ask_ != nullptr) ? best_ask_->price
                                                    : std::numeric_limits<Price>::max();
    return std::min(matching_ask_price_, best_price);
}

inline Price OrderBook::market_trailing_bid_price() const noexcept
{
    const Price best_price = (best_bid_ != nullptr) ? best_bid_->price : 0;
    return std::min(last_bid_price_, best_price);
}

inline Price OrderBook::market_trailing_ask_price() const noexcept
{
    const Price best_price = (best_ask_ != nullptr) ? best_ask_->price
                                                    : std::numeric_limits<Price>::max();
    return std::max(last_ask_price_, best_price);
}

inline void OrderBook::update_last_price(const Order& order, Price price) noexcept
{
    if (order.is_buy())
        last_bid_price_ = price;
    else
        last_ask_price_ = price;
}

inline void OrderBook::update_matching_price(const Order& order, Price price) noexcept
{
    if (order.is_buy())
        matching_bid_price_ = price;
    else
        matching_ask_price_ = price;
}

inline void OrderBook::reset_matching_price() noexcept
{
    matching_bid_price_ = 0;
    matching_ask_price_ = std::numeric_limits<Price>::max();
}

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, const OrderBook& order_book)
{
    stream << "OrderBook(Symbol=" << order_book.symbol_ << "; BidLevels="
           << order_book.bids_.size() << "; AskLevels=" << order_book.asks_.size()
           << "; BuyStopLevels=" << order_book.buy_stops_.size()
           << "; SellStopLevels=" << order_book.sell_stops_.size()
           << "; TrailingBuyStopLevels=" << order_book.trailing_buy_stops_.size()
           << "; TrailingSellStopLevels=" << order_book.trailing_sell_stops_.size()
           << ")";
    return stream;
}

} // namespace matching
