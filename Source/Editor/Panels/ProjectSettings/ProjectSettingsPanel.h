/**
 * @file ProjectSettingsPanel.h
 * @brief 프로젝트 설정 창입니다(Edit > Project Settings). `Config/` 와 팩의 설정 JSON, 활성 팩 `gamesettings.xml` 의 `<Engine>` 요소를 프로퍼티 그리드로, 반사 형식이 없는 `Config/` 표(린트 · 격리 · 환경 · 개발 표)를 범용 JSON 행으로 고칩니다.
 * @details JSON 호스트 설정은 저장하면 실행 중인 호스트가 다시 읽습니다. 팩의 `<Engine>` 요소(`PackEngineSettings`)는 다음 기동부터 반영됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/Panel/EditorStateDocumentPanel.h"
#include "Editor/Common/Widgets/PropertyGrid/EditorPropertyGrid.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsEntry.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsHistory.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"
#include "Editor/Panels/ProjectSettings/ProjectSettingsTableView.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ProjectSettingsPanel
     * @brief 언리얼 Project Settings · 유니티 Project Settings 에 해당합니다. 왼쪽은 설정 파일 목록, 오른쪽은 고른 파일의 그리드입니다.
     * @details 목록은 활성 팩의 `<Engine>` 요소(맨 앞)와 설정 테이블이 파일에서 읽은 설정에서 만든다(`ProjectSettingsEntry`). 저장은 Save(또는 Ctrl+S)로 하고, 기본값과 같은 값은 파일에서 지운다.
     *          저장하면 엔진의 `ConfigReloadSubsystem::reloadChangedFile` 이 호스트 설정을 제자리에서 다시 읽는다(`ConfigManager::reloadConfigFile`).
     *          창 시작 값만 쓰는 필드(창 크기 · 백엔드)는 다음 실행부터 반영된다.
     */
    class ProjectSettingsPanel final : public EditorStateDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "projectsettings";

        ProjectSettingsPanel();
        ~ProjectSettingsPanel() override;

        const utf8*      getPanelTitle() const override { return "Project Settings"; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 109.0f, 27.0f ); }

        /** @brief 지난 프레임 목록에 오른 설정 파일 수입니다(감시 값 `Editor.ProjectSettingsPageCount`). */
        static uint32 getPageCount();
        /** @brief 고른 파일의 현재 값에서 기본값과 다른 키 수입니다(감시 값 `Editor.ProjectSettingsKeyCount`). 고른 파일이 없으면 0 입니다. */
        static uint32 getSelectedKeyCount();
        /** @brief 저장하지 않은 편집이 있는 파일 수입니다(감시 값 `Editor.ProjectSettingsDirtyPageCount`). */
        static uint32 getDirtyPageCount();

        /** @brief 페이지 편집(형식 페이지의 그리드 · 기본값 되돌리기, 표 페이지)의 되돌리기 스택입니다 — 창에 포커스가 오면 Ctrl+Z 가 이리 간다(감사 E-13). */
        CommandStack* findPanelCommandStack() override { return &_history.getStack(); }

    protected:
        [[nodiscard]] bool saveDocument() override;
        /** @brief 고친 설정 파일들(`ProjectSettingsPage::_absolutePath`)입니다 — `saveDocument` 가 고친 페이지만 쓴다. */
        void collectFilesToSave( vector<string>& outListPath ) const override;
        void revertDocument() override;
        /** @brief 고친 설정 파일 하나의 저장 글입니다(`ProjectSettingsPage::tryMakeText`). */
        [[nodiscard]] expected<string> tryMakeFileText( string_view absolutePath ) const override;
        [[nodiscard]] bool             isEditingFile( string_view absolutePath ) override;
        /** @brief 설정 파일 하나의 사본 글을 그 페이지에 입히고 고릅니다. 되돌리기 단계는 버립니다. */
        [[nodiscard]] bool applyFileText( string_view absolutePath, const string& text ) override;

    private:
        using Page = ProjectSettingsPage;

    private:
        void reloadCatalog();
        void onPropertyEdited( const PropertyInfo& prop );
        void refreshDirtyState();
        /** @brief 파일 @p absolutePath 의 페이지입니다. 목록에 없으면 nullptr 입니다. */
        Page* findPage( string_view absolutePath ) const;
        void  drawPageList();
        void  drawPageDetail( Page& page );
        void  drawPageActions( Page& page );
        void  selectPage( Page& page );
        /** @brief 되돌리기 단계가 @p page 의 문서를 움직인 뒤 초안 · 미저장 표시를 맞춥니다(`ProjectSettingsHistory`). */
        void onUndoMoved( Page& page );
        /** @brief 기록을 비우고 고른 페이지를 다시 기록 대상으로 둡니다(파일로 되돌린 뒤 — 단계가 페이지별로 섞여 있어 그 페이지 것만 뺄 수 없다). */
        void resetHistory();

    private:
        vector<unique_ptr<Page>> _listPage;
        EditorPropertyGrid       _grid;
        ProjectSettingsTableView _tableView;
        Page*                    _pSelected;
        string                   _statusText;
        ProjectSettingsHistory   _history; ///< 페이지 편집 단계(`findPanelCommandStack`)
        uint8                    _bCatalogLoaded : 1;
    };
} // namespace sw::editor
