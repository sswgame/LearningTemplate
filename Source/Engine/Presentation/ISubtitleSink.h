/**
 * @file Engine/Presentation/ISubtitleSink.h
 * @brief 말하는 이와 줄을 자막으로 보이는 창구입니다 — 대화 진행 · 시퀀서 자막 트랙 같은 코드가 자막 화면(UI)을 모르고 줄을 냅니다. 구현은 `UISubtitleService` 이고, 없으면(전용 서버 · UI 를 넣지 않은 하네스) 칸이 비어 보이지 않습니다.
 * @details 서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다(`IPlayerNotificationSink` 와 같은 모양).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @class ISubtitleSink
     * @brief 자막 줄을 받는 쪽입니다. 게임 스레드에서 부릅니다.
     * @details 올리기는 줄 번호를 돌려주고, 그 번호로 시간보다 일찍 내릴 수 있다 — 시퀀서가 구간 중간에서 멈추거나 되감을 때, 대화를 건너뛸 때 쓴다
     *          (언리얼 `FSubtitleManager::KillSubtitles` · 시퀀서 자막 섹션을 나갈 때와 같은 자리).
     */
    class SW_API ISubtitleSink
    {
    public:
        virtual ~ISubtitleSink() = default;

        /**
         * @brief @p speaker 의 줄 @p text 를 자막으로 올립니다. 둘 다 현지화 키 · 메시지(`LocalizedMessage`) · 원문이며 그릴 때 풀립니다(푼 글을 넘기면 언어를 바꿔도 그대로다). 보이는 시간은 @p durationSeconds 이고 0 이하면 읽기 시간입니다(대화 진행은 읽기 시간, 시퀀서 자막은 구간 길이).
         * @return 줄 번호(1 부터)입니다. 들고 있다가 다음 줄 · 끝 · 건너뛰기에서 `removeSubtitle` 에 넘깁니다(버리면 앞 줄이 읽기 시간만큼 남는다).
         */
        [[nodiscard]] virtual uint32 postSubtitle( string_view speaker, string_view text, float32 durationSeconds ) = 0;
        /** @brief 줄 @p lineID 를 지금 내립니다(보이는 중이든 기다리는 중이든). 이미 내려간 번호면 아무 일도 하지 않습니다. */
        virtual void removeSubtitle( uint32 lineID ) = 0;
        /** @brief 모든 줄을 내립니다(장면 전환 · 컷씬 건너뛰기). */
        virtual void clearSubtitles() = 0;

        SW_SERVICE_SLOT( ISubtitleSink, Optional );
    };
} // namespace sw
