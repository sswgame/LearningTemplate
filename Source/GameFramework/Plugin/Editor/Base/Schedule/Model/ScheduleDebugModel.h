/**
 * @file ScheduleDebugModel.h
 * @brief NPC 일정 디버그 창의 모델 — 살아 있는 일정 시스템 고르기, 고른 NPC, 미리 볼 시각(시계 따라가기), 오늘 타임라인 행 · 설명 글, 감시 값 게시. ImGui 를 모릅니다.
 * @details 패널(`ScheduleDebugPanel`)은 목록 · 슬라이더 · 표 · 접는 머리를 그리기만 한다 — 에디터 `Panels/<패널>/Model` 과 같은 선이다(언리얼 StateTree 디버거의 추적 상태와
 *          Slate 뷰가 나뉜 것과 같다). 일정 시스템은 바꾸지 않고 읽기 질의만 쓴다. 시스템은 그리기마다 `refreshSystems` 가 다시 찾는다(포인터는 그 프레임 안에서만 쓴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Plugin/Editor/Base/Schedule/ScheduleTimelineRows.h"

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

    /**
     * @class ScheduleDebugModel
     * @brief NPC 일정 디버그 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class ScheduleDebugModel
    {
    public:
        ScheduleDebugModel();

        /**
         * @brief NPC 가 있는 살아 있는 일정 시스템을 다시 모으고 게시 값을 비운 뒤, 고른 시스템의 NPC 수를 게시합니다. 패널이 그리기마다 맨 먼저 부릅니다.
         * @return 고른 시스템입니다. 없으면 nullptr 입니다(이 프레임 안에서만 씁니다).
         */
        const ScheduleSystem* refreshSystems();
        /** @brief 이번 프레임에 모은 시스템 수입니다. */
        size_t getSystemCount() const { return _listActiveSystem.size(); }
        int32  getSystemIndex() const { return _systemIndex; }
        /** @brief 시스템 @p systemIndex 를 고릅니다(범위 밖이면 첫 시스템). 고른 시스템의 NPC 수를 게시하고 그 시스템을 돌려줍니다. */
        const ScheduleSystem* selectSystem( int32 systemIndex );

        const hashed_string& getSelectedNPC() const { return _selectedNPC; }
        void                 selectNPC( const hashed_string& npcID ) { _selectedNPC = npcID; }
        /** @brief 고른 NPC 의 @p system 안 번호입니다. 고르지 않았거나 없으면 -1 입니다. */
        int32 findSelectedNPCIndex( const ScheduleSystem& system ) const;

        /** @brief 시계를 따라가면 미리 볼 시각을 @p system 의 지금 시각으로 맞춥니다. */
        void  followClock( const ScheduleSystem& system );
        bool& editFollowClock() { return _bFollowClock; }
        int32 getPreviewMinute() const { return _previewMinute; }
        /** @brief 미리 볼 시각을 @p minute 으로 두고 시계 따라가기를 끕니다(슬라이더). */
        void setPreviewMinute( int32 minute );

        /** @brief NPC @p npcIndex 의 오늘 계획을 미리 볼 시각 기준 표 행으로 다시 짓고 그 줄 수를 게시합니다. */
        const vector<ScheduleTimelineRow>& refreshTimeline( const ScheduleSystem& system, int32 npcIndex );
        /** @brief NPC @p npcIndex 가 지금 이 행동인 이유입니다(지금 시각 기준 — 미리 볼 시각과 상관없다). 다음 호출까지 유효합니다. */
        const string& explain( const ScheduleSystem& system, int32 npcIndex );
        /** @brief NPC @p npcIndex 의 날 것 타임라인 글입니다(지금 시각 기준). 다음 호출까지 유효합니다. */
        const string& dumpTimeline( const ScheduleSystem& system, int32 npcIndex );

        const ScheduleDebugPublished& getPublished() const { return _published; }

    private:
        vector<const ScheduleSystem*> _listActiveSystem; ///< NPC 가 있는 살아 있는 시스템. `refreshSystems` 가 그리기마다 다시 채운다(프레임 너머로 쓰지 않는다)
        vector<ScheduleTimelineRow>   _listRow;
        string                        _text;
        hashed_string                 _selectedNPC;
        int32                         _systemIndex;
        int32                         _previewMinute;
        bool                          _bFollowClock;
        ScheduleDebugPublished        _published; ///< 감시 값이 읽는 게시 값
    };
} // namespace sw::editor
