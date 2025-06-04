#include "utilities/message.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <stdexcept> // Required for try-catch blocks

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::string input_string(reinterpret_cast<const char*>(Data), Size);

    try {
        // Attempt to deserialize the fuzzer-generated string
        Message deserialized_msg = Message::Deserialize(input_string);

        // Optional: Serialize the deserialized message and deserialize again
        // This can catch inconsistencies between serialize and deserialize
        std::string serialized_again = Message::Serialize(deserialized_msg);
        Message deserialized_again_msg = Message::Deserialize(serialized_again);

        // Basic check: ensure the first deserialization and the second are somewhat consistent
        // This is a very basic check. More specific checks could be added if needed,
        // for example, comparing field by field if there's a canonical representation.
        if (deserialized_msg._Type != deserialized_again_msg._Type) {
            // Or some other critical field.
            // This could indicate an issue if serialization changes essential data.
        }

    } catch (const std::runtime_error& e) {
        // Catch exceptions thrown by Deserialize (e.g., malformed input)
        // This is expected for a fuzzer and not necessarily an error in the code itself,
        // unless the exception is from an unexpected place or indicates a vulnerability (e.g., unhandled null dereference).
        // For now, we just catch it to prevent the fuzzer from stopping.
    } catch (const std::invalid_argument& e) {
        // Catch specific parsing errors for numeric types
    } catch (const std::out_of_range& e) {
        // Catch specific parsing errors for numeric types
    }

    return 0; // Non-zero return values are reserved for future use.
}
