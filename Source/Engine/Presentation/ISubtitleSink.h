/**
 * @file Engine/Presentation/ISubtitleSink.h
 * @brief 말하는 이와 줄을 자막으로 보이는 창구입니다 — 대화 진행 같은 게임 코드가 자막 화면(UI)을 모르고 줄을 냅니다. 구현은 `UISubtitleService` 이고, 없으면(전용 서버 · UI 를 넣지 않은 하네스) 칸이 비어 보이지 않습니다.
 * @details 서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다(`IPlayerNotificationSink` 와 같은 모양).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /** @class ISubtitleSink @brief 자막 줄을 받는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_API ISubtitleSink
    {
    public:
        virtual ~ISubtitleSink() = default;

        /** @brief @p speaker 의 줄 @p text 를 자막으로 올립니다. 보이는 시간은 @p durationSeconds 이고 0 이하면 읽기 시간입니다(대화 진행은 읽기 시간, 시퀀서 자막은 구간 길이). */
        virtual void postSubtitle( string_view speaker, string_view text, float32 durationSeconds ) = 0;

        SW_SERVICE_SLOT( ISubtitleSink, Optional );
    };
} // namespace sw
