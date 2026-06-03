#include "logger.h"

#include <godot_cpp/variant/utility_functions.hpp>

namespace godot_rt {
namespace {

    godot::String with_prefix(const godot::String& message) {
        return godot::String("[gdextension_cpp_example] ") + message;
    }

}

    void Logger::log(LogLevel level, const godot::String& message) {
        const godot::String output = with_prefix(message);

        switch (level) {
            case LogLevel::Info:
                godot::UtilityFunctions::print(output);
                break;
            case LogLevel::Warning:
                godot::UtilityFunctions::push_warning(output);
                break;
            case LogLevel::Error:
                godot::UtilityFunctions::push_error(output);
                break;
        }
    }

    void Logger::info(const godot::String& message) {
        log(LogLevel::Info, message);
    }

    void Logger::warning(const godot::String& message) {
        log(LogLevel::Warning, message);
    }

    void Logger::error(const godot::String& message) {
        log(LogLevel::Error, message);
    }

}
