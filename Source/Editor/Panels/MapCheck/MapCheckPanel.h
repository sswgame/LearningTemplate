/**
 * @file MapCheckPanel.h
 * @brief 맵 검사 창입니다. `ValidationIssueLog` 의 결과를 테이블로 보이고, 줄을 누르면 그 오브젝트를 고릅니다(언리얼 Map Check).
 * @details MVVM View — 값은 `MapCheckViewModel::gather` 가 담은 `MapCheckViewState` 에서만 읽는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/MapCheck/Model/MapCheckViewModel.h"

namespace sw::editor
{
    /** @brief 로드 · 저장 · 편집 때 모인 검증 결과를 보이고 그 오브젝트로 가는 도구 창입니다. */
    class MapCheckPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`, 상태줄 단추가 연다). */
        static constexpr const utf8* kPanelID = "map_check";

        /** @brief 맵 검사 창을 만듭니다(닫힌 채 시작한다). */
        MapCheckPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Map Check"; }
        /** @brief 거르기 · 테이블을 그립니다. */
        void drawContent() override;
        /** @brief 결과 줄 @p rowIndex 의 오브젝트를 고릅니다(줄 클릭과 같다). 그 오브젝트가 활성 씬에 없으면 false 입니다. */
        bool selectRowObject( uint32 rowIndex ) const;
        /** @brief 지난 그리기의 줄 수(거른 뒤)입니다. */
        uint32 getRowCount() const { return static_cast<uint32>( _viewState._listRow.size() ); }
        /** @brief 지난 그리기에서 안전한 고침(`ValidationPropertyEdit`)이 있는 줄 수입니다(Fix All 단추의 수, 프로브 `Editor.MapCheckFixableRows`). */
        uint32 getFixableRowCount() const { return _viewState._fixableRowCount; }

    private:
        /** @brief 오류 · 경고 거르기, Fix All, 검색 줄을 그립니다. */
        void drawToolbar();

    private:
        MapCheckViewModel                       _viewModel;
        MapCheckViewState                       _viewState;
        fixed_string<constant::buffer::kMax128> _searchBuffer; ///< 검색 입력란 버퍼 — 그린 뒤 ViewModel 에 넘긴다
    };
} // namespace sw::editor
