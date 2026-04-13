#include "Core/UI/Binding/UIBindingService.h"

#include "Core/UI/Widgets/WidgetSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <type_traits>
#include <unordered_set>

namespace Core {
namespace UI {
namespace Binding {
namespace {

    constexpr float kFloatEpsilon = 0.0001f;

    std::vector<std::string> SplitPath(const std::string& path) {
        std::vector<std::string> parts;
        std::string current;
        for (const char c : path) {
            if (c == '.') {
                parts.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        parts.push_back(current);
        return parts;
    }

    void RemoveHandleFromIndex(std::vector<uint64_t>& handles, uint64_t handle) {
        handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
    }

} // namespace

    UIBindingService& UIBindingService::Get() {
        static UIBindingService instance;
        return instance;
    }

    BindingRegistrationResult UIBindingService::BindWidgetPropertyToData(const BindingRegistrationRequest& request) {
        BindingRegistrationResult result;
        result.Success = false;

        if (request.WidgetId.empty() || Widgets::WidgetSystem::Get().FindWidget(request.WidgetId) == nullptr) {
            result.ErrorCode = UI_BINDING_INVALID_WIDGET;
            result.Message = "Widget is not registered in WidgetSystem.";
            return result;
        }

        if (!IsValidPath(request.WidgetPropertyPath)) {
            result.ErrorCode = UI_BINDING_INVALID_PROPERTY_PATH;
            result.Message = "Widget property path is invalid.";
            return result;
        }

        if (!IsValidPath(request.DataPath)) {
            result.ErrorCode = UI_BINDING_INVALID_DATA_PATH;
            result.Message = "Data path is invalid.";
            return result;
        }

        if (!request.ConverterName.empty() && m_Converters.find(request.ConverterName) == m_Converters.end()) {
            result.ErrorCode = UI_BINDING_CONVERTER_NOT_FOUND;
            result.Message = "Named converter was not registered.";
            return result;
        }

        if (!request.ValidatorName.empty() && m_Validators.find(request.ValidatorName) == m_Validators.end()) {
            result.ErrorCode = UI_BINDING_VALIDATOR_NOT_FOUND;
            result.Message = "Named validator was not registered.";
            return result;
        }

        if (request.Mode == BindingMode::TwoWay && WouldIntroduceTwoWayCycle(request)) {
            ++m_Diagnostics.CycleRejections;
            result.ErrorCode = UI_BINDING_CYCLE_REJECTED;
            result.Message = "Two-way binding rejected due to cycle risk.";
            return result;
        }

        BindingRecord record;
        record.Handle = m_NextBindingHandle++;
        record.Request = request;
        record.Destroyed = false;
        record.LastWidgetValue = Widgets::WidgetSystem::Get().GetWidgetProperty(
            request.WidgetId,
            request.WidgetPropertyPath);

        const nlohmann::json* resolvedData = nullptr;
        if (ResolveDataPath(request.DataPath, resolvedData) && resolvedData != nullptr) {
            record.LastDataValue = ConvertJsonToPropertyValue(*resolvedData);
        }

        m_Bindings[record.Handle] = record;
        m_BindingsByWidget[request.WidgetId].push_back(record.Handle);
        m_BindingsByDataPath[request.DataPath].push_back(record.Handle);
        m_DirtyQueue.push_back(record.Handle);

        m_Diagnostics.ActiveBindings = static_cast<uint32_t>(m_Bindings.size());

        result.Success = true;
        result.BindingHandle = record.Handle;
        return result;
    }

    bool UIBindingService::UnbindBinding(uint64_t bindingHandle) {
        auto bindingIt = m_Bindings.find(bindingHandle);
        if (bindingIt == m_Bindings.end()) {
            return false;
        }
        bindingIt->second.Destroyed = true;
        RemoveDestroyedBindings();
        return true;
    }

    uint32_t UIBindingService::UnbindWidget(const std::string& widgetId) {
        auto handlesIt = m_BindingsByWidget.find(widgetId);
        if (handlesIt == m_BindingsByWidget.end()) {
            return 0;
        }

        uint32_t removed = 0;
        for (const uint64_t handle : handlesIt->second) {
            auto bindingIt = m_Bindings.find(handle);
            if (bindingIt != m_Bindings.end() && !bindingIt->second.Destroyed) {
                bindingIt->second.Destroyed = true;
                ++removed;
            }
        }
        RemoveDestroyedBindings();
        return removed;
    }

    void UIBindingService::ClearBindings() {
        m_Bindings.clear();
        m_BindingsByWidget.clear();
        m_BindingsByDataPath.clear();
        m_DirtyQueue.clear();
        m_TwoWaySuppressionFrames.clear();
        m_Diagnostics.ActiveBindings = 0;
    }

    void UIBindingService::RegisterConverter(const std::string& converterName, BindingConverter converter) {
        if (converterName.empty() || !converter) {
            return;
        }
        m_Converters[converterName] = std::move(converter);
    }

    void UIBindingService::RegisterValidator(const std::string& validatorName, BindingValidator validator) {
        if (validatorName.empty() || !validator) {
            return;
        }
        m_Validators[validatorName] = std::move(validator);
    }

    bool UIBindingService::SetDataValue(const std::string& dataPath, const nlohmann::json& value) {
        if (!IsValidPath(dataPath)) {
            return false;
        }

        nlohmann::json* target = nullptr;
        if (!ResolveDataPath(dataPath, target) || target == nullptr) {
            return false;
        }

        *target = value;
        MarkBindingsDirtyForDataPath(dataPath);
        return true;
    }

    std::optional<nlohmann::json> UIBindingService::GetDataValue(const std::string& dataPath) const {
        if (!IsValidPath(dataPath)) {
            return std::nullopt;
        }

        const nlohmann::json* resolved = nullptr;
        if (!ResolveDataPath(dataPath, resolved) || resolved == nullptr) {
            return std::nullopt;
        }
        return std::make_optional<nlohmann::json>(*resolved);
    }

    std::vector<UIBindingState> UIBindingService::GetBindingStates() const {
        std::vector<UIBindingState> states;
        states.reserve(m_Bindings.size());
        for (const auto& [handle, binding] : m_Bindings) {
            UIBindingState state;
            state.Handle = handle;
            state.WidgetId = binding.Request.WidgetId;
            state.WidgetPropertyPath = binding.Request.WidgetPropertyPath;
            state.DataPath = binding.Request.DataPath;
            state.Mode = binding.Request.Mode;
            state.Destroyed = binding.Destroyed;
            states.push_back(std::move(state));
        }

        std::sort(
            states.begin(),
            states.end(),
            [](const UIBindingState& lhs, const UIBindingState& rhs) {
                return lhs.Handle < rhs.Handle;
            });
        return states;
    }

    bool UIBindingService::HasBindingForProperty(
        const std::string& widgetId,
        std::string_view propertyPath) const {
        const std::string canonicalRequestedPath = CanonicalizePropertyPath(propertyPath);
        for (const auto& [handle, binding] : m_Bindings) {
            (void)handle;
            if (binding.Destroyed || binding.Request.WidgetId != widgetId) {
                continue;
            }

            const std::string canonicalBindingPath =
                CanonicalizePropertyPath(binding.Request.WidgetPropertyPath);
            if (canonicalBindingPath == canonicalRequestedPath) {
                return true;
            }
        }
        return false;
    }

    void UIBindingService::NotifyTransitionMutation(
        const std::string& widgetId,
        std::string_view propertyPath) {
        if (widgetId.empty()) {
            return;
        }

        const std::string key = MakeWidgetPropertyKey(widgetId, propertyPath);
        m_TwoWaySuppressionFrames[key] = std::max<uint32_t>(m_TwoWaySuppressionFrames[key], 1u);
    }

    void UIBindingService::UpdateBindings(uint32_t maxIterations) {
        if (maxIterations == 0) {
            return;
        }

        for (const auto& [handle, binding] : m_Bindings) {
            if (!binding.Destroyed) {
                m_DirtyQueue.push_back(handle);
            }
        }

        uint32_t iterations = 0;
        while (!m_DirtyQueue.empty() && iterations < maxIterations) {
            const uint64_t handle = m_DirtyQueue.front();
            m_DirtyQueue.pop_front();

            auto bindingIt = m_Bindings.find(handle);
            if (bindingIt == m_Bindings.end() || bindingIt->second.Destroyed) {
                continue;
            }

            BindingRecord& binding = bindingIt->second;
            Widget* widget = Widgets::WidgetSystem::Get().FindWidget(binding.Request.WidgetId);
            if (widget == nullptr) {
                binding.Destroyed = true;
                continue;
            }

            const nlohmann::json* resolvedData = nullptr;
            if (!ResolveDataPath(binding.Request.DataPath, resolvedData) || resolvedData == nullptr) {
                continue;
            }

            std::optional<WidgetPropertyValue> propertyValue = ConvertJsonToPropertyValue(*resolvedData);
            if (!propertyValue.has_value()) {
                ++m_Diagnostics.ValidationFailures;
                continue;
            }

            if (!binding.Request.ConverterName.empty()) {
                const BindingConverter& converter = m_Converters[binding.Request.ConverterName];
                propertyValue = converter(propertyValue.value());
                if (!propertyValue.has_value()) {
                    ++m_Diagnostics.ValidationFailures;
                    continue;
                }
            }

            if (!binding.Request.ValidatorName.empty()) {
                const BindingValidator& validator = m_Validators[binding.Request.ValidatorName];
                if (!validator(propertyValue.value())) {
                    ++m_Diagnostics.ValidationFailures;
                    continue;
                }
            }

            if (binding.LastDataValue.has_value() &&
                PropertyValuesEqual(binding.LastDataValue.value(), propertyValue.value())) {
                ++iterations;
                continue;
            }

            if (!Widgets::WidgetSystem::Get().SetWidgetProperty(
                    binding.Request.WidgetId,
                    binding.Request.WidgetPropertyPath,
                    propertyValue.value())) {
                continue;
            }

            binding.LastDataValue = propertyValue;
            binding.LastWidgetValue = Widgets::WidgetSystem::Get().GetWidgetProperty(
                binding.Request.WidgetId,
                binding.Request.WidgetPropertyPath);

            if (binding.Request.Mode == BindingMode::TwoWay && binding.LastWidgetValue.has_value()) {
                const std::string suppressionKey = MakeWidgetPropertyKey(
                    binding.Request.WidgetId,
                    binding.Request.WidgetPropertyPath);
                auto suppressionIt = m_TwoWaySuppressionFrames.find(suppressionKey);
                if (suppressionIt != m_TwoWaySuppressionFrames.end() && suppressionIt->second > 0) {
                    ++m_Diagnostics.TransitionSuppressions;
                    ++iterations;
                    continue;
                }

                std::optional<nlohmann::json> jsonValue =
                    ConvertPropertyValueToJson(binding.LastWidgetValue.value());
                if (jsonValue.has_value()) {
                    nlohmann::json* mutableData = nullptr;
                    if (ResolveDataPath(binding.Request.DataPath, mutableData) &&
                        mutableData != nullptr &&
                        *mutableData != jsonValue.value()) {
                        *mutableData = jsonValue.value();
                        MarkBindingsDirtyForDataPath(binding.Request.DataPath);
                    }
                }
            }

            ++iterations;
        }

        m_Diagnostics.UpdateIterations += iterations;
        AdvanceTransitionSuppressions();
        RemoveDestroyedBindings();
    }

    bool UIBindingService::IsValidPath(const std::string& path) const {
        if (path.empty() || path.front() == '.' || path.back() == '.') {
            return false;
        }

        const std::vector<std::string> parts = SplitPath(path);
        for (const std::string& part : parts) {
            if (part.empty()) {
                return false;
            }
            for (const char c : part) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                    return false;
                }
            }
        }
        return true;
    }

