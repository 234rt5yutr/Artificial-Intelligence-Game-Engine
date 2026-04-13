#include "Core/UI/Focus/UIFocusRouter.h"

#include "Core/UI/ImGuiSubsystem.h"
#include "Core/UI/Modal/ModalDialogService.h"
#include "Core/UI/Widgets/Widget.h"
#include "Core/UI/Widgets/WidgetSystem.h"
#include "Core/UI/World/WorldSpaceWidgetRenderer.h"

namespace Core {
namespace UI {
namespace Focus {

    UIFocusRouter& UIFocusRouter::Get() {
        static UIFocusRouter instance;
        return instance;
    }

    bool UIFocusRouter::RouteEvent(
        const SDL_Event& event,
        ImGuiSubsystem& imgui,
        Widgets::WidgetSystem& widgetSystem) {
        const auto [owner, target] = DetermineFocusOwner(imgui, widgetSystem);
        if (owner != m_Diagnostics.CurrentOwner || target != m_Diagnostics.CurrentTarget) {
            m_Diagnostics.PreviousOwner = m_Diagnostics.CurrentOwner;
            m_Diagnostics.CurrentOwner = owner;
            m_Diagnostics.CurrentTarget = target;
            ++m_Diagnostics.OwnershipTransitions;
        }

        auto routeToWidgetSystem = [&event, &widgetSystem]() -> bool {
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                widgetSystem.OnMouseMove(glm::vec2(event.motion.x, event.motion.y));
                return true;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                widgetSystem.OnMouseButton(static_cast<int>(event.button.button), pressed);
                return true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
                widgetSystem.OnKeyEvent(static_cast<int>(event.key.key), pressed);
                return true;
            }
            return false;
        };

        switch (owner) {
            case FocusOwner::Modal: {
                const bool handled = Modal::ModalDialogService::Get().ProcessEvent(event);
                if (handled) {
                    ++m_Diagnostics.ModalCaptures;
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                        ++m_Diagnostics.CancelActions;
                    }
                }
                return handled;
            }
            case FocusOwner::WorldWidget: {
                const bool handled = routeToWidgetSystem();
                if (handled) {
                    ++m_Diagnostics.WorldWidgetCaptures;
                }
                return handled;
            }
            case FocusOwner::ScreenWidget: {
                const bool handled = routeToWidgetSystem();
                if (handled) {
                    ++m_Diagnostics.ScreenWidgetCaptures;
                }
                return handled;
            }
            case FocusOwner::ImGui: {
                const bool handled = imgui.ProcessEvent(event);
                if (handled) {
                    ++m_Diagnostics.ImGuiCaptures;
                }
                return handled;
            }
            case FocusOwner::None:
            default:
                return false;
        }
    }

    void UIFocusRouter::Reset() {
        m_Diagnostics = FocusArbitrationDiagnostics{};
    }

    std::pair<FocusOwner, std::string> UIFocusRouter::DetermineFocusOwner(
        const ImGuiSubsystem& imgui,
        const Widgets::WidgetSystem& widgetSystem) const {
        const std::optional<Modal::ModalSnapshot> activeModal = Modal::ModalDialogService::Get().GetActiveModal();
        if (activeModal.has_value() && activeModal->RequireFocusLock) {
            return {FocusOwner::Modal, activeModal->ModalId};
        }

        const auto& worldDiagnostics = World::WorldSpaceWidgetRenderer::Get().GetDiagnosticsSnapshot();
        for (const auto& [widgetId, visibility] : worldDiagnostics) {
            if (visibility.OnScreen &&
                visibility.Alpha > 0.01f &&
                !visibility.InteractionTarget.empty()) {
                return {FocusOwner::WorldWidget, widgetId};
            }
        }

        if (widgetSystem.GetFocusedWidget() != nullptr) {
            return {FocusOwner::ScreenWidget, widgetSystem.GetFocusedWidget()->GetId()};
        }
        if (widgetSystem.GetWidgetUnderCursor() != nullptr &&
            widgetSystem.GetWidgetUnderCursor()->IsInteractive()) {
            return {FocusOwner::ScreenWidget, widgetSystem.GetWidgetUnderCursor()->GetId()};
        }

        if (imgui.WantsKeyboardInput() || imgui.WantMouseInput()) {
            return {FocusOwner::ImGui, "imgui"};
        }

        return {FocusOwner::None, std::string{}};
    }

} // namespace Focus
} // namespace UI
} // namespace Core

