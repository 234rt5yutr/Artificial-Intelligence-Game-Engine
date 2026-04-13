#pragma once

#include "Core/UI/Binding/UIBindingTypes.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core {
namespace UI {
namespace Binding {

    class UIBindingService {
    public:
        static UIBindingService& Get();

        BindingRegistrationResult BindWidgetPropertyToData(const BindingRegistrationRequest& request);
        bool UnbindBinding(uint64_t bindingHandle);
        uint32_t UnbindWidget(const std::string& widgetId);
        void ClearBindings();

        void RegisterConverter(const std::string& converterName, BindingConverter converter);
        void RegisterValidator(const std::string& validatorName, BindingValidator validator);

        bool SetDataValue(const std::string& dataPath, const nlohmann::json& value);
        std::optional<nlohmann::json> GetDataValue(const std::string& dataPath) const;

        bool HasBindingForProperty(const std::string& widgetId, std::string_view propertyPath) const;
        void NotifyTransitionMutation(const std::string& widgetId, std::string_view propertyPath);

        void UpdateBindings(uint32_t maxIterations = 256);
        const UIBindingDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

    private:
        struct BindingRecord {
            uint64_t Handle = 0;
            BindingRegistrationRequest Request;
            bool Destroyed = false;
            std::optional<WidgetPropertyValue> LastWidgetValue;
            std::optional<WidgetPropertyValue> LastDataValue;
        };

        UIBindingService() = default;

        bool IsValidPath(const std::string& path) const;
        bool WouldIntroduceTwoWayCycle(const BindingRegistrationRequest& request) const;
        bool ResolveDataPath(const std::string& dataPath, nlohmann::json*& outValue);
        bool ResolveDataPath(const std::string& dataPath, const nlohmann::json*& outValue) const;
        void MarkBindingsDirtyForDataPath(const std::string& dataPath);
        void AdvanceTransitionSuppressions();
        void RemoveDestroyedBindings();
        static std::string CanonicalizePropertyPath(std::string_view propertyPath);
        static std::string MakeWidgetPropertyKey(
            const std::string& widgetId,
            std::string_view propertyPath);

        static std::optional<WidgetPropertyValue> ConvertJsonToPropertyValue(const nlohmann::json& value);
        static std::optional<nlohmann::json> ConvertPropertyValueToJson(const WidgetPropertyValue& value);
        static bool PropertyValuesEqual(
            const WidgetPropertyValue& lhs,
            const WidgetPropertyValue& rhs);

    private:
        uint64_t m_NextBindingHandle = 1;
        std::unordered_map<uint64_t, BindingRecord> m_Bindings;
        std::unordered_map<std::string, std::vector<uint64_t>> m_BindingsByWidget;
        std::unordered_map<std::string, std::vector<uint64_t>> m_BindingsByDataPath;
        std::unordered_map<std::string, BindingConverter> m_Converters;
        std::unordered_map<std::string, BindingValidator> m_Validators;
        std::deque<uint64_t> m_DirtyQueue;
        std::unordered_map<std::string, uint32_t> m_TwoWaySuppressionFrames;
        nlohmann::json m_DataStore = nlohmann::json::object();
        UIBindingDiagnostics m_Diagnostics;
    };

} // namespace Binding
} // namespace UI
} // namespace Core

