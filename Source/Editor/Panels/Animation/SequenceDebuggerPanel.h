/**
 * @file SequenceDebuggerPanel.h
 * @brief 컷씬 디버거 — 실행 중인 시퀀스 플레이어 하나의 플레이헤드 · 활성 항목 · 바인딩 상태 · 최근 기록, 그리고 켜진 키트가 보탠 해석(실제 카메라 블렌드 가중치 등)을 읽기 전용으로 보입니다.
 * @details 시퀀서 패널은 편집 중 미리보기이고, 이 창은 재생 중인 `SequencePlayerComponent` 를 본다(언리얼 Sequencer 의 재생 중 디버그 · Rewind Debugger 의 시퀀서 트랙 자리).
 *          키트의 해석은 정적 등록 제공자(`SW_SEQUENCE_DIAGNOSTIC`)라 그 키트 모듈이 올라와 있을 때만 보이고, 내려가면 목록에서 빠진다. 기록은 플레이어마다 고정 용량이다.
 *          값은 `SequenceDebuggerViewModel::gather` 가 모으고, 이 패널은 그리기만 합니다(MVVM 계약).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Animation/Model/SequenceDebuggerViewModel.h"

namespace sw::editor
{
    /** @brief 컷씬 디버거 창입니다(읽기 전용). */
    class SequenceDebuggerPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "sequence_debugger";

        SequenceDebuggerPanel();
        virtual ~SequenceDebuggerPanel() override = default;

        const utf8* getPanelTitle() const override { return "Cutscene Debugger"; }
        void        drawContent() override;

    private:
        void drawPlayerPicker();
        void drawBindingTable();
        void drawHistoryTable();
        void drawProviders();

    private:
        SequenceDebuggerViewModel _viewModel;
        SequenceDebuggerViewState _viewState; ///< 프레임마다 다시 채운다
    };
} // namespace sw::editor
