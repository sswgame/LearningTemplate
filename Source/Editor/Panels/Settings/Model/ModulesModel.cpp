#include "pch.h"

#include "Editor/Panels/Settings/Model/ModulesModel.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Editor/Common/Commands/Diagnostics/EditorModuleOverrides.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    namespace
    {
        struct ModulesModelInternal
        {
            /**
             * @brief 돌고 있는 게임의 프로젝트 매니페스트 소스 경로입니다(게임 이름이 `Source/Games/` 의 폴더 이름이다). 프로젝트 루트 아래로 푼다.
             * @return 게임 이름이 경로로 풀리지 않으면(빈 이름 · 드라이브 글자) 빈 값입니다.
             */
            static std::optional<AbsolutePath> getProjectManifestPath()
            {
                const string gameName = GameSelection::getName();
                if ( gameName.empty() )
                    return std::nullopt;
                return AbsolutePath::tryResolve( "Source/Games/" + gameName + "/" + config::kTargetGameModule + ModuleCatalog::kManifestExtension, ProcessRoots::get()._project );
            }

            /** @brief 이 실행이 모듈을 올린 조건입니다(`ModuleCatalogLoader` 와 같다). 기본값(Windows · 양쪽 대상)으로 풀면 다른 플랫폼에서 창이 실제와 다른 켜짐을 보인다. */
            static ModuleResolveContext makeResolveContext()
            {
                ModuleResolveContext context{};
                context._platform      = ModuleCatalog::getCurrentPlatform();
                context._configuration = ModuleCatalog::getCurrentConfiguration();
                context._projectModule = config::kTargetGameModule;
                context._targetMask    = ModuleCatalog::getBuildTargetMask();
                return context;
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
        , _bPendingEnabled{ SW_FALSE }
        , _bChangedSinceStart{ SW_FALSE }
        , _bLoaded{ SW_FALSE }
    {
    }

    void ModulesModel::ensureLoaded()
    {
        if ( _bLoaded == SW_FALSE )
            reload();
    }

    void ModulesModel::reload()
    {
        reloadFrom( ModuleLibraryUtil::getModuleDirectory() );
    }

    void ModulesModel::reloadFrom( string_view directory )
    {
        _bLoaded = SW_TRUE;
        _catalog = ModuleCatalog{};
        // 읽기에 실패하면 풀기가 돌지 않는다 — 옛 카탈로그의 해석을 남기지 않는다.
        _resolution = ModuleResolution{};
        _loadError.clear();
        const ModuleResolveContext context = ModulesModelInternal::makeResolveContext();
        if ( _catalog.loadDirectory( directory, _loadError ) == false || _catalog.resolve( context, _resolution, _loadError ) == false )
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesCatalogCouldNot", "Modules: the catalog in %# could not be resolved - %#", string( directory ).c_str(), _loadError.c_str() );
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
        if ( EditorModuleOverrideUtil::previewToggle( _catalog, ModulesModelInternal::makeResolveContext(), moduleName, bEnabled, _listNewlyInactive, _listNewlyActive, error ) == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesTurningBreaksModule", "Modules: turning %# %# breaks the module graph - %#", moduleName.c_str(), bEnabled ? "on" : "off", error.c_str() );
            _pendingModule.clear();
            return false;
        }
        return true;
    }

    void ModulesModel::applyPending()
    {
        const std::optional<AbsolutePath> manifestPath = ModulesModelInternal::getProjectManifestPath();
        if ( manifestPath.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModulesPanel.ModulesRunningGameDoes", "Modules: the running game '%#' does not name a project manifest path", GameSelection::getName().c_str() );
            _pendingModule.clear();
            return;
        }
        applyPendingTo( manifestPath->str() );
    }

    void ModulesModel::applyPendingTo( const string& path )
    {
        if ( writeProjectOverride( path ) )
            _bChangedSinceStart = SW_TRUE;
        _pendingModule.clear();
    }

    bool ModulesModel::writeProjectOverride( const string& path )
    {
        const ModuleManifest* pManifest = _catalog.findManifest( _pendingModule );
        string                text;
        string                rewritten;
        if ( pManifest == nullptr || FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesProjectManifestCould", "Modules: the project manifest %# could not be read", path.c_str() );
            return false;
        }
        if ( FileUtil::isReadOnlyFile( path ) )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesReadOnlyCheck", "Modules: %# is read-only - check it out first", path.c_str() );
            return false;
        }
        if ( EditorModuleOverrideUtil::setOverride( text, _pendingModule, _bPendingEnabled, pManifest->_bEnabledByDefault, rewritten ) == false ||
             FileUtil::writeTextFile( path, rewritten ) == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesProjectManifestCould2", "Modules: the project manifest %# could not be rewritten", path.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "ModulesPanel.ModulesTurnedBuildRestart", "Modules: %# turned %# in %# - build and restart to apply", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off", path.c_str() );
        return true;
    }

    bool ModulesModel::requestBuildAll()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>().execute( "build.compileAll", EditorCommandTarget::none() );
    }
} // namespace sw::editor
