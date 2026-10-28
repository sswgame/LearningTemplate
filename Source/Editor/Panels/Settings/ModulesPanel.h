/**
 * @file ModulesPanel.h
 * @brief 모듈 창(언리얼 Plugins 창 · 유니티 Package Manager) — 모듈을 종류별로 보이고(검색 · 종류 · 켜짐 필터), 고른 모듈의 상세(의존 · 역의존 · 덮어쓰기)를 보이며,
 *        켜고 끄거나 기본값으로 되돌리면 함께 바뀌는 모듈을 미리 보인 뒤 프로젝트 매니페스트를 고쳐 씁니다. 설치 · 제거(파일 복사 · 삭제)는 하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "Engine/Module/ModuleCatalog.h"

namespace sw::editor
{
    /**
     * @class ModulesPanel
     * @brief `Bin/Modules` 의 매니페스트(빌드가 복사한 카탈로그)을 읽어 종류별 테이블을 그립니다. 체크를 바꾸면 미리보기를 묻고, 확인하면
     *        `Source/Games/<활성 게임>/SWGame.module.json` 의 `_listModuleOverride` 를 고칩니다. 적용은 Build(매니페스트가 구성 의존이라 ninja 가 다시 구성한다) 뒤 다시 시작입니다.
     */
    class ModulesPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "modules";

        ModulesPanel();

        const utf8*      getPanelTitle() const override { return "Modules"; }
        bool             isToolPanel() const override { return true; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 96.0f, 33.0f ); }

        /** @brief 지금 묻고 있는 미리보기에서 새로 꺼지는 모듈 수입니다(감시 값 `Editor.ModulePreviewNewlyInactive`). 묻지 않으면 0 입니다. */
        static uint32 getPreviewNewlyInactiveCount();

    private:
        void               reloadCatalog();
        void               drawFilters();
        void               drawDetail();
        void               drawPreview();
        void               requestToggle( const string& moduleName, bool bEnabled );
        [[nodiscard]] bool writeProjectOverride();

        ModuleCatalog                        _catalog;
        ModuleResolution                     _resolution;
        string                               _loadError;
        string                               _selectedModule; ///< 상세 영역에 보이는 모듈(비면 상세를 그리지 않는다)
        string                               _pendingModule;  ///< 미리보기 중인 모듈(비면 묻지 않는다)
        int32                                _kindFilter;     ///< 보일 종류(`ModuleKind` 값, -1 은 전부)
        int32                                _stateFilter;    ///< 0 전부 · 1 켜진 것만 · 2 꺼진 것만
        bool                                 _bOpenPreview;   ///< 이번 프레임에 확인 창을 연다(테이블 밖 ID 로 열려고 미룬다)
        vector<ModuleInactiveEntry>          _listNewlyInactive;
        vector<string>                       _listNewlyActive;
        bool                                 _bPendingEnabled;
        bool                                 _bChangedSinceStart; ///< 이 실행에서 매니페스트를 고쳤다 — 빌드 · 다시 시작 띠를 보인다
        fixed_string<constant::kMaxBuffer64> _filter;             ///< 이름 · 설명 검색
    };
} // namespace sw::editor
