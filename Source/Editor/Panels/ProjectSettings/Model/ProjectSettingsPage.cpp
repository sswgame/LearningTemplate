#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/LogOnce.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    void ProjectSettingsPage::open()
    {
        _bOpened = true;
        _bDirty  = false;
        if ( _entry.isTable() )
        {
            const expected<void> opened = _tableDocument.open( _absolutePath );
            _bTypeFound                 = opened.has_value();
            if ( opened.has_value() == false )
                SW_LOG_WARNING_ID( "ProjectSettingsPanel.ProjectSettingsTableCouldNot", "Project settings: %# could not be opened - %# (%#)", _absolutePath.c_str(), opened.error().message(),
                                   opened.error().detail() );
            return;
        }
        const TypeInfo* pType = editor::getSubsystem<TypeRegistry>().findType( hashed_string( _entry._typeName.c_str() ) );
        if ( pType == nullptr )
        {
            _bTypeFound = false;
            return;
        }
        if ( _document.open( *pType, _absolutePath, _entry._elementName ) == ConfigReadResult::Invalid )
            SW_LOG_WARNING_ID( "ProjectSettingsPanel.ProjectSettingsCouldNot", "Project settings: %# could not be opened - see the errors above", _absolutePath.c_str() );
        _bDirty = false;
    }

    void ProjectSettingsPage::refreshDirty()
    {
        if ( _bOpened )
            _bDirty = _entry.isTable() ? _tableDocument.isModified() : _document.isModified();
    }

    bool ProjectSettingsPage::saveDocument( bool& outApplied )
    {
        outApplied        = false;
        const bool bSaved = _entry.isTable() ? _tableDocument.saveDocument() : _document.saveDocument();
        if ( bSaved == false )
            return false;
        _bDirty = false;
        // 호스트가 파일에서 읽은 설정(EngineConfig · GameConfig …)이면 제자리에서 다시 읽는다.
        // 방금 쓴 파일을 다시 읽지 못하면(Failed) 읽는 쪽이 오류를 남겼다 — 적용되지 않은 것이다.
        ConfigReloadSubsystem* pConfigReload = editor::findSubsystem<ConfigReloadSubsystem>();
        outApplied                           = pConfigReload != nullptr && pConfigReload->reloadChangedFile( _absolutePath ) == ReloadResult::Reloaded;
        return true;
    }

    void ProjectSettingsPage::revert()
    {
        if ( _entry.isTable() )
        {
            const expected<void> reverted = _tableDocument.revert();
            _bTypeFound                   = reverted.has_value(); // 디스크에서 깨졌으면 목록에서 고를 수 없게 한다 — 이유는 open 이 로그로 남긴다
        }
        else
        {
            (void)_document.revert(); // 결과는 창의 칩과 오류 줄이 보인다
        }
        _bDirty = false;
    }

    void ProjectSettingsPage::resetToDefault()
    {
        if ( _entry.isTable() )
            return; // 형식이 없어 기본값을 모른다
        _document.resetToDefault();
        _bDirty = _document.isModified();
    }

    uint32 ProjectSettingsPage::countKeys() const
    {
        if ( _bOpened == false )
            return 0;
        return _entry.isTable() ? _tableDocument.countLeaves() : _document.countNonDefaultKeys();
    }

    bool ProjectSettingsPage::hasDirty( const vector<unique_ptr<ProjectSettingsPage>>& listPage )
    {
        for ( const unique_ptr<ProjectSettingsPage>& pPage : listPage )
        {
            if ( pPage->_bDirty )
                return true;
        }
        return false;
    }

    void ProjectSettingsPage::collectPages( vector<unique_ptr<ProjectSettingsPage>>& outListPage )
    {
        outListPage.clear();
        const TypeRegistry& registry = editor::getSubsystem<TypeRegistry>();

        // 활성 팩의 `<Engine>` 요소(창 제목 · 렌더 파이프라인 · 스키마 · UI 규칙) — 게임의 엔진 설정은 프리셋이 아니라 팩 gamesettings.xml 에 있다.
        // 유니티 · 언리얼의 Project Settings 처럼 열린 프로젝트(활성 게임) 하나만 고친다.
        const DataPath& packRoot   = editor::getSubsystem<const GameConfig>()._packRoot;
        const string    packFolder = packRoot.isSet() ? EditorPaths::findContentFolder( packRoot.get() ) : string{};
        // 팩 폴더를 못 찾았는데 빈 루트에 이어 붙이면 상대 경로가 돼 작업 폴더의 파일을 열고 쓴다 — 페이지를 만들지 않고 알린다.
        if ( packRoot.isSet() && packFolder.empty() )
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.ProjectSettingsActivePack", "Project settings: the active pack folder '%#' was not found - its <Engine> settings page is not listed", packRoot.get().c_str() );
        if ( packFolder.empty() == false )
        {
            unique_ptr<ProjectSettingsPage> pPackPage = make_unique<ProjectSettingsPage>();
            pPackPage->_entry._page                   = string( PackEngineSettings::StaticType()->_name.c_str() );
            pPackPage->_entry._typeName               = pPackPage->_entry._page;
            pPackPage->_entry._elementName            = PackEngineSettings::kElementName;
            pPackPage->_entry._relativePath           = FileUtil::joinPath( FileUtil::joinPath( constant::path::kContentFolder, packRoot.get() ), PackEngineSettings::kPackRelativePath );
            pPackPage->_entry._absolutePath           = FileUtil::joinPath( packFolder, PackEngineSettings::kPackRelativePath );
            pPackPage->_absolutePath                  = pPackPage->_entry._absolutePath;
            pPackPage->_bTypeFound                    = registry.findType( PackEngineSettings::StaticType()->_name ) != nullptr;
            outListPage.push_back( std::move( pPackPage ) );
        }

        const ConfigManager* const pConfigManager = editor::findSubsystem<ConfigManager>();
        if ( pConfigManager == nullptr )
        {
            SW_LOG_ERROR_ID( "ProjectSettingsPanel.ProjectSettingsEditorServices", "Project settings: the editor services have no ConfigManager - there is nothing to list" );
            return;
        }
        vector<ProjectSettingsEntry> listEntry;
        ProjectSettingsEntry::collectLoaded( *pConfigManager, ProcessRoots::get()._project.str(), listEntry );
        vector<ProjectSettingsEntry> listTable;
        (void)ProjectSettingsEntry::collectTables( ProcessRoots::get()._project.str(), listEntry, listTable ); // 못 훑으면 오류를 남겼다 — 형식 페이지는 그대로 보인다
        for ( ProjectSettingsEntry& table : listTable )
            listEntry.push_back( std::move( table ) );
        for ( ProjectSettingsEntry& entry : listEntry )
        {
            unique_ptr<ProjectSettingsPage> pPage = make_unique<ProjectSettingsPage>();
            pPage->_entry                         = std::move( entry );
            pPage->_absolutePath                  = pPage->_entry._absolutePath;
            pPage->_bTypeFound                    = pPage->_entry.isTable() || registry.findType( hashed_string( pPage->_entry._typeName.c_str() ) ) != nullptr;
            outListPage.push_back( std::move( pPage ) );
        }
    }

    ProjectSettingsWatchValues& ProjectSettingsPage::getWatchValues()
    {
        return getEditorState<ProjectSettingsWatchValues>();
    }
} // namespace sw::editor
