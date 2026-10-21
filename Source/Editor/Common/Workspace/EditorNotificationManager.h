/**
 * @file EditorNotificationManager.h
 * @brief 에디터 토스트 알림 큐입니다(EditorContext 소유, ImGui 없음). 그리기는 `EditorNotificationView` 가 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    /** @brief 알림 유형 */
    enum class NotificationType : uint8
    {
        Info = 0,
        Success,
        Warning,
        Error
    };

    /** @brief 개별 토스트 알림 항목 */
    struct NotificationItem
    {
        string           _title;
        string           _message;
        NotificationType _type{ NotificationType::Info };
        float32          _durationSec{ 4.0f }; ///< 0 보다 커야 한다(`push` 가 단언)
        float32          _elapsedSec{ 0.0f };
        float32          _progress{ -1.0f }; ///< 0.0 ~ 1.0 이면 프로그레스 바 표시, 음수면 미표시
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNotificationManager
     * @brief 화면 오른쪽 아래에 뜨는 토스트 알림과 진행 막대의 목록입니다. 커맨드 · 작업 공간은 이것에 쌓기만 하고, 셸이 프레임마다
     *        `update` 로 시간을 진행한 뒤 `EditorNotificationView::draw` 로 그립니다.
     */
    class SW_EDITOR_API EditorNotificationManager : public EditorSubsystem
    {
    public:
        EditorNotificationManager()                                              = default;
        EditorNotificationManager( const EditorNotificationManager& )            = delete;
        EditorNotificationManager& operator=( const EditorNotificationManager& ) = delete;
        ~EditorNotificationManager() override                                    = default;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorNotificationManager";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 알림을 하나 쌓습니다. **메인 스레드에서만 부릅니다.**
         * @details @p durationSec 은 0 보다 커야 합니다(아니면 단언하며, 다른 값으로 바꿔 쓰지 않습니다).
         * 목록에는 락이 없습니다. `update` 와 그리기가 프레임마다 같은 벡터를 순회하고 지우므로, 다른 스레드에서 push
         *          하면 순회 중 재할당으로 죽습니다. 백그라운드에서 알리고 싶으면 결과를 큐에 담아 메인 스레드에서 꺼내 push
         *          하십시오(`EditorBackgroundIO` 의 publish 방식. 파일 대화 상자는 `FileUtil::pumpFileDialogResults` 가 그렇게
         *          넘겨줍니다).
         */
        void push( string_view title, string_view message, NotificationType type = NotificationType::Info,
                   float32 durationSec = 4.0f, float32 progress = -1.0f );
        /** @brief 시간을 @p deltaTime 초 진행하고 표시 시간이 끝난 알림을 지웁니다. */
        void update( float32 deltaTime );
        void clear() { _listNotification.clear(); }

        /** @brief 지금 떠 있는 알림입니다(쌓인 순서). */
        const vector<NotificationItem>& getNotifications() const { return _listNotification; }

    private:
        vector<NotificationItem> _listNotification;
    };
} // namespace sw::editor
