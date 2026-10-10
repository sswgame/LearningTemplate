/**
 * @file MapCheckPanel.h
 * @brief 맵 검사 창입니다. `ValidationIssueLog` 의 결과를 표로 보이고, 줄을 누르면 그 오브젝트를 고릅니다(언리얼 Map Check).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/MapCheckRows.h"

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
        /** @brief 거르기 · 표를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }

        /**
         * @brief 활성 씬의 모든 오브젝트를 다시 검증합니다(Check Map 단추와 같다).
         * @return 검증한 오브젝트 수
         */
        static uint32 validateActiveScene();
        /** @brief 결과 줄 @p rowIndex 의 오브젝트를 고릅니다(줄 클릭과 같다). 그 오브젝트가 활성 씬에 없으면 false 입니다. */
        bool selectRowObject( uint32 rowIndex ) const;
        /** @brief 지난 그리기의 줄 수(거른 뒤)입니다. */
        uint32 getRowCount() const { return static_cast<uint32>( _listRow.size() ); }

    private:
        /** @brief 결과 번호가 바뀌었거나 거르기가 바뀌었으면 줄을 다시 만듭니다. */
        void syncRows();

    private:
        vector<ValidationIssue>               _listRow;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        string                                _builtSearch; ///< 줄을 만들 때의 검색어
        MapCheckCounts                        _counts;
        uint32                                _builtRevision; ///< 줄을 만들 때의 `ValidationIssueLog::getRevision`
        uint8                                 _bShowErrors   : 1;
        uint8                                 _bShowWarnings : 1;
        uint8                                 _bRowsDirty    : 1;
        [[maybe_unused]] uint8                _reservedFlags : 5;
    };
} // namespace sw::editor
