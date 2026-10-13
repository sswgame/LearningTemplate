#include "pch.h"

#include "ModuleHost/DeferredImageUnloadQueue.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Module/ModuleImageUtil.h"

#include "ModuleHost/ModuleShadowCopy.h"

namespace sw
{
    SW_LOG_CALLER( "LiveReloadManager" );

    DeferredImageUnloadQueue::DeferredImageUnloadQueue( uint32 maxBatchCount )
        : _listImage{}
        , _batchID{ 0 }
        , _maxBatchCount{ maxBatchCount }
    {
    }

    void DeferredImageUnloadQueue::beginBatch()
    {
        ++_batchID;
    }

    void DeferredImageUnloadQueue::deferUnload( string_view moduleName, void* pHandle, string_view shadowPath, bool bKeepMapped )
    {
        if ( pHandle == nullptr )
            return;

        DeferredImage deferredImage{};
        deferredImage._moduleName  = string{ moduleName };
        deferredImage._shadowPath  = string{ shadowPath };
        deferredImage._pHandle     = pHandle;
        deferredImage._batchID     = _batchID;
        deferredImage._bKeepMapped = bKeepMapped;
        _listImage.push_back( std::move( deferredImage ) );

        // 목록은 배치 순서이고 배치 번호는 연쇄마다 하나씩 오른다. 가장 오래된 것이 마지막 N 번의 연쇄 밖이면 내린다.
        while ( _listImage.back()._batchID - _listImage.front()._batchID >= _maxBatchCount )
        {
            unloadOldestBatch();
        }
    }

    void DeferredImageUnloadQueue::unloadOldestBatch()
    {
        if ( _listImage.empty() )
            return;

        const uint32 oldestBatchID = _listImage.front()._batchID;
        size_t       batchEnd{ 0 };
        while ( batchEnd < _listImage.size() && _listImage[batchEnd]._batchID == oldestBatchID )
        {
            ++batchEnd;
        }

        for ( size_t imageIndex = batchEnd; imageIndex > 0; --imageIndex )
        {
            const DeferredImage& deferredImage = _listImage[imageIndex - 1];
            if ( deferredImage._bKeepMapped )
            {
                // 다른 코드가 아직 구독하는 이벤트 채널을 이 이미지가 만들었다(`ModuleImageUtil::releaseModuleCode`). 언로드하지도, 파일을 지우지도 않는다.
                SW_LOG_INFO( "Keeping deferred module image %# mapped (batch %#) — an event channel it created is still subscribed", deferredImage._moduleName,
                             deferredImage._batchID );
                continue;
            }
            SW_LOG_INFO( "Unloading deferred module image %# (batch %#, handle=%#)", deferredImage._moduleName, deferredImage._batchID, deferredImage._pHandle );
            ModuleImageUtil::unloadDynamicLibrary( deferredImage._pHandle );
            ModuleShadowCopy::removeArtifacts( deferredImage._shadowPath );
        }
        _listImage.erase( _listImage.begin(), _listImage.begin() + static_cast<std::ptrdiff_t>( batchEnd ) );
    }

    void DeferredImageUnloadQueue::unloadAll()
    {
        while ( _listImage.empty() == false )
        {
            unloadOldestBatch();
        }
        // 다 내렸으면 목록의 용량도 돌려준다(`erase` 는 용량을 남긴다). 이 큐를 든 관리자는 엔진의 종료 누수 보고 뒤에 사라진다.
        _listImage = vector<DeferredImage>{};
    }
} // namespace sw
