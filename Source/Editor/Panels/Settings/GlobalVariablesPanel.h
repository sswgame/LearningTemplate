/**
 * @file GlobalVariablesPanel.h
 * @brief 전역 변수(치트, 디버그 플래그, 환경 설정 등)를 살펴보고 편집하는 에디터 창입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw
{
    struct GlobalVariableInfo;

    class GlobalVariableManager;
} // namespace sw

namespace sw::editor
{
    class EditorListFilter;

    /** @brief 등록된 모든 전역 변수를 나열하고 실시간으로 편집하는 에디터 도구 창입니다. */
    class GlobalVariablesPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "global_variables";

        /** @brief 전역 변수 창을 만듭니다(도구 창이라 닫힌 채 시작합니다). */
        GlobalVariablesPanel();
        /** @brief 추가 해제할 GPU 리소스는 없습니다. */
        virtual ~GlobalVariablesPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Global Variables"; }
        /** @brief 전역 변수 UI를 그립니다. */
        void drawContent() override;
        /** @brief 검색·그룹화·리셋·프리셋 툴바를 그립니다. */
        void drawVariableToolbar( GlobalVariableManager& gvm );
        /** @brief "Presets..." 단추의 팝업입니다. 이름을 붙여 저장하고, 저장한 프리셋 목록에서 불러옵니다. */
        void drawPresetsPopup();
        /** @brief 핀 고정된 즐겨찾기 변수 섹션을 그립니다. */
        void drawPinnedSection( GlobalVariableManager& gvm );
        /** @brief 모듈별 그룹 또는 평면 모드로 변수 테이블을 그립니다. */
        void drawVariableTable( const vector<GlobalVariableInfo*>& listFiltered );
        /** @brief 기본 창 크기를 반환합니다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 91.0f, 25.0f ); }
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool               isToolPanel() const override { return true; }
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;
        /** @brief 값은 전역 변수 관리자에 바로 적용되어 있어 창을 닫아도 잃는 것이 없다 — 닫기에 확인을 두면 저장하거나 값을 초기화하지 않고는 창을 숨길 수 없다. */
        bool asksBeforeClose() const override { return false; }
        /** @brief 보일 변수 목록을 다시 지은 횟수입니다. 프로브 `Editor.GlobalVariablesRebuildCount` 가 읽습니다(등록 · 검색 글이 그대로면 늘지 않는다). */
        uint32 getListRebuildCount() const { return _listRebuildCount; }

    private:
        /** @brief 단일 전역 변수의 편집 컨트롤을 그립니다. */
        void drawVariableRow( GlobalVariableInfo& info, bool bShowPin );
        void markSessionDirty();
        /** @brief 보일 변수 목록(테스트용 제외, @p filter 통과, 모듈 · 이름 순)을 다시 짓고 그때의 등록 세대와 검색 글을 적습니다. */
        void rebuildVariableList( const GlobalVariableManager& gvm, const EditorListFilter& filter );

    private:
        unordered_set<string> _uniquePinnedVar;
        EditorFileCollectJob  _presetJob;
        vector<string>        _listPresetFile;
        /** @brief 보일 변수입니다. 등록 세대(`GlobalVariableManager::getRegistrationGeneration`)나 검색 글이 바뀐 프레임에만 다시 채운다. */
        vector<GlobalVariableInfo*>           _listFilteredVariable;
        string                                _listFilterText;     ///< 목록을 지은 때의 검색 글
        uint64                                _listGeneration;     ///< 목록을 지은 때의 등록 세대(0 이면 아직 짓지 않았다 — 세대는 1 부터)
        uint32                                _totalVariableCount; ///< 테스트용을 뺀 변수 수(필터 전)
        uint32                                _listRebuildCount;
        fixed_string<constant::kMaxBuffer128> _searchFilter;
        fixed_string<constant::kMaxBuffer64>  _presetNameBuf;
        uint8                                 _bGroupByModule   : 1;
        uint8                                 _bPresetListDirty : 1;
        [[maybe_unused]] uint8                _reserved         : 6;
    };
} // namespace sw::editor
