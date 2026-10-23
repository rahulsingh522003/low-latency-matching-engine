#include "matching/errors.h"
#include "matching/level.h"
#include "matching/events/event_handler.h"
#include "matching/manager/market_manager.h"
#include "matching/order.h"
#include "matching/book/order_book.h"
#include "matching/symbol.h"
#include "matching/types.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_order(std::string_view prefix, const matching::Order& order)
{
    std::cout << prefix << "id=" << order.id << " symbol=" << order.symbol_id
              << " type=" << static_cast<unsigned>(order.type)
              << " side=" << (order.is_buy() ? "buy" : "sell")
              << " tif=" << static_cast<unsigned>(order.time_in_force)
              << " price=" << order.price << " stop=" << order.stop_price
              << " quantity=" << order.quantity
              << " display-limit=" << order.max_display_quantity
              << " slippage=" << order.slippage
              << " trailing-distance=" << order.trailing_distance
              << " trailing-step=" << order.trailing_step << '\n';
}

class PrintingHandler final : public matching::EventHandler
{
protected:
    void on_add_symbol(const matching::Symbol& symbol) noexcept override
    {
        std::cout << "event add-symbol " << symbol << '\n';
    }

    void on_delete_symbol(const matching::Symbol& symbol) noexcept override
    {
        std::cout << "event delete-symbol " << symbol << '\n';
    }

    void on_add_order_book(const matching::OrderBook& book) noexcept override
    {
        std::cout << "event add-book " << book << '\n';
    }

    void on_update_order_book(const matching::OrderBook& book,
                              bool top) noexcept override
    {
        std::cout << "event update-book " << book << " top=" << top << '\n';
    }

    void on_delete_order_book(const matching::OrderBook& book) noexcept override
    {
        std::cout << "event delete-book " << book << '\n';
    }

    void on_add_level(const matching::OrderBook& book,
                      const matching::Level& level,
                      bool top) noexcept override
    {
        print_level("add-level", book, level, top);
    }

    void on_update_level(const matching::OrderBook& book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        print_level("update-level", book, level, top);
    }

    void on_delete_level(const matching::OrderBook& book,
                         const matching::Level& level,
                         bool top) noexcept override
    {
        print_level("delete-level", book, level, top);
    }

    void on_add_order(const matching::Order& order) noexcept override
    {
        print_order("event add-order ", order);
    }

    void on_update_order(const matching::Order& order) noexcept override
    {
        print_order("event update-order ", order);
    }

    void on_delete_order(const matching::Order& order) noexcept override
    {
        print_order("event delete-order ", order);
    }

    void on_execute_order(const matching::Order& order,
                          matching::Price price,
                          matching::Quantity quantity) noexcept override
    {
        std::cout << "event execute-order id=" << order.id
                  << " symbol=" << order.symbol_id << " price=" << price
                  << " quantity=" << quantity
                  << " leaves=" << order.quantity << '\n';
    }

private:
    static void print_level(std::string_view event,
                            const matching::OrderBook& book,
                            const matching::Level& level,
                            bool top) noexcept
    {
        std::cout << "event " << event << " symbol=" << book.symbol().id << ' '
                  << level << " top=" << top << '\n';
    }
};

using Tokens = std::vector<std::string_view>;

[[nodiscard]] Tokens tokenize(std::string_view line)
{
    Tokens tokens;
    std::size_t position = 0;
    while (position < line.size())
    {
        while ((position < line.size()) &&
               std::isspace(static_cast<unsigned char>(line[position])))
        {
            ++position;
        }
        if ((position == line.size()) || (line[position] == '#'))
            break;

        const std::size_t start = position;
        while ((position < line.size()) &&
               !std::isspace(static_cast<unsigned char>(line[position])))
        {
            ++position;
        }
        tokens.push_back(line.substr(start, position - start));
    }
    return tokens;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view text, Integer& value) noexcept
{
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return (result.ec == std::errc{}) && (result.ptr == end);
}

