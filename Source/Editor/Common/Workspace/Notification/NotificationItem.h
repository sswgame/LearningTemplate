/**
 * @file NotificationItem.h
 * @brief 토스트 알림 하나의 값입니다 — `EditorNotificationManager` 가 쌓고 `EditorNotificationView` 가 그린다.
 * @details 목록의 주인에서 값만 떼어 냈다 — MVVM View 가 서브시스템 헤더를 보지 않고 그리게.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 알림 유형입니다. 색은 토스트의 테두리와 제목이 함께 쓴다. */
    ENUM()
    enum class NotificationKind : uint8
    {
        Info    ENUMERATOR( Color = "Signal.Info" ) = 0,
        Success ENUMERATOR( Color = "Signal.Success" ),
        Warning ENUMERATOR( Color = "Signal.Warning" ),
        Error   ENUMERATOR( Color = "Signal.Error" )
    };

    /** @brief 개별 토스트 알림 항목 */
    struct NotificationItem
    {
        string           _title;
        string           _message;
        NotificationKind _type{ NotificationKind::Info };
        float32          _durationSec{ 4.0f }; ///< 0 보다 커야 한다(`push` 가 단언)
        float32          _elapsedSec{ 0.0f };
        float32          _progress{ -1.0f }; ///< 0.0 ~ 1.0 이면 프로그레스 바 표시, 음수면 미표시
        string           _revealPath;        ///< 비어 있지 않으면 "Show in Explorer" 단추가 이 절대 경로를 연다
    };
} // namespace sw::editor
