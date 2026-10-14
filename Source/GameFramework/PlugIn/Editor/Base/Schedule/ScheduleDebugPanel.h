/**
 * @file ScheduleDebugPanel.h
 * @brief NPC 하루 일정 디버그 창입니다. 살아 있는 `ScheduleSystem` 의 NPC 를 골라 오늘 타임라인 표와 "지금 왜 이 행동인가"(`explainNpc`) 글을 보입니다.
 * @details 언리얼 Mass/StateTree 디버거 · 유니티 Timeline 디버그 창의 자리다. 일정 시스템을 바꾸지 않는다 — 읽기 질의(`getPlan` · `getNpcView` · `explainNpc` · `dumpTimeline`)만 쓴다.
 *          시각 슬라이더는 시뮬레이션을 돌리지 않고 오늘 계획에서 그 시각을 덮는 칸을 짚어 보일 뿐이다. 시스템은 매 프레임 `ScheduleSystem::collectLiveSystems` 로 다시 찾는다(포인터를 프레임 너머로 들고 있지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "GameFramework/PlugIn/Editor/Base/Schedule/ScheduleTimelineRows.h"

namespace sw
{
    class ScheduleSystem;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ScheduleDebugPanel
     * @brief NPC 일정 타임라인 · 설명 창입니다.
     */
    class ScheduleDebugPanel final : public IEditorPanel
    {
    public:
        ScheduleDebugPanel();

        const utf8* getPanelTitle() const override { return "NPC Schedule"; }
        bool        isToolPanel() const override { return true; }
        float2      getInitialPanelSize() const override { return float2{ 900.0f, 620.0f }; }
        void        drawContent() override;

    private:
        void drawNpcList( const ScheduleSystem& system );
        void drawTimeline( const ScheduleSystem& system, int32 npcIndex );
        void drawExplanation( const ScheduleSystem& system, int32 npcIndex );

        vector<ScheduleTimelineRow> _listRow;
        string                      _text;
        hashed_string               _selectedNpc;
        int32                       _systemIndex;
        int32                       _previewMinute;
        bool                        _bFollowClock;
    };
} // namespace sw::editor
