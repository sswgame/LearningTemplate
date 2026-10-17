#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "GPUMeshVertexPool" );

    GPUMeshVertexPool::GPUMeshVertexPool()
        : _allocator{}
        , _vertexBuffer{ 0 }
        , _mapEntry{}
        , _listScratchAdded{}
        , _listScratchRetry{}
        , _listScratchRemoved{}
        , _listScratchVertex{}
        , _listScratchRegion{}
        , _updateCount{ 0 }
        , _liveVertexCount{ 0 }
        , _repackCount{ 0 }
        , _lastUploadBytes{ 0 }
    {
    }

    uint32 GPUMeshVertexPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapEntry.find( pMesh );
        return ( it != _mapEntry.end() ) ? it->second._base : kInvalidBase;
    }

    bool GPUMeshVertexPool::update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        _lastUploadBytes = 0;
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;

        // 번호 = 이 호출 수(프레임마다 한 번). N 번째에 돌려받은 구간은 N + kMaxFrameCountInFlight 번째부터 다시 나간다 — 그 사이에 N - 1 번째까지
        // 제출한 프레임이 모두 끝난다.
        ++_updateCount;
        _allocator.retireDeferred( _updateCount );
        const uint64 retireStamp = _updateCount + constant::kMaxFrameCountInFlight;

        // 1) 표시: 이번 목록의 메시를 본다. 처음 보거나 내용이 바뀐 메시는 새 구간이 필요하다.
        bool bChanged = false;
        _listScratchAdded.clear();
        _listScratchRetry.clear();
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh == nullptr || pMesh->getVertices().empty() )
                continue;
            const uint64 contentID   = pMesh->getContentID();
            const uint32 vertexCount = static_cast<uint32>( pMesh->getVertices().size() );
            const auto   it          = _mapEntry.find( pMesh );
            if ( it == _mapEntry.end() )
            {
                _mapEntry.emplace( pMesh, Entry{ contentID, _updateCount, kInvalidBase, vertexCount } );
                _listScratchAdded.push_back( pMesh );
                bChanged = true;
                continue;
            }
            Entry& entry = it->second;
            if ( entry._seenStamp == _updateCount )
                continue; // 같은 메시가 목록에 또 있다(배치 여럿)
            entry._seenStamp = _updateCount;
            if ( entry._contentID == contentID )
            {
                if ( entry._base == kInvalidBase )
                    _listScratchRetry.push_back( pMesh ); // 예산 때문에 못 들어간 메시
                continue;
            }
            // 포인터는 같고 내용이 다르다(지워진 자리에 새 메시, 또는 setVertices) — 옛 구간을 돌려주고 새 구간을 받는다.
            if ( entry._base != kInvalidBase )
            {
                _allocator.freeDeferred( entry._base, entry._count, retireStamp );
                _liveVertexCount -= entry._count;
            }
            entry._contentID = contentID;
            entry._base      = kInvalidBase;
            entry._count     = vertexCount;
            _listScratchAdded.push_back( pMesh );
            bChanged = true;
        }

        // 2) 쓸기: 이번 목록에 없는 메시의 구간을 돌려준다. 포인터는 따라가지 않는다(이미 지워졌을 수 있다).
        _listScratchRemoved.clear();
        for ( const auto& [pMesh, entry] : _mapEntry )
        {
            if ( entry._seenStamp != _updateCount )
                _listScratchRemoved.push_back( pMesh );
        }
        for ( const Mesh* pMesh : _listScratchRemoved )
        {
            const Entry& entry = _mapEntry.find( pMesh )->second;
            if ( entry._base != kInvalidBase )
            {
                _allocator.freeDeferred( entry._base, entry._count, retireStamp );
                _liveVertexCount -= entry._count;
            }
            _mapEntry.erase( pMesh );
            bChanged = true;
        }

        if ( _mapEntry.empty() )
        {
            // 풀을 끄거나(gv_vertexPool=0) 씬이 비었다 — 버퍼를 놓는다. 배치는 자기 정점 버퍼로 돌아간다.
            const bool bHadBuffer = _vertexBuffer != 0;
            release( pDevice );
            return bChanged || bHadBuffer;
        }
        if ( bChanged == false )
            return false;

        // 예산 때문에 못 들어간 메시는 집합이 바뀐 프레임에만 다시 시도한다 — 매 프레임 시도하면 넘친 씬이 매 프레임 다시 채운다.
        _listScratchAdded.insert( _listScratchAdded.end(), _listScratchRetry.begin(), _listScratchRetry.end() );

        // 3) 들어온 메시에 구간을 준다. 하나라도 자리가 없으면(용량 부족, 조각남) 새 버퍼로 다시 채운다.
        if ( _vertexBuffer == 0 )
        {
            repack( pDevice );
            return true;
        }
        _listScratchVertex.clear();
        _listScratchRegion.clear();
        for ( const Mesh* pMesh : _listScratchAdded )
        {
            Entry&       entry = _mapEntry.find( pMesh )->second;
            const uint32 base  = _allocator.allocate( entry._count );
            if ( base == SpanAllocator::kInvalidOffset )
            {
                // 이미 받은 구간들은 repack 이 처음부터 다시 나눈다.
                repack( pDevice );
                return true;
            }
            entry._base = base;
            _liveVertexCount += entry._count;
            const vector<RHIVertex>& listMeshVertex = pMesh->getVertices();
            RHIBufferCopyRegion      region{};
            region._srcOffset = static_cast<uint32>( _listScratchVertex.size() * sizeof( RHIVertex ) );
            region._dstOffset = base * static_cast<uint32>( sizeof( RHIVertex ) );
            region._sizeBytes = entry._count * static_cast<uint32>( sizeof( RHIVertex ) );
            _listScratchRegion.push_back( region );
            _listScratchVertex.insert( _listScratchVertex.end(), listMeshVertex.begin(), listMeshVertex.end() );
        }
        if ( _listScratchRegion.empty() == false )
        {
            pDevice->getResourceFactory()->updateVertexBufferRegions( _vertexBuffer, _listScratchVertex.data(), _listScratchRegion.data(),
                                                                      static_cast<uint32>( _listScratchRegion.size() ) );
            _lastUploadBytes = static_cast<uint32>( _listScratchVertex.size() * sizeof( RHIVertex ) );
        }
        return true;
    }

    void GPUMeshVertexPool::repack( IRHIDevice* pDevice )
    {
        // 새 버퍼는 아무 프레임도 읽지 않는다 — 대기 중인 반납까지 버리고 살아 있는 메시를 앞에서부터 잇는다.
        uint32 liveCount = 0;
        for ( const auto& [pMesh, entry] : _mapEntry )
        {
            liveCount += entry._count;
        }
        uint32 capacity = liveCount + liveCount / 2u;
        if ( capacity < kMinPoolVertices )
            capacity = kMinPoolVertices;
        if ( capacity > kMaxPoolVertices )
            capacity = kMaxPoolVertices;

        if ( _vertexBuffer != 0 )
            pDevice->getResourceFactory()->destroyBuffer( _vertexBuffer );
        _vertexBuffer = 0;
        _allocator.reset( capacity );
        _liveVertexCount = 0;
        ++_repackCount;

        // 목록 순서 = 맵의 밀집 순서다. 정렬하지 않는다 — 구간 위치는 그림에 드러나지 않는다.
        _listScratchVertex.clear();
        _listScratchVertex.reserve( capacity );
        bool bWarnedFull = false;
        for ( auto& [pMesh, entry] : _mapEntry )
        {
            entry._base      = _allocator.allocate( entry._count );
            const bool bFits = entry._base != SpanAllocator::kInvalidOffset;
            if ( bFits == false )
            {
                entry._base = kInvalidBase;
                if ( bWarnedFull == false )
                    SW_LOG_WARNING( "정점 풀이 가득 찼습니다(%# 정점) — 남은 메시는 자기 정점 버퍼로 그립니다(멀티 드로우에 못 묶인다).", kMaxPoolVertices );
                bWarnedFull = true;
                continue;
            }
            // 맵의 메시는 모두 이번 update 에서 본 것이라 살아 있다(못 본 것은 쓸기에서 지웠다).
            const vector<RHIVertex>& listMeshVertex = pMesh->getVertices();
            _listScratchVertex.insert( _listScratchVertex.end(), listMeshVertex.begin(), listMeshVertex.end() );
            _liveVertexCount += entry._count;
        }
        if ( _liveVertexCount == 0 )
            return;
        // 꼬리는 비워 둔 용량이다 — 0 으로 채워 버퍼 전체를 한 번에 만든다(createVertexBuffer 는 내용 없이 만들 수 없다).
        _listScratchVertex.resize( capacity );
        const uint32 bytes = capacity * static_cast<uint32>( sizeof( RHIVertex ) );
        _vertexBuffer      = pDevice->getResourceFactory()->createVertexBuffer( _listScratchVertex.data(), bytes );
        _lastUploadBytes   = bytes;
        if ( _vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "정점 풀 버퍼를 만들지 못했습니다(%# 정점) — 배치는 자기 정점 버퍼로 그립니다.", capacity );
            for ( auto& [pMesh, entry] : _mapEntry )
            {
                entry._base = kInvalidBase;
            }
            _allocator.reset( 0 );
            _liveVertexCount = 0;
        }
    }

    void GPUMeshVertexPool::release( IRHIDevice* pDevice )
    {
        const bool bReleasableBuffer = _vertexBuffer != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr;
        if ( bReleasableBuffer )
            pDevice->getResourceFactory()->destroyBuffer( _vertexBuffer );
        _vertexBuffer = 0;
        _mapEntry.clear();
        _allocator.reset( 0 );
        _liveVertexCount = 0;
    }
} // namespace sw
