namespace matching {

template <typename OutputStream>
inline OutputStream& operator<<(OutputStream& stream, UpdateType type)
{
    switch (type)
    {
        case UpdateType::ADD:
            stream << "ADD";
            break;
        case UpdateType::UPDATE:
            stream << "UPDATE";
            break;
        case UpdateType::DELETE:
            stream << "DELETE";
            break;
        default:
            stream << "<unknown>";
            break;
    }

    return stream;
}

} // namespace matching
