namespace providers::nasdaq::itch {

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, MarketAdapterStatus status)
{
    switch (status)
    {
        case MarketAdapterStatus::OK:
            return stream << "OK";
        case MarketAdapterStatus::MATCHING_MUST_BE_DISABLED:
            return stream << "MATCHING_MUST_BE_DISABLED";
        case MarketAdapterStatus::INVALID_ORDER_SIDE:
            return stream << "INVALID_ORDER_SIDE";
        case MarketAdapterStatus::PRICE_NOT_REPRESENTABLE:
            return stream << "PRICE_NOT_REPRESENTABLE";
        case MarketAdapterStatus::ENGINE_REJECTED:
            return stream << "ENGINE_REJECTED";
        case MarketAdapterStatus::ALLOCATION_FAILURE:
            return stream << "ALLOCATION_FAILURE";
        case MarketAdapterStatus::INTERNAL_FAILURE:
            return stream << "INTERNAL_FAILURE";
    }

    return stream << "UNKNOWN_MARKET_ADAPTER_STATUS";
}

} // namespace providers::nasdaq::itch
