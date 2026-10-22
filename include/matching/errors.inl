namespace matching {

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, ErrorCode error)
{
    switch (error)
    {
        case ErrorCode::OK:
            stream << "OK";
            break;
        case ErrorCode::SYMBOL_DUPLICATE:
            stream << "SYMBOL_DUPLICATE";
            break;
        case ErrorCode::SYMBOL_NOT_FOUND:
            stream << "SYMBOL_NOT_FOUND";
            break;
        case ErrorCode::SYMBOL_IN_USE:
            stream << "SYMBOL_IN_USE";
            break;
        case ErrorCode::ORDER_BOOK_DUPLICATE:
            stream << "ORDER_BOOK_DUPLICATE";
            break;
        case ErrorCode::ORDER_BOOK_NOT_FOUND:
            stream << "ORDER_BOOK_NOT_FOUND";
            break;
        case ErrorCode::ORDER_BOOK_NOT_EMPTY:
            stream << "ORDER_BOOK_NOT_EMPTY";
            break;
        case ErrorCode::ORDER_DUPLICATE:
            stream << "ORDER_DUPLICATE";
            break;
        case ErrorCode::ORDER_NOT_FOUND:
            stream << "ORDER_NOT_FOUND";
            break;
        case ErrorCode::ORDER_ID_INVALID:
            stream << "ORDER_ID_INVALID";
            break;
        case ErrorCode::ORDER_TYPE_INVALID:
            stream << "ORDER_TYPE_INVALID";
            break;
        case ErrorCode::ORDER_PARAMETER_INVALID:
            stream << "ORDER_PARAMETER_INVALID";
            break;
        case ErrorCode::ORDER_QUANTITY_INVALID:
            stream << "ORDER_QUANTITY_INVALID";
            break;
        default:
            stream << "<unknown>";
            break;
    }

    return stream;
}

} // namespace matching
