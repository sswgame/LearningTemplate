/**
 * @file ModulesPanel.h
 * @brief 모듈 창(언리얼 Plugins 창) — 모듈을 종류별로 보이고, 켜고 끄면 함께 바뀌는 모듈을 미리 보인 뒤 프로젝트 매니페스트를 고쳐 씁니다.
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
     * @brief `Bin/Modules` 의 매니페스트(빌드가 복사한 카탈로그)을 읽어 종류별 표를 그립니다. 체크를 바꾸면 미리보기를 묻고, 확인하면
     *        `Source/Games/<활성 게임>/SWGame.module.json` 의 `_listModuleOverride` 를 고칩니다. 적용은 Build(매니페스트가 구성 의존이라 ninja 가 다시 구성한다) 뒤 다시 시작입니다.
     */
    class ModulesPanel final : public IEditorPanel
    {
    public:
        ModulesPanel();

        const utf8* getPanelTitle() const override { return "Modules"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 720.0f, 620.0f }; }

        /** @brief 지금 묻고 있는 미리보기에서 새로 꺼지는 모듈 수입니다(탐침 `Editor.ModulePreviewNewlyInactive`). 묻지 않으면 0 입니다. */
        static uint32 getPreviewNewlyInactiveCount();

    private:
        void               reloadCatalog();
        void               drawPreview();
        [[nodiscard]] bool writeProjectOverride();

        ModuleCatalog                        _catalog;
        ModuleResolution                     _resolution;
        string                               _loadError;
        string                               _pendingModule; ///< 미리보기 중인 모듈(비면 묻지 않는다)
        vector<ModuleInactiveEntry>          _listNewlyInactive;
        vector<string>                       _listNewlyActive;
        bool                                 _bPendingEnabled;
        bool                                 _bChangedSinceStart; ///< 이 실행에서 매니페스트를 고쳤다 — 빌드 · 다시 시작 띠를 보인다
        fixed_string<constant::kMaxBuffer64> _filter;             ///< 이름 · 설명 검색
    };
} // namespace sw::editor
