/**
 * @file ModuleManager/GameModuleManagerLinkageShipping.cpp
 * @brief 배포 구성의 모듈 잇기 — 게임 · 키트 · GameFramework 는 정적 링크라 로드할 라이브러리 · 리로드 매니저 · 모듈 컴파일러가 없고, 게임 API 는 정적 `exportGameAPI` 다.
 * @details 개발은 `GameModuleManagerLinkage.cpp` 를 CMake 가 고른다(`Source/ModuleManager/CMakeLists.txt`).
 */
#include "pch.h"

#include "ModuleManager/GameModuleManager.h"

namespace sw
{
    SW_LOG_CALLER( "GameModuleManager" );

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
} // namespace sw
