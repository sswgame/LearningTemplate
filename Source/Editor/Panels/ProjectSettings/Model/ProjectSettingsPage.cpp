#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    void ProjectSettingsPage::open()
    {
        _bOpened              = true;
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
            _bDirty = _document.isModified();
    }

    bool ProjectSettingsPage::saveDocument( bool& outApplied )
    {
        outApplied = false;
        if ( _document.saveDocument() == false )
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
        (void)_document.revert(); // 결과는 창의 칩과 오류 줄이 보인다
        _bDirty = false;
    }

    void ProjectSettingsPage::resetToDefault()
    {
        _document.resetToDefault();
        _bDirty = _document.isModified();
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
} // namespace sw::editor