    bool UIBindingService::WouldIntroduceTwoWayCycle(const BindingRegistrationRequest& request) const {
        const std::string requestWidgetNode = "widget:" + request.WidgetId + ":" + request.WidgetPropertyPath;
        const std::string requestDataNode = "data:" + request.DataPath;

        std::unordered_map<std::string, std::vector<std::string>> graph;
        auto addUndirectedEdge = [&graph](const std::string& lhs, const std::string& rhs) {
            graph[lhs].push_back(rhs);
            graph[rhs].push_back(lhs);
        };

        for (const auto& [handle, binding] : m_Bindings) {
            (void)handle;
            if (binding.Destroyed || binding.Request.Mode != BindingMode::TwoWay) {
                continue;
            }

            const std::string widgetNode =
                "widget:" + binding.Request.WidgetId + ":" + binding.Request.WidgetPropertyPath;
            const std::string dataNode = "data:" + binding.Request.DataPath;
            addUndirectedEdge(widgetNode, dataNode);
        }

        std::unordered_set<std::string> visited;
        std::deque<std::string> queue;
        queue.push_back(requestWidgetNode);
        visited.insert(requestWidgetNode);

        while (!queue.empty()) {
            const std::string current = queue.front();
            queue.pop_front();
            if (current == requestDataNode) {
                return true;
            }

            const auto neighborsIt = graph.find(current);
            if (neighborsIt == graph.end()) {
                continue;
            }

            for (const std::string& neighbor : neighborsIt->second) {
                if (visited.insert(neighbor).second) {
                    queue.push_back(neighbor);
                }
            }
        }

        return false;
    }