[[nodiscard]] bool parse_side(std::string_view text,
                              matching::OrderSide& side) noexcept
{
    if (text == "buy")
    {
        side = matching::OrderSide::BUY;
        return true;
    }
    if (text == "sell")
    {
        side = matching::OrderSide::SELL;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_time_in_force(
    std::string_view text,
    matching::OrderTimeInForce& time_in_force) noexcept
{
    if (text == "gtc")
        time_in_force = matching::OrderTimeInForce::GTC;
    else if (text == "ioc")
        time_in_force = matching::OrderTimeInForce::IOC;
    else if (text == "fok")
        time_in_force = matching::OrderTimeInForce::FOK;
    else if (text == "aon")
        time_in_force = matching::OrderTimeInForce::AON;
    else
        return false;
    return true;
}

void print_help()
{
    std::cout
        << "Prices use integer cents: 18525 means 185.25.\n"
        << "Commands:\n"
        << "  enable matching | disable matching | run matching\n"
        << "  add symbol <symbol-id> <name>\n"
        << "  delete symbol <symbol-id>\n"
        << "  add book <symbol-id> | delete book <symbol-id>\n"
        << "  add order market <buy|sell> <id> <symbol-id> <quantity> "
           "[ioc|fok] [slippage]\n"
        << "  add order limit <buy|sell> <id> <symbol-id> <price> <quantity> "
           "[gtc|ioc|fok|aon] [display-quantity]\n"
        << "  add order stop <buy|sell> <id> <symbol-id> <stop-price> "
           "<quantity> [gtc|ioc|fok] [slippage]\n"
        << "  add order stop-limit <buy|sell> <id> <symbol-id> <stop-price> "
           "<limit-price> <quantity> [gtc|ioc|fok|aon] [display-quantity]\n"
        << "  add order trailing-stop <buy|sell> <id> <symbol-id> <stop-price> "
           "<quantity> <distance> <step> [gtc|ioc|fok] [slippage]\n"
        << "  add order trailing-stop-limit <buy|sell> <id> <symbol-id> "
           "<stop-price> <limit-price> <quantity> <distance> <step> "
           "[gtc|ioc|fok|aon] [display-quantity]\n"
        << "  reduce order <symbol-id> <id> <quantity>\n"
        << "  modify order <symbol-id> <id> <new-price> <new-quantity>\n"
        << "  replace order <symbol-id> <id> <new-id> <price> <quantity>\n"
        << "  delete order <symbol-id> <id>\n"
        << "  execute order <symbol-id> <id> <quantity> [price]\n"
        << "  show book <symbol-id> | show order <id>\n"
        << "  help | quit\n";
}

class CommandProcessor
{
public:
    explicit CommandProcessor(matching::MarketManager& market) noexcept
        : market_(market)
    {
    }

    [[nodiscard]] bool process(std::string_view line)
    {
        const Tokens tokens = tokenize(line);
        if (tokens.empty())
            return true;
        if ((tokens[0] == "quit") || (tokens[0] == "exit"))
            return false;
        if ((tokens.size() == 1) && (tokens[0] == "help"))
        {
            print_help();
            return true;
        }
        if ((tokens.size() == 2) && (tokens[1] == "matching"))
        {
            if (tokens[0] == "enable")
                market_.enable_matching();
            else if (tokens[0] == "disable")
                market_.disable_matching();
            else if (tokens[0] == "run")
                market_.match();
            else
                return syntax_error();
            std::cout << "OK\n";
            return true;
        }
        if ((tokens.size() >= 2) && (tokens[0] == "add") &&
            (tokens[1] == "symbol"))
        {
            return add_symbol(tokens);
        }
        if ((tokens.size() >= 2) && (tokens[0] == "delete") &&
            (tokens[1] == "symbol"))
        {
            return delete_symbol(tokens);
        }
        if ((tokens.size() >= 2) && (tokens[0] == "show"))
            return show(tokens);
        if ((tokens.size() >= 2) && (tokens[1] == "book"))
            return change_book(tokens);
        if ((tokens.size() >= 2) && (tokens[0] == "add") &&
            (tokens[1] == "order"))
        {
            return add_order(tokens);
        }
        if ((tokens.size() >= 2) && (tokens[1] == "order"))
            return change_order(tokens);
        return syntax_error();
    }

private:
    matching::MarketManager& market_;

    static bool syntax_error()
    {
        std::cerr << "ERROR invalid command; enter 'help'\n";
        return true;
    }

    static bool report(matching::ErrorCode result)
    {
        if (result == matching::ErrorCode::OK)
            std::cout << "OK\n";
        else
            std::cerr << "ERROR " << result << '\n';
        return true;
    }

    bool add_symbol(const Tokens& tokens)
    {
        matching::SymbolId id = 0;
        if ((tokens.size() != 4) || !parse_integer(tokens[2], id) ||
            (tokens[3].size() > matching::SymbolName{}.size()))
        {
            return syntax_error();
        }

        matching::SymbolName name;
        name.fill(' ');
        std::copy(tokens[3].begin(), tokens[3].end(), name.begin());
        try
        {
            return report(market_.add_symbol(matching::Symbol(id, name)));
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "ERROR allocation failure\n";
            return true;
        }
    }

    bool delete_symbol(const Tokens& tokens)
    {
        matching::SymbolId id = 0;
        if ((tokens.size() != 3) || !parse_integer(tokens[2], id))
            return syntax_error();
        return report(market_.delete_symbol(id));
    }

    bool change_book(const Tokens& tokens)
    {
        matching::SymbolId id = 0;
        if ((tokens.size() != 3) || !parse_integer(tokens[2], id))
            return syntax_error();

        try
        {
            if (tokens[0] == "add")
                return report(market_.add_order_book(id));
            if (tokens[0] == "delete")
                return report(market_.delete_order_book(id));
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "ERROR allocation failure\n";
            return true;
        }
        return syntax_error();
    }

    bool add_order(const Tokens& tokens)
    {
        if (tokens.size() < 7)
            return syntax_error();

        matching::OrderSide side;
        matching::OrderId id = 0;
        matching::SymbolId symbol_id = 0;
        if (!parse_side(tokens[3], side) || !parse_integer(tokens[4], id) ||
            !parse_integer(tokens[5], symbol_id))
        {
            return syntax_error();
        }

        matching::Order order;
        const std::string_view kind = tokens[2];
        if (kind == "market")
        {
            if (!make_market_order(tokens, side, id, symbol_id, order))
                return syntax_error();
        }
        else if (kind == "limit")
        {
            if (!make_limit_order(tokens, side, id, symbol_id, order))
                return syntax_error();
        }
        else if (kind == "stop")
        {
            if (!make_stop_order(tokens, side, id, symbol_id, order))
                return syntax_error();
        }
        else if (kind == "stop-limit")
        {
            if (!make_stop_limit_order(tokens, side, id, symbol_id, order))
                return syntax_error();
        }
        else if (kind == "trailing-stop")
        {
            if (!make_trailing_stop_order(tokens, side, id, symbol_id, order))
                return syntax_error();
        }
        else if (kind == "trailing-stop-limit")
        {
            if (!make_trailing_stop_limit_order(
                    tokens, side, id, symbol_id, order))
            {
                return syntax_error();
            }
        }
        else
            return syntax_error();

        try
        {
            return report(market_.add_order(order));
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "ERROR allocation failure\n";
            return true;
        }
    }

    static bool make_market_order(const Tokens& tokens,
                                  matching::OrderSide side,
                                  matching::OrderId id,
                                  matching::SymbolId symbol_id,
                                  matching::Order& order) noexcept
    {
        if ((tokens.size() < 7) || (tokens.size() > 9))
            return false;
        matching::Quantity quantity = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::IOC;
        matching::Price slippage = matching::Order::UNLIMITED_SLIPPAGE;
        if (!parse_integer(tokens[6], quantity))
            return false;
        if ((tokens.size() >= 8) && !parse_time_in_force(tokens[7], tif))
            return false;
        if ((tokens.size() == 9) && !parse_integer(tokens[8], slippage))
            return false;
        order = matching::Order::Market(id, symbol_id, side, quantity, tif, slippage);
        return true;
    }

    static bool make_limit_order(const Tokens& tokens,
                                 matching::OrderSide side,
                                 matching::OrderId id,
                                 matching::SymbolId symbol_id,
                                 matching::Order& order) noexcept
    {
        if ((tokens.size() < 8) || (tokens.size() > 10))
            return false;
        matching::Price price = 0;
        matching::Quantity quantity = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::GTC;
        matching::Quantity display = matching::Order::FULLY_DISPLAYED;
        if (!parse_integer(tokens[6], price) ||
            !parse_integer(tokens[7], quantity))
        {
            return false;
        }
        if ((tokens.size() >= 9) && !parse_time_in_force(tokens[8], tif))
            return false;
        if ((tokens.size() == 10) && !parse_integer(tokens[9], display))
            return false;
        order = matching::Order::Limit(
            id, symbol_id, side, price, quantity, tif, display);
        return true;
    }

    static bool make_stop_order(const Tokens& tokens,
                                matching::OrderSide side,
                                matching::OrderId id,
                                matching::SymbolId symbol_id,
                                matching::Order& order) noexcept
    {
        if ((tokens.size() < 8) || (tokens.size() > 10))
            return false;
        matching::Price stop_price = 0;
        matching::Quantity quantity = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::GTC;
        matching::Price slippage = matching::Order::UNLIMITED_SLIPPAGE;
        if (!parse_integer(tokens[6], stop_price) ||
            !parse_integer(tokens[7], quantity))
        {
            return false;
        }
        if ((tokens.size() >= 9) && !parse_time_in_force(tokens[8], tif))
            return false;
        if ((tokens.size() == 10) && !parse_integer(tokens[9], slippage))
            return false;
        order = matching::Order::Stop(
            id, symbol_id, side, stop_price, quantity, tif, slippage);
        return true;
    }

    static bool make_stop_limit_order(const Tokens& tokens,
                                      matching::OrderSide side,
                                      matching::OrderId id,
                                      matching::SymbolId symbol_id,
                                      matching::Order& order) noexcept
    {
        if ((tokens.size() < 9) || (tokens.size() > 11))
            return false;
        matching::Price stop_price = 0;
        matching::Price price = 0;
        matching::Quantity quantity = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::GTC;
        matching::Quantity display = matching::Order::FULLY_DISPLAYED;
        if (!parse_integer(tokens[6], stop_price) ||
            !parse_integer(tokens[7], price) ||
            !parse_integer(tokens[8], quantity))
        {
            return false;
        }
        if ((tokens.size() >= 10) && !parse_time_in_force(tokens[9], tif))
            return false;
        if ((tokens.size() == 11) && !parse_integer(tokens[10], display))
            return false;
        order = matching::Order::StopLimit(
            id, symbol_id, side, stop_price, price, quantity, tif, display);
        return true;
    }

    static bool make_trailing_stop_order(const Tokens& tokens,
                                         matching::OrderSide side,
                                         matching::OrderId id,
                                         matching::SymbolId symbol_id,
                                         matching::Order& order) noexcept
    {
        if ((tokens.size() < 10) || (tokens.size() > 12))
            return false;
        matching::Price stop_price = 0;
        matching::Quantity quantity = 0;
        std::int64_t distance = 0;
        std::int64_t step = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::GTC;
        matching::Price slippage = matching::Order::UNLIMITED_SLIPPAGE;
        if (!parse_integer(tokens[6], stop_price) ||
            !parse_integer(tokens[7], quantity) ||
            !parse_integer(tokens[8], distance) ||
            !parse_integer(tokens[9], step))
        {
            return false;
        }
        if ((tokens.size() >= 11) && !parse_time_in_force(tokens[10], tif))
            return false;
        if ((tokens.size() == 12) && !parse_integer(tokens[11], slippage))
            return false;
        order = matching::Order::TrailingStop(id,
                                               symbol_id,
                                               side,
                                               stop_price,
                                               quantity,
                                               distance,
                                               step,
                                               tif,
                                               slippage);
        return true;
    }

    static bool make_trailing_stop_limit_order(
        const Tokens& tokens,
        matching::OrderSide side,
        matching::OrderId id,
        matching::SymbolId symbol_id,
        matching::Order& order) noexcept
    {
        if ((tokens.size() < 11) || (tokens.size() > 13))
            return false;
        matching::Price stop_price = 0;
        matching::Price price = 0;
        matching::Quantity quantity = 0;
        std::int64_t distance = 0;
        std::int64_t step = 0;
        matching::OrderTimeInForce tif = matching::OrderTimeInForce::GTC;
        matching::Quantity display = matching::Order::FULLY_DISPLAYED;
        if (!parse_integer(tokens[6], stop_price) ||
            !parse_integer(tokens[7], price) ||
            !parse_integer(tokens[8], quantity) ||
            !parse_integer(tokens[9], distance) ||
            !parse_integer(tokens[10], step))
        {
            return false;
        }
        if ((tokens.size() >= 12) && !parse_time_in_force(tokens[11], tif))
            return false;
        if ((tokens.size() == 13) && !parse_integer(tokens[12], display))
            return false;
        order = matching::Order::TrailingStopLimit(id,
                                                    symbol_id,
                                                    side,
                                                    stop_price,
                                                    price,
                                                    quantity,
                                                    distance,
                                                    step,
                                                    tif,
                                                    display);
        return true;
    }

    bool change_order(const Tokens& tokens)
    {
        matching::SymbolId symbol_id = 0;
        matching::OrderId order_id = 0;
        if ((tokens.size() < 4) || !parse_integer(tokens[2], symbol_id) ||
            !parse_integer(tokens[3], order_id))
        {
            return syntax_error();
        }

        try
        {
            if ((tokens[0] == "reduce") && (tokens.size() == 5))
            {
                matching::Quantity quantity = 0;
                return parse_integer(tokens[4], quantity)
                           ? report(market_.reduce_order(
                                 symbol_id, order_id, quantity))
                           : syntax_error();
            }
            if ((tokens[0] == "modify") && (tokens.size() == 6))
            {
                matching::Price price = 0;
                matching::Quantity quantity = 0;
                return (parse_integer(tokens[4], price) &&
                        parse_integer(tokens[5], quantity))
                           ? report(market_.modify_order(
                                 symbol_id, order_id, price, quantity))
                           : syntax_error();
            }
            if ((tokens[0] == "replace") && (tokens.size() == 7))
            {
                matching::OrderId new_id = 0;
                matching::Price price = 0;
                matching::Quantity quantity = 0;
                return (parse_integer(tokens[4], new_id) &&
                        parse_integer(tokens[5], price) &&
                        parse_integer(tokens[6], quantity))
                           ? report(market_.replace_order(symbol_id,
                                                          order_id,
                                                          new_id,
                                                          price,
                                                          quantity))
                           : syntax_error();
            }
            if ((tokens[0] == "delete") && (tokens.size() == 4))
                return report(market_.delete_order(symbol_id, order_id));
            if ((tokens[0] == "execute") &&
                ((tokens.size() == 5) || (tokens.size() == 6)))
            {
                matching::Quantity quantity = 0;
                if (!parse_integer(tokens[4], quantity))
                    return syntax_error();
                if (tokens.size() == 5)
                    return report(
                        market_.execute_order(symbol_id, order_id, quantity));

                matching::Price price = 0;
                return parse_integer(tokens[5], price)
                           ? report(market_.execute_order(
                                 symbol_id, order_id, price, quantity))
                           : syntax_error();
            }
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "ERROR allocation failure\n";
            return true;
        }
        return syntax_error();
    }

    bool show(const Tokens& tokens) const
    {
        if ((tokens.size() == 3) && (tokens[1] == "book"))
        {
            matching::SymbolId symbol_id = 0;
            if (!parse_integer(tokens[2], symbol_id))
                return syntax_error();
            const matching::OrderBook* book = market_.find_order_book(symbol_id);
            if (book == nullptr)
                std::cerr << "ERROR " << matching::ErrorCode::ORDER_BOOK_NOT_FOUND
                          << '\n';
            else
                std::cout << *book << '\n';
            return true;
        }
        if ((tokens.size() == 3) && (tokens[1] == "order"))
        {
            matching::OrderId order_id = 0;
            if (!parse_integer(tokens[2], order_id))
                return syntax_error();
            const matching::Order* order = market_.find_order(order_id);
            if (order == nullptr)
                std::cerr << "ERROR " << matching::ErrorCode::ORDER_NOT_FOUND
                          << '\n';
            else
                print_order("", *order);
            return true;
        }
        return syntax_error();
    }
};

} // namespace

int main()
{
    PrintingHandler handler;
    matching::MarketManager market(handler);
    CommandProcessor commands(market);

    std::cout << "Matching engine console. Enter 'help' for commands.\n";
    std::string line;
    while (std::getline(std::cin, line) && commands.process(line))
    {
    }
    return 0;
}
