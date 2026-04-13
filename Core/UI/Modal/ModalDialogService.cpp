#include "Core/UI/Modal/ModalDialogService.h"

#include "Core/UI/Widgets/WidgetSystem.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Core {
namespace UI {
namespace Modal {

    ModalDialogService& ModalDialogService::Get() {
        static ModalDialogService instance;
        return instance;
    }

    ShowLocalizedModalDialogResult ModalDialogService::ShowLocalizedModalDialog(
        const ShowLocalizedModalDialogRequest& request) {
        ShowLocalizedModalDialogResult result;
        result.Success = false;
        result.State = ModalState::Queued;

        if (request.DialogId.empty() || request.TitleKey.empty() || request.BodyKey.empty() ||
            request.ActionKeys.empty()) {
            result.ErrorCode = UI_MODAL_INVALID_REQUEST;
            result.Message = "DialogId, title/body keys, and at least one action key are required.";
            return result;
        }

        const Localization::LocalizationResolveResult resolvedTitle =
            Localization::LocalizationService::Get().ResolveString(
                request.TitleKey, request.Locale, request.FallbackLocale);
        const Localization::LocalizationResolveResult resolvedBody =
            Localization::LocalizationService::Get().ResolveString(
                request.BodyKey, request.Locale, request.FallbackLocale);
        if (!resolvedTitle.Success || !resolvedBody.Success) {
            result.ErrorCode = UI_MODAL_LOCALIZATION_FAILURE;
            result.Message = "Failed to resolve localized title/body content.";
            return result;
        }

        ModalSession session;
        session.Snapshot.ModalId = BuildModalId(m_NextModalOrdinal++);
        session.Snapshot.DialogId = request.DialogId;
        session.Snapshot.TitleText = resolvedTitle.Value;
        session.Snapshot.BodyText = resolvedBody.Value;
        session.Snapshot.ResolvedLocale =
            resolvedTitle.ResolvedLocale.empty() ? resolvedBody.ResolvedLocale : resolvedTitle.ResolvedLocale;
        session.Snapshot.FallbackUsed = resolvedTitle.FallbackUsed || resolvedBody.FallbackUsed;
        session.Snapshot.TimeoutSeconds = std::max(0.0f, request.TimeoutSeconds);
        session.Snapshot.ElapsedSeconds = 0.0f;
        session.Snapshot.RequireFocusLock = request.RequireFocusLock;
        session.Snapshot.ActionKeys = request.ActionKeys;
        session.ActionCallback = request.ActionCallback;

        session.Snapshot.ActionLabels.reserve(request.ActionKeys.size());
        for (const std::string& actionKey : request.ActionKeys) {
            const Localization::LocalizationResolveResult actionText =
                Localization::LocalizationService::Get().ResolveString(
                    actionKey, request.Locale, request.FallbackLocale);
            if (!actionText.Success) {
                result.ErrorCode = UI_MODAL_LOCALIZATION_FAILURE;
                result.Message = "Failed to resolve localized modal action labels.";
                return result;
            }
            session.Snapshot.ActionLabels.push_back(actionText.Value);
            session.Snapshot.FallbackUsed = session.Snapshot.FallbackUsed || actionText.FallbackUsed;
        }

        if (request.RequireFocusLock && m_FocusLockActive) {
            if (!request.QueueIfBusy) {
                result.ErrorCode = UI_MODAL_FOCUS_LOCK_CONFLICT;
                result.Message = "A focus-locked modal is already active.";
                return result;
            }

            session.Snapshot.State = ModalState::Queued;
            result.Success = true;
            result.ModalId = session.Snapshot.ModalId;
            result.ResolvedLocale = session.Snapshot.ResolvedLocale;
            result.FallbackUsed = session.Snapshot.FallbackUsed;
            result.State = ModalState::Queued;
            m_QueuedModals.push_back(std::move(session));
            return result;
        }

        ActivateSession(std::move(session));
        result.Success = true;
        result.ModalId = m_ActiveModal->Snapshot.ModalId;
        result.ResolvedLocale = m_ActiveModal->Snapshot.ResolvedLocale;
        result.FallbackUsed = m_ActiveModal->Snapshot.FallbackUsed;
        result.State = ModalState::Active;
        return result;
    }

    bool ModalDialogService::TriggerAction(const std::string& modalId, uint32_t actionIndex) {
        if (!m_ActiveModal.has_value() || m_ActiveModal->Snapshot.ModalId != modalId) {
            return false;
        }
        if (actionIndex >= m_ActiveModal->Snapshot.ActionKeys.size()) {
            return false;
        }

        const std::string actionKey = m_ActiveModal->Snapshot.ActionKeys[actionIndex];
        if (m_ActiveModal->ActionCallback) {
            m_ActiveModal->ActionCallback(modalId, actionKey);
        }

        return DismissModal(modalId);
    }