    bool UIBindingService::ResolveDataPath(const std::string& dataPath, nlohmann::json*& outValue) {
        std::vector<std::string> parts = SplitPath(dataPath);
        nlohmann::json* current = &m_DataStore;

        for (size_t index = 0; index < parts.size(); ++index) {
            const std::string& token = parts[index];
            if (index == parts.size() - 1) {
                outValue = &((*current)[token]);
                return true;
            }

            nlohmann::json& next = (*current)[token];
            if (!next.is_object()) {
                next = nlohmann::json::object();
            }
            current = &next;
        }

        return false;
    }

    bool UIBindingService::ResolveDataPath(const std::string& dataPath, const nlohmann::json*& outValue) const {
        std::vector<std::string> parts = SplitPath(dataPath);
        const nlohmann::json* current = &m_DataStore;

        for (size_t index = 0; index < parts.size(); ++index) {
            const std::string& token = parts[index];
            if (!current->is_object()) {
                return false;
            }
            auto valueIt = current->find(token);
            if (valueIt == current->end()) {
                return false;
            }

            if (index == parts.size() - 1) {
                outValue = &(*valueIt);
                return true;
            }
            current = &(*valueIt);
        }

        return false;
    }

    void UIBindingService::MarkBindingsDirtyForDataPath(const std::string& dataPath) {
        auto handlesIt = m_BindingsByDataPath.find(dataPath);
        if (handlesIt == m_BindingsByDataPath.end()) {
            return;
        }

        for (const uint64_t handle : handlesIt->second) {
            m_DirtyQueue.push_back(handle);
        }
    }

