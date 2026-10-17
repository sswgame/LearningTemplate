/**
 * @file ProjectSettingsPanel.h
 * @brief 프로젝트 설정 창입니다(Edit > Project Settings). `Config/` 와 팩의 설정 JSON 을 프로퍼티 그리드로 고치고, 저장하면 실행 중인 호스트가 다시 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsCatalog.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ProjectSettingsPanel
     * @brief 언리얼 Project Settings · 유니티 Project Settings 에 해당합니다. 왼쪽은 설정 파일 목록, 오른쪽은 고른 파일의 그리드입니다.
     * @details 목록은 생성 문서 `docs/Config/ConfigReference.json` 에서 만든다(`ProjectSettingsCatalog`). 저장은 Save(또는 Ctrl+S)로 하고, 기본값과 같은 값은 파일에서 지운다.
     *          저장하면 `ConfigHotReload::reloadChangedFile` 이 호스트 설정을 제자리에서 다시 읽는다(`ConfigManager::reloadConfigFile`).
     *          창 시작 값만 쓰는 필드(창 크기 · 백엔드)는 다음 실행부터 반영된다. 실행 중 반영 여부는 설정 색인 문서(`docs/Config/`)에 있다.
     */
    class ProjectSettingsPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "projectsettings";

        ProjectSettingsPanel();
        ~ProjectSettingsPanel() override;

        const utf8* getPanelTitle() const override { return "Project Settings"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 820.0f, 520.0f }; }

        /** @brief 지난 프레임 목록에 오른 설정 파일 수입니다(탐침 `Editor.ProjectSettingsPageCount`). */
        static uint32 getPageCount();
        /** @brief 고른 파일의 현재 값에서 기본값과 다른 키 수입니다(탐침 `Editor.ProjectSettingsKeyCount`). 고른 파일이 없으면 0 입니다. */
        static uint32 getSelectedKeyCount();
        /** @brief 저장하지 않은 편집이 있는 파일 수입니다(탐침 `Editor.ProjectSettingsDirtyPageCount`). */
        static uint32 getDirtyPageCount();

    protected:
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

    private:
        /** @brief 목록의 파일 하나와 그 편집 상태입니다. 처음 고를 때 연다. */
        struct Page
        {
            ProjectSettingsEntry    _entry;
            ProjectSettingsDocument _document;
            string                  _absolutePath;
            bool                    _bOpened{ false };
            bool                    _bTypeFound{ false };
            bool                    _bDirty{ false };
        };

    private:
        void reloadCatalog();
        void openPage( Page& page );
        void onPropertyEdited( const PropertyInfo& prop );
        /** @brief 고른 파일을 저장하고 호스트에 알립니다. 호스트가 반영했으면 outApplied 가 true 입니다. */
        [[nodiscard]] bool savePage( Page& page, bool& outApplied );
        void               refreshDirtyState();
        void               drawPageList();
        void               drawPageDetail( Page& page );

    private:
        vector<unique_ptr<Page>> _listPage;
        EditorPropertyGrid       _grid;
        Page*                    _pSelected;
        string                   _statusText;
        bool                     _bCatalogLoaded;
    };
} // namespace sw::editor
