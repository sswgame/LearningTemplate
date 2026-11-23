#include "pch.h"

#include "DevTools/Host/HostDevTools.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ContentFolderWatchService.h"
#include "DevTools/DevToolsHost.h"
#include "DevTools/FileWatch/FileWatchService.h"

#include "Engine/Engine.h"

// 개발 구성에만 있다(`DevTools/Host/` — 배포는 폴더째 짓지 않는다). 호스트는 개발 계층(`IHostDevLayer`)으로만 닿는다.
namespace sw
{
    void HostDevTools::addToHostScope( SubsystemScope& host, Engine& engine, FileWatchService& fileWatch )
    {
        // 개발 도구 — 엔진 기동 확장(델리게이트)으로 걸어, 엔진이 기동 단계 범위를 채울 때 자기 서브시스템을 더한다(엔진은 무엇인지 모른다).
        _pDevTools = &host.emplaceSubsystem<DevToolsHost>( fileWatch );
        engine.setStartupExtension( _pDevTools->makeStartupExtension() );
        // 콘텐츠 폴더 감시 — 기능 · 게임의 데이터 다시 읽기가 범위에서 찾는다(배포에는 없다).
        const bool bFolderWatchBound = host.exposeMember<IContentFolderWatch>( *_pDevTools, _pDevTools->getContentFolderWatch() ); // 주인은 `DevToolsHost` — 함께 지워진다
        SW_ASSERT( bFolderWatchBound );
        (void)bFolderWatchBound;
        // 파일 감시 창구 — 에디터의 에셋 리로드가 찾아 건다. 주인은 호스트 개발 계층이라 개발 도구보다 오래 산다.
        const bool bFileWatchBound = host.exposeMember<FileWatchService>( *_pDevTools, fileWatch );
        SW_ASSERT( bFileWatchBound );
        (void)bFileWatchBound;
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