    void UIBindingService::AdvanceTransitionSuppressions() {
        std::vector<std::string> keysToErase;
        keysToErase.reserve(m_TwoWaySuppressionFrames.size());
        for (auto& [key, frames] : m_TwoWaySuppressionFrames) {
            if (frames == 0) {
                keysToErase.push_back(key);
                continue;
            }

            --frames;
            if (frames == 0) {
                keysToErase.push_back(key);
            }
        }

        for (const std::string& key : keysToErase) {
            m_TwoWaySuppressionFrames.erase(key);
        }
    }

    std::string UIBindingService::CanonicalizePropertyPath(std::string_view propertyPath) {
        if (propertyPath == "offset") {
            return "position";
        }
        if (propertyPath == "position.x" || propertyPath == "position.y") {
            return "position";
        }
        if (propertyPath == "scale.x" || propertyPath == "scale.y") {
            return "scale";
        }
        if (propertyPath == "color.r" || propertyPath == "color.g" ||
            propertyPath == "color.b" || propertyPath == "color.a") {
            return "color";
        }
        return std::string(propertyPath);
    }

    std::string UIBindingService::MakeWidgetPropertyKey(
        const std::string& widgetId,
        std::string_view propertyPath) {
        return widgetId + "|" + CanonicalizePropertyPath(propertyPath);
    }

