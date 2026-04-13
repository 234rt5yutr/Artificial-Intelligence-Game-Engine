#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <utility>

namespace Core {
namespace UI {

    class ImGuiSubsystem;
    namespace Widgets {
        class WidgetSystem;
    }

namespace Focus {

    enum class FocusOwner : uint8_t {
        None = 0,
        Modal,
        WorldWidget,
        ScreenWidget,
        ImGui
    };

    struct FocusArbitrationDiagnostics {
        FocusOwner CurrentOwner = FocusOwner::None;
        FocusOwner PreviousOwner = FocusOwner::None;
        std::string CurrentTarget;
        uint64_t OwnershipTransitions = 0;
        uint64_t ModalCaptures = 0;
        uint64_t WorldWidgetCaptures = 0;
        uint64_t ScreenWidgetCaptures = 0;
        uint64_t ImGuiCaptures = 0;
        uint64_t CancelActions = 0;
    };

    class UIFocusRouter {
    public:
        static UIFocusRouter& Get();

        bool RouteEvent(const SDL_Event& event, ImGuiSubsystem& imgui, Widgets::WidgetSystem& widgetSystem);
        const FocusArbitrationDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
        void Reset();

    private:
        UIFocusRouter() = default;

        std::pair<FocusOwner, std::string> DetermineFocusOwner(
            const ImGuiSubsystem& imgui,
            const Widgets::WidgetSystem& widgetSystem) const;

    private:
        FocusArbitrationDiagnostics m_Diagnostics;
    };

    inline const char* FocusOwnerToString(FocusOwner owner) {
        switch (owner) {
            case FocusOwner::Modal:
                return "modal";
            case FocusOwner::WorldWidget:
                return "world_widget";
            case FocusOwner::ScreenWidget:
                return "screen_widget";
            case FocusOwner::ImGui:
                return "imgui";
            case FocusOwner::None:
            default:
                return "none";
        }
    }

} // namespace Focus
} // namespace UI
} // namespace Core

