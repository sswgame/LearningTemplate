/**
 * @file EditorNotificationView.h
 * @brief 토스트 알림 큐(`EditorNotificationManager`)를 화면 오른쪽 아래에 그립니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    class EditorNotificationManager;

    /**
     * @struct EditorNotificationView
     * @brief 알림 그리기입니다. 목록과 시간 진행은 `EditorNotificationManager`(ImGui 없음)가 들고, 여기는 그리기만 합니다.
     */
    struct EditorNotificationView
    {
        /** @brief 떠 있는 알림을 메인 뷰포트 오른쪽 아래부터 쌓아 그립니다. 메인 뷰포트가 없으면 호출 시점이 틀린 것이라 멈춥니다. */
        static void draw( const EditorNotificationManager& manager );
    };
} // namespace sw::editor
