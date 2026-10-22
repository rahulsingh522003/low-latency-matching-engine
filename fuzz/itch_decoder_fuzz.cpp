#include "providers/nasdaq/itch/decoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace itch = providers::nasdaq::itch;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
    const auto* bytes = reinterpret_cast<const std::byte*>(data);

    itch::MessageHandler whole_handler;
    itch::Decoder whole_decoder(whole_handler);
    static_cast<void>(whole_decoder.process(std::span(bytes, size)));
    static_cast<void>(whole_decoder.finish());

    itch::MessageHandler chunked_handler;
    itch::Decoder chunked_decoder(chunked_handler);
    std::size_t offset = 0;
    while ((offset < size) && !chunked_decoder.failed())
    {
        const std::size_t requested =
            1U + (static_cast<std::size_t>(data[offset]) % 64U);
        const std::size_t chunk_size = std::min(requested, size - offset);
        static_cast<void>(chunked_decoder.process(
            std::span(bytes + offset, chunk_size)));
        offset += chunk_size;
    }
    static_cast<void>(chunked_decoder.finish());
    return 0;
}
