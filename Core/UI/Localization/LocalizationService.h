#pragma once

#include "Core/UI/Localization/LocalizationTable.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace Core {
namespace UI {
namespace Localization {

    constexpr const char* UI_LOCALIZATION_KEY_NOT_FOUND = "UI_LOCALIZATION_KEY_NOT_FOUND";
    constexpr const char* UI_LOCALIZATION_LOCALE_NOT_FOUND = "UI_LOCALIZATION_LOCALE_NOT_FOUND";
    constexpr const char* UI_LOCALIZATION_INVALID_TABLE = "UI_LOCALIZATION_INVALID_TABLE";

    struct LocalizationResolveResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        std::string Value;
        std::string ResolvedLocale;
        bool FallbackUsed = false;
    };

    struct LocalizationDiagnostics {
        uint32_t RegisteredLocales = 0;
        uint64_t LookupRequests = 0;
        uint64_t LookupHits = 0;
        uint64_t LookupMisses = 0;
        uint64_t FallbackHits = 0;
    };

    class LocalizationService {
    public:
        static LocalizationService& Get();

        bool RegisterLocalizationTable(const LocalizationTable& table);
        std::optional<LocalizationTable> ParseLocalizationTableFromJson(
            const nlohmann::json& payload,
            std::string* errorMessage) const;

        LocalizationResolveResult ResolveString(
            const std::string& key,
            const std::string& requestedLocale,
            const std::string& fallbackLocale) const;

        void SetDefaultLocale(const std::string& locale);
        const std::string& GetDefaultLocale() const { return m_DefaultLocale; }
        bool HasLocale(const std::string& locale) const;

        const LocalizationDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

    private:
        LocalizationService() = default;

        static std::string NormalizeLocale(std::string locale);
        static std::string ExtractBaseLocale(const std::string& locale);

    private:
        mutable LocalizationDiagnostics m_Diagnostics;
        std::string m_DefaultLocale = "en-us";
        std::unordered_map<std::string, LocalizationTable> m_Tables;
    };

} // namespace Localization
} // namespace UI
} // namespace Core

