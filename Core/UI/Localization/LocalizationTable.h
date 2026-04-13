#pragma once

#include <string>
#include <unordered_map>

namespace Core {
namespace UI {
namespace Localization {

    struct LocalizationTable {
        std::string Locale;
        std::unordered_map<std::string, std::string> Entries;
    };

} // namespace Localization
} // namespace UI
} // namespace Core