    void UIBindingService::RemoveDestroyedBindings() {
        std::vector<uint64_t> handlesToRemove;
        for (const auto& [handle, binding] : m_Bindings) {
            if (binding.Destroyed) {
                handlesToRemove.push_back(handle);
            }
        }

        for (const uint64_t handle : handlesToRemove) {
            auto bindingIt = m_Bindings.find(handle);
            if (bindingIt == m_Bindings.end()) {
                continue;
            }

            auto widgetIndexIt = m_BindingsByWidget.find(bindingIt->second.Request.WidgetId);
            if (widgetIndexIt != m_BindingsByWidget.end()) {
                RemoveHandleFromIndex(widgetIndexIt->second, handle);
                if (widgetIndexIt->second.empty()) {
                    m_BindingsByWidget.erase(widgetIndexIt);
                }
            }

            auto dataIndexIt = m_BindingsByDataPath.find(bindingIt->second.Request.DataPath);
            if (dataIndexIt != m_BindingsByDataPath.end()) {
                RemoveHandleFromIndex(dataIndexIt->second, handle);
                if (dataIndexIt->second.empty()) {
                    m_BindingsByDataPath.erase(dataIndexIt);
                }
            }

            m_Bindings.erase(bindingIt);
            ++m_Diagnostics.AutoCleanupRemovals;
        }

        m_Diagnostics.ActiveBindings = static_cast<uint32_t>(m_Bindings.size());
    }

    std::optional<WidgetPropertyValue> UIBindingService::ConvertJsonToPropertyValue(
        const nlohmann::json& value) {
        using PropertyValue = WidgetPropertyValue;
        if (value.is_boolean()) {
            return PropertyValue(value.get<bool>());
        }
        if (value.is_number_integer()) {
            return PropertyValue(static_cast<int32_t>(value.get<int64_t>()));
        }
        if (value.is_number()) {
            return PropertyValue(value.get<float>());
        }
        if (value.is_string()) {
            return PropertyValue(value.get<std::string>());
        }
        if (value.is_object() && value.contains("x") && value.contains("y")) {
            return PropertyValue(glm::vec2(value.value("x", 0.0f), value.value("y", 0.0f)));
        }
        if (value.is_object() && value.contains("r") && value.contains("g") &&
            value.contains("b") && value.contains("a")) {
            return PropertyValue(glm::vec4(
                value.value("r", 0.0f),
                value.value("g", 0.0f),
                value.value("b", 0.0f),
                value.value("a", 1.0f)));
        }
        return std::nullopt;
    }

    std::optional<nlohmann::json> UIBindingService::ConvertPropertyValueToJson(
        const WidgetPropertyValue& value) {
        return std::visit(
            [](const auto& currentValue) -> std::optional<nlohmann::json> {
                using ValueType = std::decay_t<decltype(currentValue)>;
                if constexpr (std::is_same_v<ValueType, std::monostate>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<ValueType, glm::vec2>) {
                    return nlohmann::json{{"x", currentValue.x}, {"y", currentValue.y}};
                } else if constexpr (std::is_same_v<ValueType, glm::vec4>) {
                    return nlohmann::json{
                        {"r", currentValue.r},
                        {"g", currentValue.g},
                        {"b", currentValue.b},
                        {"a", currentValue.a}};
                } else {
                    return nlohmann::json(currentValue);
                }
            },
            value);
    }

    bool UIBindingService::PropertyValuesEqual(
        const WidgetPropertyValue& lhs,
        const WidgetPropertyValue& rhs) {
        if (lhs.index() != rhs.index()) {
            return false;
        }

        return std::visit(
            [](const auto& left, const auto& right) -> bool {
                using ValueType = std::decay_t<decltype(left)>;
                if constexpr (!std::is_same_v<ValueType, std::decay_t<decltype(right)>>) {
                    return false;
                } else if constexpr (std::is_same_v<ValueType, std::monostate>) {
                    return true;
                } else if constexpr (std::is_same_v<ValueType, float>) {
                    return std::fabs(left - right) <= kFloatEpsilon;
                } else if constexpr (std::is_same_v<ValueType, glm::vec2>) {
                    return std::fabs(left.x - right.x) <= kFloatEpsilon &&
                           std::fabs(left.y - right.y) <= kFloatEpsilon;
                } else if constexpr (std::is_same_v<ValueType, glm::vec4>) {
                    return std::fabs(left.r - right.r) <= kFloatEpsilon &&
                           std::fabs(left.g - right.g) <= kFloatEpsilon &&
                           std::fabs(left.b - right.b) <= kFloatEpsilon &&
                           std::fabs(left.a - right.a) <= kFloatEpsilon;
                } else {
                    return left == right;
                }
            },
            lhs,
            rhs);
    }

} // namespace Binding
} // namespace UI
} // namespace Core

