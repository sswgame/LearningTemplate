/**
 * @file GlobalVariablesPanel.h
 * @brief 전역 변수(치트, 디버그 플래그, 환경 설정 등)를 살펴보고 편집하는 에디터 창입니다.
 * @details 보일 목록 · 핀 · 프리셋은 `GlobalVariablesModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 검색 글 · 프리셋 이름 입력란 · 모듈별 묶기 토글만 든다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Settings/Model/GlobalVariablesModel.h"

namespace sw
{
    struct GlobalVariableInfo;

    class GlobalVariableManager;
} // namespace sw

namespace sw::editor
{
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
        uint32 getListRebuildCount() const { return _model.getListRebuildCount(); }

    private:
        /** @brief 단일 전역 변수의 편집 컨트롤을 그립니다. */
        void drawVariableRow( GlobalVariableInfo& info, bool bShowPin );
        void markSessionDirty();

    private:
        GlobalVariablesModel                  _model;
        fixed_string<constant::kMaxBuffer128> _searchFilter;
        fixed_string<constant::kMaxBuffer64>  _presetNameBuf;
        bool                                  _bGroupByModule;
    };
} // namespace sw::editor
