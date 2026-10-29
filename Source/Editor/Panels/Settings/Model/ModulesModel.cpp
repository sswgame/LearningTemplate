#include "pch.h"

#include "Editor/Panels/Settings/Model/ModulesModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Editor/Common/Commands/Dev/EditorModuleOverrides.h"
#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModulesPanel" );

    namespace
    {
        struct ModulesModelInternal
        {
            /** @brief 돌고 있는 게임의 프로젝트 매니페스트 소스 경로입니다(게임 이름이 `Source/Games/` 의 폴더 이름이다). */
            static string getProjectManifestPath()
            {
                const string gameName = GameSelection::getName();
                return EditorPaths::resolveProjectRelativePath( "Source/Games/" + gameName + "/" + config::kTargetGameModule + ModuleCatalog::kManifestExtension );
            }
        };
    } // namespace

    ModulesModel::ModulesModel()
        : _catalog{}
        , _resolution{}
        , _loadError{}
        , _selectedModule{}
        , _pendingModule{}
        , _kindFilter{ -1 }
        , _stateFilter{ ModuleStateFilter::All }
        , _listNewlyInactive{}
        , _listNewlyActive{}
        , _bPendingEnabled{ false }
        , _bChangedSinceStart{ false }
    {
    }

    void ModulesModel::ensureLoaded()
    {
        if ( _catalog.getManifestCount() == 0 && _loadError.empty() )
            reload();
    }

    void ModulesModel::reload()
    {
        _catalog = ModuleCatalog{};
        _loadError.clear();
        const string         directory = ModuleLibraryUtil::getModuleDirectory();
        ModuleResolveContext context{};
        if ( _catalog.loadDirectory( directory, _loadError ) == false || _catalog.resolve( context, _resolution, _loadError ) == false )
            SW_LOG_WARNING( "Modules: the catalog in %# could not be resolved - %#", directory.c_str(), _loadError.c_str() );
    }

    string ModulesModel::findInactiveReason( const string& name ) const
    {
        for ( const ModuleInactiveEntry& entry : _resolution._listInactive )
        {
            if ( entry._name == name )
                return entry._reason;
        }
        return {};
    }

    bool ModulesModel::isStateVisible( const string& name ) const
    {
        const bool bActive = _resolution.isActive( name );
        switch ( _stateFilter )
        {
            case ModuleStateFilter::On:
                return bActive;
            case ModuleStateFilter::Off:
                return bActive == false;
            case ModuleStateFilter::All:
            case ModuleStateFilter::Count:
                break;
        }
        return true;
    }

    bool ModulesModel::makeSelectedDetail( EditorModuleDetail& outDetail )
    {
        if ( _selectedModule.empty() )
            return false;
        if ( EditorModuleOverrideUtil::makeDetail( _catalog, string_view{ config::kTargetGameModule }, _selectedModule, outDetail ) == false )
        {
            _selectedModule.clear(); // 새로 고침으로 사라진 모듈
            return false;
        }
        return true;
    }

    const utf8* ModulesModel::getKindName( ModuleKind kind )
    {
        switch ( kind )
        {
            case ModuleKind::GameFramework:
                return "GameFramework";
            case ModuleKind::Kit:
                return "Kits";
            case ModuleKind::Game:
                return "Game";
            case ModuleKind::Editor:
                return "Editor";
            case ModuleKind::RHI:
                return "RHI backends";
            case ModuleKind::EditorExtension:
                return "Editor extensions";
        }
        return "";
    }

    bool ModulesModel::requestToggle( const string& moduleName, bool bEnabled )
    {
        _pendingModule   = moduleName;
        _bPendingEnabled = bEnabled;
        string error;
        if ( EditorModuleOverrideUtil::previewToggle( _catalog, ModuleResolveContext{}, moduleName, bEnabled, _listNewlyInactive, _listNewlyActive, error ) == false )
        {
            SW_LOG_WARNING( "Modules: turning %# %# breaks the module graph - %#", moduleName.c_str(), bEnabled ? "on" : "off", error.c_str() );
            _pendingModule.clear();
            return false;
        }
        return true;
    }

    void ModulesModel::applyPending()
    {
        if ( writeProjectOverride() )
            _bChangedSinceStart = true;
        _pendingModule.clear();
    }

    bool ModulesModel::writeProjectOverride()
    {
        const string          path      = ModulesModelInternal::getProjectManifestPath();
        const ModuleManifest* pManifest = _catalog.findManifest( _pendingModule );
        string                text;
        string                rewritten;
        if ( pManifest == nullptr || FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be read", path.c_str() );
            return false;
        }
        if ( FileUtil::isReadOnlyFile( path ) )
        {
            SW_LOG_WARNING( "Modules: %# is read-only - check it out first", path.c_str() );
            return false;
        }
        if ( EditorModuleOverrideUtil::setOverride( text, _pendingModule, _bPendingEnabled, pManifest->_bEnabledByDefault, rewritten ) == false ||
             FileUtil::writeTextFile( path, rewritten ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be rewritten", path.c_str() );
            return false;
        }
        SW_LOG_INFO( "Modules: %# turned %# in %# - build and restart to apply", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off", path.c_str() );
        return true;
    }
} // namespace sw::editor
