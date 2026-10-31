/**
 * @file ModuleManager/GameModuleManagerLinkage.cpp
 * @brief 모듈 잇기 — 개발은 게임 · 키트 · GameFramework 를 동적 라이브러리로 리로드 매니저에 올리고 게임 API 를 라이브러리 심볼에서 읽는다.
 *        배포는 정적 링크라 로드할 라이브러리 · 리로드 매니저 · 모듈 컴파일러가 없고 게임 API 는 정적 `exportGameAPI` 다.
 * @details 두 방식은 진짜 구현 둘이다(빈 몸통이 아니다 — 언리얼 `IS_MONOLITHIC`). 동적 쪽이 배포가 짓지 않는 `LiveReload/` 를 쓰므로 이 파일 안에서
 *          `SW_WITH_DYNAMIC_MODULES` 로 가른다(`docs/agents/BuildConfigSwitch.md` 둘째 줄).
 */
#include "pch.h"

#include "Core/Foundation/DynamicModules.h"
#include "Core/Memory/Memory.h"

#include "Engine/Module/ModuleCatalog.h"

#include "ModuleManager/GameModuleManager.h"

#include "sw/config/ConfigConstants.h"

#if SW_WITH_DYNAMIC_MODULES
    #include "ModuleManager/LiveReload/LiveReloadManager.h"
    #include "ModuleManager/LiveReload/ModuleCompiler.h"
    #include "ModuleManager/ModuleInstanceUtil.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "GameModuleManager" );

