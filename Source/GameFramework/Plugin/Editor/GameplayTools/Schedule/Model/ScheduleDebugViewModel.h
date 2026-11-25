/**
 * @file ScheduleDebugViewModel.h
 * @brief NPC 일정 디버그 창의 모델 — 살아 있는 일정 시스템 고르기, 고른 NPC, 미리 볼 시각(시계 따라가기), 오늘 타임라인 행 · 설명 글, 감시 값 게시. ImGui 를 모릅니다.
 * @details 패널(`ScheduleDebugPanel`)은 목록 · 슬라이더 · 표 · 접는 머리를 그리기만 한다 — 에디터 `Panels/<패널>/Model` 과 같은 선이다(언리얼 StateTree 디버거의 추적 상태와
 *          Slate 뷰가 나뉜 것과 같다). 일정 시스템은 바꾸지 않고 읽기 질의만 쓴다. 시스템은 그리기마다 `refreshSystems` 가 다시 찾는다(포인터는 그 프레임 안에서만 쓴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Schedule/Model/ScheduleTimelineRows.h"

namespace sw
{
    class ScheduleSystem;
} // namespace sw

namespace sw::editor
{
    /** @brief 자동화 감시 값이 읽는 값입니다. 창이 그릴 때마다 게시합니다(게임 스레드). 창이 닫히면 마지막 값이 남습니다. 값은 패널 인스턴스의 것이라 에디터마다 따로입니다. */
    struct ScheduleDebugPublished
    {
        uint32 _npcCount{ 0 };         ///< 고른 일정 시스템의 NPC 수
        uint32 _timelineRowCount{ 0 }; ///< 고른 NPC 의 오늘 타임라인 줄 수(고른 NPC 가 없으면 0)
    };

    /** @brief 창이 한 프레임 그릴 값입니다(`ScheduleDebugViewModel::gather`). 값만 담는다 — 일정 시스템 포인터를 들지 않는다. */
    struct ScheduleDebugViewState
    {
        vector<string>              _listNPC;            ///< 고른 시스템의 NPC id(시스템 안 번호 순서)
        vector<ScheduleTimelineRow> _listRow;            ///< 고른 NPC 의 오늘 타임라인(미리 볼 시각 기준)
        string                      _explanation;        ///< "지금 왜 이 행동인가"(접는 머리가 열려 있을 때만 채운다)
        string                      _rawTimeline;        ///< 날 것 타임라인 글(접는 머리가 열려 있을 때만 채운다)
        int32                       _systemCount{ 0 };   ///< NPC 가 있는 살아 있는 일정 시스템 수
        int32                       _systemIndex{ 0 };   ///< 고른 시스템 번호
        optional<int32>             _selectedNPC{};      ///< 고른 NPC 의 `_listNPC` 번호. 없으면 비어 있다
        int32                       _previewMinute{ 0 }; ///< 미리 볼 시각(그날의 분)
        int32                       _nowMinute{ 0 };     ///< 게임 시각(그날의 분)
        int32                       _day{ 0 };           ///< 게임 날짜
        uint8                       _bHasSystem{ SW_FALSE };
        uint8                       _bPlayStopped{ SW_FALSE }; ///< 에디터 플레이가 멈춰 있다(빈 상태의 "플레이" 단추)
        uint8                       _bFollowClock{ SW_FALSE };
    };

    /**
     * @class ScheduleDebugViewModel
     * @brief NPC 일정 디버그 창의 ViewModel 입니다 — 그리기마다 `gather` 가 값을 모으고, 창은 의도를 명령으로 넘깁니다. 파일 머리말 참고.
     * @details 일정 시스템 포인터는 `gather` 안에서만 쓴다(시스템은 매번 `ScheduleSystem::collectLiveSystems` 로 다시 찾는다).
     */
    class ScheduleDebugViewModel
    {
    public:
        ScheduleDebugViewModel();

        /** @brief 살아 있는 일정 시스템을 다시 모아 이번 프레임 값을 @p outState 에 채우고 감시 값을 게시합니다. 창이 그리기 맨 앞에서 한 번 부릅니다. */
        void gather( ScheduleDebugViewState& outState );

        /** @brief 시스템 @p systemIndex 를 고릅니다(범위 밖이면 다음 `gather` 가 첫 시스템으로 둔다). */
        void selectSystem( int32 systemIndex ) { _systemIndex = systemIndex; }
        /** @brief NPC @p npcID 를 고릅니다. */
        void selectNPC( string_view npcID ) { _selectedNPC = hashed_string( npcID ); }
        /** @brief 미리 볼 시각이 게임 시계를 따라갈지 정합니다. */
        void setFollowClock( bool bFollow ) { _bFollowClock = bFollow; }
        /** @brief 미리 볼 시각을 @p minute 으로 두고 시계 따라가기를 끕니다(슬라이더). */
        void setPreviewMinute( int32 minute );
        /** @brief 설명 · 날 것 타임라인 글을 보이는지 알립니다(보기 상태 — 기록하지 않는다). 다음 `gather` 부터 보이는 글만 짓는다. */
        void setTextShown( bool bExplanation, bool bRawTimeline );

        const ScheduleDebugPublished& getPublished() const { return _published; }

    private:
        vector<const ScheduleSystem*> _listActiveSystem; ///< NPC 가 있는 살아 있는 시스템. `gather` 안에서만 쓴다
        hashed_string                 _selectedNPC;
        int32                         _systemIndex;
        int32                         _previewMinute;
        uint8                         _bFollowClock      : 1;
        uint8                         _bExplanationShown : 1;
        uint8                         _bRawTimelineShown : 1;
        ScheduleDebugPublished        _published; ///< 감시 값이 읽는 게시 값
    };
} // namespace sw::editor
