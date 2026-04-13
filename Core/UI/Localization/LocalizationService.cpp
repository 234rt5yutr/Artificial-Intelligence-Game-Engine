#include "Core/UI/Localization/LocalizationService.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Core {
namespace UI {
namespace Localization {

    LocalizationService& LocalizationService::Get() {
        static LocalizationService instance;
        return instance;
    }

    bool LocalizationService::RegisterLocalizationTable(const LocalizationTable& table) {
        const std::string normalizedLocale = NormalizeLocale(table.Locale);
        if (normalizedLocale.empty() || table.Entries.empty()) {
            return false;
        }

        LocalizationTable normalizedTable = table;
        normalizedTable.Locale = normalizedLocale;
        m_Tables[normalizedLocale] = std::move(normalizedTable);
        m_Diagnostics.RegisteredLocales = static_cast<uint32_t>(m_Tables.size());
        return true;
    }

    std::optional<LocalizationTable> LocalizationService::ParseLocalizationTableFromJson(
        const nlohmann::json& payload,
        std::string* errorMessage) const {
        if (!payload.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Localization payload must be an object.";
            }
            return std::nullopt;
        }

        const std::string locale = payload.value("locale", std::string{});
        if (locale.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Localization payload is missing locale.";
            }
            return std::nullopt;
        }

        const nlohmann::json entries = payload.value("entries", nlohmann::json::object());
        if (!entries.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Localization payload entries must be an object.";
            }
            return std::nullopt;
        }

        LocalizationTable table;
        table.Locale = NormalizeLocale(locale);
        for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
            if (!entryIt.value().is_string()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Localization entry values must be strings.";
                }
                return std::nullopt;
            }
            table.Entries[entryIt.key()] = entryIt.value().get<std::string>();
        }

        if (table.Locale.empty() || table.Entries.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Localization table is empty.";
            }
            return std::nullopt;
        }
        return table;
    }

    LocalizationResolveResult LocalizationService::ResolveString(
        const std::string& key,
        const std::string& requestedLocale,
        const std::string& fallbackLocale) const {
        LocalizationResolveResult result;
        ++m_Diagnostics.LookupRequests;

        if (key.empty()) {
            ++m_Diagnostics.LookupMisses;
            result.Success = false;
            result.ErrorCode = UI_LOCALIZATION_KEY_NOT_FOUND;
            result.Message = "Localization key cannot be empty.";
            return result;
        }

        const std::string normalizedRequested = NormalizeLocale(requestedLocale);
        const std::string normalizedRequestedBase = ExtractBaseLocale(normalizedRequested);
        const std::string normalizedFallback = NormalizeLocale(fallbackLocale);
        const std::string normalizedDefault = NormalizeLocale(m_DefaultLocale);

        std::vector<std::string> lookupOrder;
        lookupOrder.reserve(4);
        if (!normalizedRequested.empty()) {
            lookupOrder.push_back(normalizedRequested);
        }
        if (!normalizedRequestedBase.empty() && normalizedRequestedBase != normalizedRequested) {
            lookupOrder.push_back(normalizedRequestedBase);
        }
        if (!normalizedFallback.empty()) {
            lookupOrder.push_back(normalizedFallback);
        }
        if (!normalizedDefault.empty()) {
            lookupOrder.push_back(normalizedDefault);
        }

        std::unordered_set<std::string> seenLocales;
        for (const std::string& locale : lookupOrder) {
            if (locale.empty() || !seenLocales.insert(locale).second) {
                continue;
            }

            const auto tableIt = m_Tables.find(locale);
            if (tableIt == m_Tables.end()) {
                continue;
            }

            const auto entryIt = tableIt->second.Entries.find(key);
            if (entryIt == tableIt->second.Entries.end()) {
                continue;
            }

            result.Success = true;
            result.Value = entryIt->second;
            result.ResolvedLocale = locale;
            result.FallbackUsed = locale != normalizedRequested;
            if (result.FallbackUsed) {
                ++m_Diagnostics.FallbackHits;
            }
            ++m_Diagnostics.LookupHits;
            return result;
        }

        ++m_Diagnostics.LookupMisses;
        result.Success = false;
        result.ErrorCode = UI_LOCALIZATION_KEY_NOT_FOUND;
        result.Message = "Localization key was not found in requested fallback chain.";
        return result;
    }

    void LocalizationService::SetDefaultLocale(const std::string& locale) {
        const std::string normalized = NormalizeLocale(locale);
        if (!normalized.empty()) {
            m_DefaultLocale = normalized;
        }
    }

    bool LocalizationService::HasLocale(const std::string& locale) const {
        const std::string normalized = NormalizeLocale(locale);
        return !normalized.empty() && m_Tables.find(normalized) != m_Tables.end();
    }

    std::string LocalizationService::NormalizeLocale(std::string locale) {
        std::transform(
            locale.begin(),
            locale.end(),
            locale.begin(),
            [](unsigned char c) -> char {
                if (c == '_') {
                    return '-';
                }
                return static_cast<char>(std::tolower(c));
            });
        return locale;
    }

    std::string LocalizationService::ExtractBaseLocale(const std::string& locale) {
        const size_t separator = locale.find('-');
        if (separator == std::string::npos) {
            return locale;
        }
        return locale.substr(0, separator);
    }

} // namespace Localization
} // namespace UI
} // namespace Core