#if SW_WITH_DYNAMIC_MODULES

    bool GameModuleManager::loadModuleLibrarys( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _pLiveReloadManager = pLiveReloadManager;
        if ( _pLiveReloadManager == nullptr )
            return true;

        SW_MEMORY_SCOPE( Game );
        vector<string> listGameModule;

        // 로드 순서는 매니페스트의 의존이 정한다(의존이 먼저, 동점은 이름 순). 공용 모듈(GameFramework)은 그것을 링크하는 키트와 SWGame 보다 먼저
        // 로드되므로 제 이름으로 등록된다(`LiveReloadManager::loadSharedModule` 의 이유).
        for ( const string& moduleName : resolution._listLoadOrder )
        {
            const ModuleManifest* pManifest = catalog.findManifest( moduleName );
            if ( pManifest == nullptr )
                continue;
            if ( pManifest->_kind == ModuleKind::GameFramework )
            {
                if ( _pLiveReloadManager->loadSharedModule( moduleName ) == false )
                {
                    SW_LOG_ERROR( "Shared module load failed (%#)", moduleName );
                    return false;
                }
                listGameModule.push_back( moduleName );
                continue;
            }
            if ( pManifest->_kind != ModuleKind::Kit )
                continue;

            vector<string> listDependency;
            for ( const ModuleDependency& dependency : pManifest->_listDependency )
            {
                listDependency.push_back( dependency._name );
            }
            if ( _pLiveReloadManager->registerModule( moduleName, listDependency ) == false )
            {
                SW_LOG_ERROR( "Kit module register failed (%#)", moduleName );
                return false;
            }
            holdReloadSubscription(
                _pLiveReloadManager->setOnBeforeReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &GameModuleManager::onBeforeGameplayDLLReload, this ) ) );
            holdReloadSubscription(
                _pLiveReloadManager->setOnAfterReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &GameModuleManager::onAfterGameplayDLLReload, this ) ) );
            listGameModule.push_back( moduleName );
        }

        // 게임 모듈은 라이브러리만 로드한다(타입 등록까지). 리로드 직후 콜백인 인스턴스 생성은 기동(`initializeGame`)이 등록하고 호출한다.
        holdReloadSubscription( _pLiveReloadManager->setOnBeforeReload(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &GameModuleManager::onBeforeGameReload, this ) ) );
        holdReloadSubscription( _pLiveReloadManager->setOnReloadFault(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &GameModuleManager::onGameReloadFault, this ) ) );
        holdReloadSubscription( _pLiveReloadManager->setOnValidateLibrary(
            sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateLibraryDelegate, &GameModuleManager::isGameLibraryUsable, this ) ) );
        _listGameScopeModule = listGameModule; // SWGame 은 이름으로 따로 본다(onBeforeCommitBatch)
        if ( _pLiveReloadManager->registerModule( sw::config::kTargetGameModule, listGameModule ) == false )
        {
            SW_LOG_ERROR( "SWGame module register failed" );
            return false;
        }

        if ( _pLiveReloadManager->isGraphBroken() )
        {
            SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
            return false;
        }

        holdReloadSubscription(
            _pLiveReloadManager->addOnBeforeCommitBatch( SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &GameModuleManager::onBeforeCommitBatch, this ) ) );
        return true;
    }

    bool GameModuleManager::startGameInstance()
    {
        _moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
        if ( _pLiveReloadManager == nullptr )
            return true;

        // 매니저가 비우기를 부르는 것은 모듈을 내리기 직전(리로드)뿐이다 — 종료는 `shutdown` 이 이 콜백을 놓은 뒤 매니저를 내린다.
        holdReloadSubscription( _pLiveReloadManager->setDrainWorkers( SW_DELEGATE_LAMBDA( LiveReloadManager::DrainWorkersDelegate, [this]()
        { drainRenderWorkers( DrainReason::Reload ); } ) ) );

        BLOCK( "SWGame 인스턴스 생성" )
        {
            SW_MEMORY_SCOPE( Game );
            holdReloadSubscription( _pLiveReloadManager->setOnAfterReload(
                sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &GameModuleManager::onAfterGameReload, this ) ) );
            if ( _pLiveReloadManager->runAfterReload( sw::config::kTargetGameModule ) == false )
            {
                SW_LOG_ERROR( "SWGame instance could not be started" );
                return false;
            }
        }
        return true;
    }

    void GameModuleManager::markReloadGraphBroken( const utf8* pReason )
    {
        if ( _pLiveReloadManager != nullptr )
            _pLiveReloadManager->markGraphBroken( pReason != nullptr ? pReason : "unspecified" );
    }

    void* GameModuleManager::getLoadedModuleHandle( string_view moduleName ) const
    {
        if ( _pLiveReloadManager == nullptr )
            return nullptr;
        return _pLiveReloadManager->getModuleHandle( moduleName );
    }

    bool GameModuleManager::exportGameAPITable( void* pLibraryModule )
    {
        // 동적 경로라 새 라이브러리가 이 호스트와 다른 테이블로 빌드됐을 수 있다 — ABI 를 대조한다(배포는 정적 링크라 어긋날 수 없다).
        return ModuleInstanceUtil::exportAPIFromLibrary<GameAPI, PFN_ExportGameAPI>( pLibraryModule, getModuleSymbols(), _gameAPI );
    }
#else

    bool GameModuleManager::loadModuleLibrarys( LiveReloadManager* pLiveReloadManager, [[maybe_unused]] const ModuleCatalog& catalog,
                                                [[maybe_unused]] const ModuleResolution& resolution )
    {
        // 그 타입은 리플렉션 단계가 이미 모았고, 무엇을 링크할지는 CMake 가 같은 매니페스트로 정했다.
        _pLiveReloadManager = pLiveReloadManager;
        return true;
    }

    bool GameModuleManager::startGameInstance()
    {
        onAfterGameReload( nullptr );
        return true;
    }

    void GameModuleManager::markReloadGraphBroken( [[maybe_unused]] const utf8* pReason )
    {
    }

    void* GameModuleManager::getLoadedModuleHandle( [[maybe_unused]] string_view moduleName ) const
    {
        // 정적 링크라 "로드된 모듈"이라는 것이 없다. 부르는 쪽이 nullptr 을 처리한다.
        return nullptr;
    }

    bool GameModuleManager::exportGameAPITable( [[maybe_unused]] void* pLibraryModule )
    {
        if ( exportGameAPI( &_gameAPI ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
            return false;
        }
        return true;
    }
#endif
} // namespace sw
