/**
 * @file EditorNotificationManager.h
 * @brief 에디터 비동기 토스트 알림 관리자 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

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
        float32          _durationSec{ 4.0f };
        float32          _elapsedSec{ 0.0f };
        float32          _progress{ -1.0f }; ///< 0.0 ~ 1.0 이면 프로그레스 바 표시, 음수면 미표시
    };

    /**
     * @class EditorNotificationManager
     * @brief 화면 오른쪽 아래의 비동기 토스트 알림과 진행 막대를 그립니다.
     */
    class EditorNotificationManager
    {
    public:
        EditorNotificationManager()  = default;
        ~EditorNotificationManager() = default;

        /**
         * @brief 알림을 하나 쌓습니다. **메인 스레드에서만 부릅니다.**
         * @details 목록에는 락이 없습니다. `updateAndDraw` 가 프레임마다 같은 벡터를 순회하고 지우므로, 다른 스레드에서 push
         *          하면 순회 중 재할당으로 죽습니다. 백그라운드에서 알리고 싶으면 결과를 큐에 담아 메인 스레드에서 꺼내 push
         *          하십시오(`EditorBackgroundIo` 의 publish 방식. 파일 대화 상자는 `FileUtil::pumpFileDialogResults` 가 그렇게
         *          넘겨줍니다).
         */
        void push( string_view title, string_view message, NotificationType type = NotificationType::Info,
                   float32 durationSec = 4.0f, float32 progress = -1.0f );
        void updateAndDraw( float32 deltaTime, float32 screenWidth, float32 screenHeight );
        void clear() { _listNotification.clear(); }

    private:
        vector<NotificationItem> _listNotification;
    };
} // namespace sw::editor
