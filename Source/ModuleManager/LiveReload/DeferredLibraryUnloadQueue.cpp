#include "pch.h"

#include "ModuleManager/LiveReload/DeferredLibraryUnloadQueue.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "ModuleManager/LiveReload/ModuleShadowCopy.h"

namespace sw
{
    SW_LOG_CALLER( "LiveReloadManager" );

    DeferredLibraryUnloadQueue::DeferredLibraryUnloadQueue( uint32 maxBatchCount )
        : _listLibrary{}
        , _batchID{ 0 }
        , _maxBatchCount{ maxBatchCount }
    {
    }

    void DeferredLibraryUnloadQueue::beginBatch()
    {
        ++_batchID;
    }

    void DeferredLibraryUnloadQueue::deferUnload( string_view moduleName, void* pHandle, string_view shadowPath, bool bKeepMapped )
    {
        if ( pHandle == nullptr )
            return;

        DeferredLibrary deferredLibrary{};
        deferredLibrary._moduleName  = string{ moduleName };
        deferredLibrary._shadowPath  = string{ shadowPath };
        deferredLibrary._pHandle     = pHandle;
        deferredLibrary._batchID     = _batchID;
        deferredLibrary._bKeepMapped = bKeepMapped;
        _listLibrary.push_back( std::move( deferredLibrary ) );

        // 목록은 배치 순서이고 배치 번호는 연쇄마다 하나씩 오른다. 가장 오래된 것이 마지막 N 번의 연쇄 밖이면 내린다.
        while ( _listLibrary.back()._batchID - _listLibrary.front()._batchID >= _maxBatchCount )
        {
            unloadOldestBatch();
        }
    }

    void DeferredLibraryUnloadQueue::unloadOldestBatch()
    {
        if ( _listLibrary.empty() )
            return;

        const uint32 oldestBatchID = _listLibrary.front()._batchID;
        size_t       batchEnd{ 0 };
        while ( batchEnd < _listLibrary.size() && _listLibrary[batchEnd]._batchID == oldestBatchID )
        {
            ++batchEnd;
        }

        for ( size_t libraryIndex = batchEnd; libraryIndex > 0; --libraryIndex )
        {
            const DeferredLibrary& deferredLibrary = _listLibrary[libraryIndex - 1];
            if ( deferredLibrary._bKeepMapped )
            {
                // 다른 코드가 아직 구독하는 이벤트 채널을 이 라이브러리가 만들었다(`ModuleLibraryUtil::releaseModuleCode`). 언로드하지도, 파일을 지우지도 않는다.
                SW_LOG_INFO( "Keeping deferred module image %# mapped (batch %#) — an event channel it created is still subscribed", deferredLibrary._moduleName,
                             deferredLibrary._batchID );
                continue;
            }
            SW_LOG_INFO( "Unloading deferred module image %# (batch %#, handle=%#)", deferredLibrary._moduleName, deferredLibrary._batchID, deferredLibrary._pHandle );
            ModuleLibraryUtil::unloadDynamicLibrary( deferredLibrary._pHandle );
            ModuleShadowCopy::removeArtifacts( deferredLibrary._shadowPath );
        }
        _listLibrary.erase( _listLibrary.begin(), _listLibrary.begin() + static_cast<std::ptrdiff_t>( batchEnd ) );
    }

    void DeferredLibraryUnloadQueue::unloadAll()
    {
        while ( _listLibrary.empty() == false )
        {
            unloadOldestBatch();
        }
        // 다 내렸으면 목록의 용량도 돌려준다(`erase` 는 용량을 남긴다). 이 큐를 든 관리자는 엔진의 종료 누수 보고 뒤에 사라진다.
        _listLibrary = vector<DeferredLibrary>{};
    }
} // namespace sw
