/**
 * @file ScheduleDebugPanel.h
 * @brief NPC 하루 일정 디버그 창입니다. 살아 있는 `ScheduleSystem` 의 NPC 를 골라 오늘 타임라인 표와 "지금 왜 이 행동인가"(`explainNPC`) 글을 보입니다.
 * @details 언리얼 Mass/StateTree 디버거 · 유니티 Timeline 디버그 창의 자리다. 일정 시스템을 바꾸지 않는다 — 읽기 질의(`getPlan` · `getNPCView` · `explainNPC` · `dumpTimeline`)만 쓴다.
 *          시각 슬라이더는 시뮬레이션을 돌리지 않고 오늘 계획에서 그 시각을 덮는 칸을 짚어 보일 뿐이다. 시스템은 매 프레임 `ScheduleSystem::collectLiveSystems` 로 다시 찾는다(포인터를 프레임 너머로 들고 있지 않는다).
 *          고름 · 미리 볼 시각 · 행 · 설명 글 · 게시 값은 `ScheduleDebugModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기만 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

#include "GameFramework/Plugin/Editor/Base/Schedule/Model/ScheduleDebugModel.h"

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
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "npc_schedule";

        ScheduleDebugPanel();

        const utf8*      getPanelTitle() const override { return "NPC Schedule"; }
        bool             isToolPanel() const override { return true; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 120.0f, 33.0f ); }
        void             drawContent() override;

        /** @brief 자동화 감시 값이 읽는 값입니다(모델이 그리기마다 게시한다). */
        const ScheduleDebugPublished& getPublished() const { return _model.getPublished(); }

    private:
        void drawNPCList( const ScheduleSystem& system );
        void drawTimeline( const ScheduleSystem& system, int32 npcIndex );
        void drawExplanation( const ScheduleSystem& system, int32 npcIndex );

        ScheduleDebugModel _model;
    };
} // namespace sw::editor
