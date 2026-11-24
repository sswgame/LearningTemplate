#include "pch.h"

#include "Editor/Panels/Settings/Model/ModulesViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/Diagnostics/EditorModuleOverrides.h"
#include "Editor/Common/Commands/EditorCommandIDs.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    namespace
    {
        struct ModulesViewModelInternal
        {
            /**
             * @brief 돌고 있는 게임의 프로젝트 매니페스트 소스 경로입니다(게임 이름이 `Source/Games/` 의 폴더 이름이다). 프로젝트 루트 아래로 푼다.
             * @return 게임 이름이 경로로 풀리지 않으면(빈 이름 · 드라이브 글자) 빈 값입니다.
             */
            static optional<AbsolutePath> getProjectManifestPath()
            {
                const string gameName = GameSelection::getName();
                if ( gameName.empty() )
                    return nullopt;
                fixed_string<constant::buffer::kMax512> manifestPath;
                formatstring( manifestPath.data(), manifestPath.capacity(), "Source/Games/%#/%#%#", gameName, config::kTargetGameModule, ModuleCatalog::kManifestExtension );
                return AbsolutePath::tryResolve( manifestPath, ProcessRoots::get()._project );
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

            /** @brief 폴더 @p directory 의 카탈로그를 읽고 해석합니다. 실패하면 경고를 남기고 해석을 비웁니다. 워커에서도 부른다(읽는 것은 인자와 빌드 상수뿐이다). */
            static ModuleCatalogSnapshot loadSnapshot( string_view directory )
            {
                ModuleCatalogSnapshot      snapshot;
                const ModuleResolveContext context = makeResolveContext();
                expected<void>             loaded  = snapshot._catalog.loadDirectory( FileUtil::toDiskPath( directory ) );
                if ( loaded.has_value() )
                    loaded = snapshot._catalog.resolve( context, snapshot._resolution );
                if ( loaded.has_value() )
                    return snapshot;
                snapshot._resolution = ModuleResolution{};
                StringBuilder<constant::buffer::kMax512> errorBuilder;
                errorBuilder.appendFormat( "%# (%#)", loaded.error().message(), loaded.error().detail() );
                snapshot._loadError = string( errorBuilder.view() );
                SW_LOG_WARNING_ID( "ModulesPanel.ModulesCatalogCouldNot", "Modules: the catalog in %# could not be resolved - %#", string( directory ).c_str(), snapshot._loadError.c_str() );
                return snapshot;
            }
        };
    } // namespace

    void ModuleCatalogLoadJob::request( string_view directory )
    {
        const uint32 generation = beginRequest( string( directory ) );
        submitJob( "ModuleCatalogLoad", SW_DELEGATE_FUNCTION( TaskArgsDelegate, ModuleCatalogLoadJob::runJob ), MakeTaskArgs( _pState, generation ) );
    }

    void ModuleCatalogLoadJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        string                  directory;
        if ( readInput( pState, generation, directory ) == false || isStopRaised( readStopSignal( pState ) ) )
            return;
        publish( pState, generation, ModulesViewModelInternal::loadSnapshot( directory ) );
    }

    ModulesViewModel::ModulesViewModel()
        : _loadJob{}
        , _catalog{}
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

    void ModulesViewModel::requestReload()
    {
        _loadJob.request( ModuleLibraryUtil::getModuleDirectory().str() );
    }

    void ModulesViewModel::pollReload()
    {
        ModuleCatalogSnapshot snapshot;
        if ( _loadJob.take( snapshot ) == EditorTaskPoll::Ready )
            applySnapshot( std::move( snapshot ) );
    }

    void ModulesViewModel::reloadFrom( string_view directory )
    {
        applySnapshot( ModulesViewModelInternal::loadSnapshot( directory ) );
    }

    void ModulesViewModel::applySnapshot( ModuleCatalogSnapshot&& snapshot )
    {
        _catalog    = std::move( snapshot._catalog );
        _resolution = std::move( snapshot._resolution );
        _loadError  = std::move( snapshot._loadError );
        _bLoaded    = SW_TRUE;
    }

    string ModulesViewModel::findInactiveReason( const string& name ) const
    {
        for ( const ModuleInactiveEntry& entry : _resolution._listInactive )
        {
            if ( entry._name == name )
                return entry._reason;
        }
        return {};
    }

    bool ModulesViewModel::isStateVisible( const string& name ) const
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

    bool ModulesViewModel::makeSelectedDetail( EditorModuleDetail& outDetail )
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

    const utf8* ModulesViewModel::getKindName( ModuleKind kind )
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

    bool ModulesViewModel::requestToggle( const string& moduleName, bool bEnabled )
    {
        _pendingModule               = moduleName;
        _bPendingEnabled             = bEnabled;
        const expected<void> preview = EditorModuleOverrideUtil::previewToggle( _catalog, ModulesViewModelInternal::makeResolveContext(), moduleName, bEnabled, _listNewlyInactive, _listNewlyActive );
        if ( preview.has_value() == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesTurningBreaksModule", "Modules: turning %# %# breaks the module graph - %#: %#", moduleName.c_str(), bEnabled ? "on" : "off", preview.error().message(),
                               preview.error().detail() );
            _pendingModule.clear();
            return false;
        }
        return true;
    }

    void ModulesViewModel::applyPending()
    {
        const optional<AbsolutePath> manifestPath = ModulesViewModelInternal::getProjectManifestPath();
        if ( manifestPath.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModulesPanel.ModulesRunningGameDoes", "Modules: the running game '%#' does not name a project manifest path", GameSelection::getName().c_str() );
            _pendingModule.clear();
            return;
        }
        applyPendingTo( manifestPath->str() );
    }

    void ModulesViewModel::applyPendingTo( const string& path )
    {
        if ( writeProjectOverride( path ) )
            _bChangedSinceStart = SW_TRUE;
        _pendingModule.clear();
    }

    bool ModulesViewModel::writeProjectOverride( const string& path )
    {
        const ModuleManifest* pManifest = _catalog.findManifest( _pendingModule );
        string                text;
        string                rewritten;
        if ( pManifest == nullptr || FileUtil::readTextFile( FileUtil::toDiskPath( path ), text ) == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesProjectManifestCould", "Modules: the project manifest %# could not be read", path.c_str() );
            return false;
        }
        if ( FileUtil::isReadOnlyFile( FileUtil::toDiskPath( path ) ) )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesReadOnlyCheck", "Modules: %# is read-only - check it out first", path.c_str() );
            return false;
        }
        if ( EditorModuleOverrideUtil::setOverride( text, _pendingModule, _bPendingEnabled, pManifest->_bEnabledByDefault, rewritten ) == false ||
             FileUtil::writeTextFile( FileUtil::toDiskPath( path ), rewritten ) == false )
        {
            SW_LOG_WARNING_ID( "ModulesPanel.ModulesProjectManifestCould2", "Modules: the project manifest %# could not be rewritten", path.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "ModulesPanel.ModulesTurnedBuildRestart", "Modules: %# turned %# in %# - build and restart to apply", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off", path.c_str() );
        return true;
    }

    bool ModulesViewModel::requestBuildAll()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>().execute( editorcommand::kBuildCompileAll, EditorCommandTarget::none() );
    }
} // namespace sw::editor