    bool ModalDialogService::DismissModal(const std::string& modalId) {
        if (m_ActiveModal.has_value() && m_ActiveModal->Snapshot.ModalId == modalId) {
            const bool requiredFocusLock = m_ActiveModal->Snapshot.RequireFocusLock;
            m_ActiveModal->Snapshot.State = ModalState::Dismissed;
            m_ActiveModal.reset();
            if (requiredFocusLock) {
                ReleaseFocusLock();
            }
            ActivateQueuedModalIfAvailable();
            return true;
        }

        auto queuedIt = std::find_if(
            m_QueuedModals.begin(),
            m_QueuedModals.end(),
            [&modalId](const ModalSession& session) {
                return session.Snapshot.ModalId == modalId;
            });
        if (queuedIt != m_QueuedModals.end()) {
            queuedIt->Snapshot.State = ModalState::Dismissed;
            m_QueuedModals.erase(queuedIt);
            return true;
        }
        return false;
    }

    void ModalDialogService::ClearModals() {
        m_QueuedModals.clear();
        m_ActiveModal.reset();
        if (m_FocusLockActive) {
            ReleaseFocusLock();
        } else {
            m_PreviousFocusedWidgetId.clear();
        }
    }

    void ModalDialogService::Update(float deltaTime) {
        if (deltaTime < 0.0f) {
            deltaTime = 0.0f;
        }

        if (!m_ActiveModal.has_value()) {
            ActivateQueuedModalIfAvailable();
            return;
        }

        ModalSession& active = m_ActiveModal.value();
        active.Snapshot.ElapsedSeconds += deltaTime;
        if (active.Snapshot.TimeoutSeconds > 0.0f &&
            active.Snapshot.ElapsedSeconds >= active.Snapshot.TimeoutSeconds) {
            const std::string timedOutModalId = active.Snapshot.ModalId;
            const bool requiredFocusLock = active.Snapshot.RequireFocusLock;
            active.Snapshot.State = ModalState::Timeout;
            if (active.ActionCallback) {
                active.ActionCallback(timedOutModalId, std::string{});
            }

            m_ActiveModal.reset();
            if (requiredFocusLock) {
                ReleaseFocusLock();
            }
            ActivateQueuedModalIfAvailable();
        }
    }

    bool ModalDialogService::ProcessEvent(const SDL_Event& event) {
        if (!m_ActiveModal.has_value() || !m_ActiveModal->Snapshot.RequireFocusLock) {
            return false;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            const SDL_Keycode key = event.key.key;
            if (key == SDLK_ESCAPE) {
                (void)DismissModal(m_ActiveModal->Snapshot.ModalId);
                return true;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                (void)TriggerAction(m_ActiveModal->Snapshot.ModalId, 0);
                return true;
            }
            if (key >= SDLK_1 && key <= SDLK_9) {
                const uint32_t actionIndex = static_cast<uint32_t>(key - SDLK_1);
                if (actionIndex < m_ActiveModal->Snapshot.ActionKeys.size()) {
                    (void)TriggerAction(m_ActiveModal->Snapshot.ModalId, actionIndex);
                    return true;
                }
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
            event.type == SDL_EVENT_MOUSE_MOTION) {
            return true;
        }

        return false;
    }

    std::optional<ModalSnapshot> ModalDialogService::GetActiveModal() const {
        if (!m_ActiveModal.has_value()) {
            return std::nullopt;
        }
        return m_ActiveModal->Snapshot;
    }

    std::vector<ModalSnapshot> ModalDialogService::GetQueuedModals() const {
        std::vector<ModalSnapshot> snapshots;
        snapshots.reserve(m_QueuedModals.size());
        for (const ModalSession& session : m_QueuedModals) {
            snapshots.push_back(session.Snapshot);
        }
        return snapshots;
    }

    void ModalDialogService::ActivateSession(ModalSession session) {
        session.Snapshot.State = ModalState::Active;
        session.Snapshot.ElapsedSeconds = 0.0f;

        if (session.Snapshot.RequireFocusLock && !m_FocusLockActive) {
            Widget* focusedWidget = UI::Widgets::WidgetSystem::Get().GetFocusedWidget();
            m_PreviousFocusedWidgetId = focusedWidget != nullptr ? focusedWidget->GetId() : std::string{};
            m_FocusLockActive = true;
        }

        m_ActiveModal = std::move(session);
    }

    void ModalDialogService::ActivateQueuedModalIfAvailable() {
        if (m_ActiveModal.has_value() || m_QueuedModals.empty()) {
            return;
        }

        ModalSession session = std::move(m_QueuedModals.front());
        m_QueuedModals.pop_front();
        ActivateSession(std::move(session));
    }

    void ModalDialogService::ReleaseFocusLock() {
        m_FocusLockActive = false;
        if (m_PreviousFocusedWidgetId.empty()) {
            return;
        }

        Widget* previousFocused = UI::Widgets::WidgetSystem::Get().FindWidget(m_PreviousFocusedWidgetId);
        if (previousFocused != nullptr) {
            previousFocused->SetFocused(true);
        }
        m_PreviousFocusedWidgetId.clear();
    }

    std::string ModalDialogService::BuildModalId(uint64_t modalOrdinal) {
        std::ostringstream stream;
        stream << "modal_" << std::hex << std::setw(4) << std::setfill('0') << modalOrdinal;
        return stream.str();
    }

} // namespace Modal
} // namespace UI
} // namespace Core

