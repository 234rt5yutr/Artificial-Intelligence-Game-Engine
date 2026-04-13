#pragma once

#include "Core/UI/Localization/LocalizationService.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace UI {
namespace Modal {

    constexpr const char* UI_MODAL_INVALID_REQUEST = "UI_MODAL_INVALID_REQUEST";
    constexpr const char* UI_MODAL_LOCALIZATION_FAILURE = "UI_MODAL_LOCALIZATION_FAILURE";
    constexpr const char* UI_MODAL_FOCUS_LOCK_CONFLICT = "UI_MODAL_FOCUS_LOCK_CONFLICT";

    enum class ModalState : uint8_t {
        Queued = 0,
        Active,
        Dismissed,
        Timeout
    };

    struct ShowLocalizedModalDialogRequest {
        std::string DialogId;
        std::string Locale;
        std::string TitleKey;
        std::string BodyKey;
        std::vector<std::string> ActionKeys;
        std::string FallbackLocale;
        bool RequireFocusLock = true;
        bool QueueIfBusy = true;
        float TimeoutSeconds = 0.0f;
        std::function<void(const std::string& modalId, const std::string& actionKey)> ActionCallback;
    };

    struct ShowLocalizedModalDialogResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        std::string ModalId;
        std::string ResolvedLocale;
        bool FallbackUsed = false;
        ModalState State = ModalState::Queued;
    };

    struct ModalSnapshot {
        std::string ModalId;
        std::string DialogId;
        std::string TitleText;
        std::string BodyText;
        std::vector<std::string> ActionLabels;
        std::vector<std::string> ActionKeys;
        std::string ResolvedLocale;
        bool FallbackUsed = false;
        ModalState State = ModalState::Queued;
        float TimeoutSeconds = 0.0f;
        float ElapsedSeconds = 0.0f;
        bool RequireFocusLock = true;
    };

    class ModalDialogService {
    public:
        static ModalDialogService& Get();

        ShowLocalizedModalDialogResult ShowLocalizedModalDialog(
            const ShowLocalizedModalDialogRequest& request);
        bool TriggerAction(const std::string& modalId, uint32_t actionIndex);
        bool DismissModal(const std::string& modalId);
        void ClearModals();

        void Update(float deltaTime);
        bool ProcessEvent(const SDL_Event& event);

        bool HasFocusLock() const { return m_FocusLockActive; }
        std::optional<ModalSnapshot> GetActiveModal() const;
        std::vector<ModalSnapshot> GetQueuedModals() const;

    private:
        struct ModalSession {
            ModalSnapshot Snapshot;
            std::function<void(const std::string&, const std::string&)> ActionCallback;
        };

        ModalDialogService() = default;

        void ActivateSession(ModalSession session);
        void ActivateQueuedModalIfAvailable();
        void ReleaseFocusLock();
        static std::string BuildModalId(uint64_t modalOrdinal);

    private:
        uint64_t m_NextModalOrdinal = 1;
        bool m_FocusLockActive = false;
        std::string m_PreviousFocusedWidgetId;
        std::optional<ModalSession> m_ActiveModal;
        std::deque<ModalSession> m_QueuedModals;
    };

} // namespace Modal
} // namespace UI
} // namespace Core

