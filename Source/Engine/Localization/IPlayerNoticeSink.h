/**
 * @file Engine/Localization/IPlayerNoticeSink.h
 * @brief 플레이어에게 짧은 글을 알리는 창구입니다 — 저장 · 게임 흐름이 알림 화면(UI)을 모르고 알립니다. 구현은 `UINotificationService` 이고, 없으면(UI 를 넣지 않은 하네스) 칸이 비어 알리지 않습니다.
 * @details 서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /** @class IPlayerNoticeSink @brief 플레이어 알림을 받는 쪽입니다. 게임 스레드에서 부릅니다. */
    class IPlayerNoticeSink
    {
    public:
        virtual ~IPlayerNoticeSink() = default;

        /** @brief @p text(현지화 키 또는 글 그대로)를 @p durationSeconds 동안 알립니다. 같은 글이 떠 있으면 센다. */
        virtual void postNotice( const string& text, float32 durationSeconds ) = 0;
    };

    SW_SERVICE_SLOT( IPlayerNoticeSink, Optional );
} // namespace sw
