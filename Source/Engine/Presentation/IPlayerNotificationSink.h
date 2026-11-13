/**
 * @file Engine/Presentation/IPlayerNotificationSink.h
 * @brief 플레이어에게 짧은 글을 알리는 창구입니다 — 저장 · 게임 흐름이 알림 화면(UI)을 모르고 알립니다. 구현은 `UINotificationService` 이고, 없으면(UI 를 넣지 않은 하네스) 칸이 비어 알리지 않습니다.
 * @details 서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /** @brief 알림의 종류입니다. 알림 화면이 모양 · 순서를 고른다(힌트는 지금 할 일이라 쌓인 알림보다 먼저 보인다). */
    enum class PlayerNotificationKind : uint8
    {
        Info, ///< 일반 알림(저장됨 · 게임 흐름)
        Hint  ///< 튜토리얼 힌트(리치 텍스트 행동 태그 `[action=이름]` 을 쓸 수 있다)
    };

    /** @class IPlayerNotificationSink @brief 플레이어 알림을 받는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_API IPlayerNotificationSink
    {
    public:
        virtual ~IPlayerNotificationSink() = default;

        /** @brief @p text(현지화 키 또는 글 그대로)를 @p durationSeconds 동안 @p kind 모양으로 알립니다. 같은 글이 떠 있으면 센다. */
        virtual void postNotice( const string& text, float32 durationSeconds, PlayerNotificationKind kind ) = 0;

        SW_SERVICE_SLOT( IPlayerNotificationSink, Optional );
    };
} // namespace sw
