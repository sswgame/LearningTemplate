#include "pch.h"

#include "ModuleManager/HostDevTools.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"
#include "DevTools/DevToolsHost.h"

#include "Engine/Engine.h"

// 개발 빌드의 구현이다 — 배포는 `HostDevToolsShipping.cpp` 를 CMake 가 고른다(`Source/ModuleManager/CMakeLists.txt`).
namespace sw
{
    void HostDevTools::addToHostScope( SubsystemScope& host, Engine& engine )
    {
        // 개발 도구 — 엔진 기동 확장(델리게이트)으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(엔진은 무엇인지 모른다).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            _pDevTools = &host.addSubsystem( make_unique<DevToolsHost>() );
        }
        engine.setStartupExtension( _pDevTools->makeStartupExtension() );
        // 콘텐츠 폴더 감시 — 기능 · 게임의 데이터 다시 읽기가 범위에서 찾는다(배포에는 없다).
        const bool bFolderWatchBound = host.bind<IContentFolderWatch>( &_pDevTools->getContentFolderWatch() );
        SW_ASSERT( bFolderWatchBound );
        (void)bFolderWatchBound;
    }

    void HostDevTools::attachFrameHooks( Engine& engine )
    {
        _frameSubscription = engine.getFrameHooks().add( _pDevTools->makeFrameHooks() );
    }

    void HostDevTools::startConsole( bool bEnableEditor )
    {
        _pDevTools->startConsole( bEnableEditor );
    }

    void HostDevTools::stopConsole()
    {
        _pDevTools->stopConsole();
    }

    void HostDevTools::detach()
    {
        _frameSubscription.reset();
    }
} // namespace sw
