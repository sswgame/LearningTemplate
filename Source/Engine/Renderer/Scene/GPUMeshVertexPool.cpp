#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

namespace sw
{
    GPUMeshVertexPool::GPUMeshVertexPool( RHIMeshVertexFormat format )
        : _format{ format }
        , _allocator{}
        , _indexAllocator{}
        , _vertexBuffer{ 0 }
        , _indexBuffer{ 0 }
        , _mapEntry{}
        , _listScratchAdded{}
        , _listScratchRetry{}
        , _listScratchRemoved{}
        , _listScratchByte{}
        , _listScratchIndex{}
        , _listScratchRegion{}
        , _listScratchIndexRegion{}
        , _updateCount{ 0 }
        , _gpuFence{}
        , _liveVertexCount{ 0 }
        , _listPendingCopy{}
        , _listRetiredBuffer{}
    {
    }

    bool GPUMeshVertexPool::queueMeshFill( IRHIDevice& device, const Mesh& mesh, uint32 firstVertex, uint32 vertexCapacity )
    {
        const uint32          count           = mesh.getDrawVertexCount();
        const uint32          attributeStride = constant::render::getMeshAttributeStride( _format );
        const uint64          dstPosition     = static_cast<uint64>( firstVertex ) * constant::render::kMeshPositionStride;
        const uint64          dstAttribute    = RHIMeshVertexStreams::computeAttributeOffset( vertexCapacity ) + static_cast<uint64>( firstVertex ) * attributeStride;
        const MeshRenderData& proxy           = mesh.getRenderData();
        if ( proxy.isRHIValid() )
        {
            // GPU 원본 — 렌더 프록시 버퍼는 [위치 구간(그릴 정점 수 칸)][속성 구간] 이다(`Mesh::getAttributeStreamOffset`). CPU 정점을 읽지 않는다(R-15).
            const RHIBufferHandle source = proxy.getVertexBuffer();
            _listPendingCopy.push_back( PendingCopy{ _vertexBuffer, dstPosition, source, 0, static_cast<uint64>( count ) * constant::render::kMeshPositionStride } );
            _listPendingCopy.push_back( PendingCopy{ _vertexBuffer, dstAttribute, source, mesh.getAttributeStreamOffset(), static_cast<uint64>( count ) * attributeStride } );
            return true;
        }
        // 프록시가 없는 동적 메시(프레임마다 새 메시 — 천) — CPU 정점을 쥔 메시만 여기 온다(절차 메시는 CPU 정점을 놓지 않는다). GPU 전용 버퍼라 스테이징으로 올린다.
        const MeshCPUView cpu = mesh.getCPUView();
        if ( cpu.isAvailable() == false || cpu.getVertices().empty() )
            return false;
        _listScratchByte.resize( RHIMeshVertexStreams::computeBufferBytes( _format, count ) );
        writeDrawVertices( mesh, 0, count );
        const RHIBufferCopyRegion arrRegion[2] = {
            RHIBufferCopyRegion{                                                    0,  static_cast<uint32>( dstPosition ), count * constant::render::kMeshPositionStride},
            RHIBufferCopyRegion{RHIMeshVertexStreams::computeAttributeOffset( count ), static_cast<uint32>( dstAttribute ),                       count * attributeStride},
        };
        device.getResourceFactory()->updateStructuredBufferRegions( _vertexBuffer, _listScratchByte.data(), arrRegion, 2 );
        return true;
    }

    void GPUMeshVertexPool::recordPendingCopies( IRHICommandList& cmd, IRHIDevice& device )
    {
        for ( const PendingCopy& copy : _listPendingCopy )
        {
            const RHIBufferTransition arrBefore[2] = {
                RHIBufferTransition{     copy._source, RHIBufferState::CopySource},
                RHIBufferTransition{copy._destination,   RHIBufferState::CopyDest}
            };
            cmd.transitionBuffers( arrBefore, 2 );
            cmd.copyBufferRegion( copy._destination, copy._destinationOffset, copy._source, copy._sourceOffset, copy._sizeBytes );
            const RHIBufferTransition arrAfter[2] = {
                RHIBufferTransition{     copy._source, RHIBufferState::VertexOrConstant},
                RHIBufferTransition{copy._destination, RHIBufferState::VertexOrConstant}
            };
            cmd.transitionBuffers( arrAfter, 2 );
        }
        _listPendingCopy.clear();
        // 옛 풀을 읽는 복사를 방금 기록했다 — 이 프레임이 끝난 뒤에 놓는다(`releaseHandle` 이 이미 만든 패킷 뒤로 미룬다).
        for ( const RHIBufferHandle retired : _listRetiredBuffer )
        {
            device.releaseHandle( retired );
        }
        _listRetiredBuffer.clear();
    }

    uint32 GPUMeshVertexPool::getAttributeOffset() const
    {
        return RHIMeshVertexStreams::computeAttributeOffset( _allocator.getCapacity() );
    }

    void GPUMeshVertexPool::writeDrawVertices( const Mesh& mesh, uint32 firstVertex, uint32 vertexCapacity )
    {
        const uint32 lod0Count = static_cast<uint32>( mesh.getCPUView().getVertices().size() );
        RHIMeshVertexStreams::write( _format, mesh.getCPUView().getVertices().data(), lod0Count, firstVertex, vertexCapacity, _listScratchByte.data() );
        RHIMeshVertexStreams::write( _format, mesh.getCPUView().getLODVertices().data(), static_cast<uint32>( mesh.getCPUView().getLODVertices().size() ), firstVertex + lod0Count, vertexCapacity,
                                     _listScratchByte.data() );
    }

    GPUMeshVertexPool::Placement GPUMeshVertexPool::getPlacementOf( const Mesh* pMesh ) const
    {
        if ( pMesh == nullptr )
            return Placement{};
        const auto it = _mapEntry.find( pMesh->getContentID() );
        if ( it == _mapEntry.end() || it->second._base == kInvalidBase )
            return Placement{};
        return Placement{ it->second._base, it->second._indexBase };
    }

    bool GPUMeshVertexPool::update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;

        // 돌려받은 구간의 키는 펜스 값이다 — 이번에 나간 메시를 읽던 앞 프레임은 모두 이 펜스 앞에 냈다. GPU 가 그 값을 끝냈다고 알린 구간만 다시 나간다.
        ++_updateCount;
        const uint64 retireStamp = _gpuFence.signal( *pDevice );
        _allocator.retireDeferred( _gpuFence.getCompletedValue() );
        _indexAllocator.retireDeferred( _gpuFence.getCompletedValue() );

        const bool bMarkedChange = markListedMeshes( listMesh );
        const bool bChanged      = sweepUnlistedMeshes( retireStamp ) || bMarkedChange;
        if ( _mapEntry.empty() )
        {
            // 풀을 끄거나(gv_vertexPool=false) 씬이 비었다 — 버퍼를 놓는다. 배치는 자기 정점 · 인덱스 버퍼로 돌아간다.
            const bool bHadBuffer = _vertexBuffer != 0;
            release( pDevice );
            return bChanged || bHadBuffer;
        }
        if ( bChanged == false )
            return false;

        // 예산 때문에 못 들어간 메시는 집합이 바뀐 프레임에만 다시 시도한다 — 매 프레임 시도하면 넘친 씬이 매 프레임 다시 채운다.
        _listScratchAdded.insert( _listScratchAdded.end(), _listScratchRetry.begin(), _listScratchRetry.end() );

        // 3) 들어온 메시에 구간을 준다. 하나라도 자리가 없으면(용량 부족, 조각남) 새 버퍼로 다시 채운다.
        if ( _vertexBuffer == 0 || placeAddedMeshes( pDevice ) == false )
            repack( pDevice );
        return true;
    }

    void GPUMeshVertexPool::retireEntry( Entry& inoutEntry, uint64 retireStamp )
    {
        if ( inoutEntry._base == kInvalidBase )
            return;
        _allocator.freeDeferred( inoutEntry._base, inoutEntry._count, retireStamp );
        _indexAllocator.freeDeferred( inoutEntry._indexBase, inoutEntry._indexCount, retireStamp );
        _liveVertexCount -= inoutEntry._count;
        inoutEntry._base      = kInvalidBase;
        inoutEntry._indexBase = kInvalidBase;
    }

    bool GPUMeshVertexPool::markListedMeshes( const vector<Mesh*>& listMesh )
    {
        // 1) 표시: 이번 목록의 메시를 본다. 처음 보는 내용 토큰은 새 구간이 필요하다 — 지워진 자리의 새 메시 · setGeometry 는 새 토큰이라 여기로 오고,
        //    옛 토큰의 항목은 쓸기가 돌려준다(키가 주소가 아니라 비교할 것이 없다).
        bool bChanged = false;
        _listScratchAdded.clear();
        _listScratchRetry.clear();
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh == nullptr || pMesh->getIndices().empty() || pMesh->getVertexFormat() != _format )
                continue; // 다른 형식은 그 형식의 풀이 담는다
            const IdentityToken& contentID = pMesh->getContentID();
            const auto           it        = _mapEntry.find( contentID );
            if ( it == _mapEntry.end() )
            {
                // 정점 수는 LOD 0 뒤에 낮은 단계까지다(배치는 단계의 범위로 그린다).
                _mapEntry.emplace( contentID, Entry{ pMesh, _updateCount, kInvalidBase, pMesh->getDrawVertexCount(), kInvalidBase, pMesh->getDrawIndexCount() } );
                _listScratchAdded.push_back( contentID );
                bChanged = true;
                continue;
            }
            Entry& entry = it->second;
            if ( entry._seenStamp == _updateCount )
                continue; // 같은 메시가 목록에 또 있다(배치 여럿)
            entry._seenStamp = _updateCount;
            entry._pMesh     = pMesh;
            if ( entry._base == kInvalidBase )
                _listScratchRetry.push_back( contentID ); // 예산 때문에 못 들어간 메시
        }
        return bChanged;
    }

    bool GPUMeshVertexPool::sweepUnlistedMeshes( uint64 retireStamp )
    {
        // 2) 쓸기: 이번 목록에 없는 메시의 구간을 돌려준다. 메시는 따라가지 않는다(이미 지워졌을 수 있다).
        _listScratchRemoved.clear();
        for ( const auto& [contentID, entry] : _mapEntry )
        {
            if ( entry._seenStamp != _updateCount )
                _listScratchRemoved.push_back( contentID );
        }
        for ( const IdentityToken& contentID : _listScratchRemoved )
        {
            retireEntry( _mapEntry.find( contentID )->second, retireStamp );
            _mapEntry.erase( contentID );
        }
        return _listScratchRemoved.empty() == false;
    }

    bool GPUMeshVertexPool::placeAddedMeshes( IRHIDevice* pDevice )
    {
        _listScratchIndex.clear();
        _listScratchRegion.clear();
        _listScratchIndexRegion.clear();
        for ( const IdentityToken& contentID : _listScratchAdded )
        {
            Entry&       entry     = _mapEntry.find( contentID )->second;
            const uint32 base      = _allocator.allocate( entry._count );
            const uint32 indexBase = base != SpanAllocator::kInvalidOffset ? _indexAllocator.allocate( entry._indexCount ) : SpanAllocator::kInvalidOffset;
            if ( indexBase == SpanAllocator::kInvalidOffset )
            {
                // 이미 받은 구간들은 repack 이 처음부터 다시 나눈다. 이번에 자리만 받고 아직 채우지 않은 메시는 옛 풀에 내용이 없다 — 자리를 지워
                // repack 이 옛 풀(남이 쓰던 빈 구간)에서 복사하지 않고 원본에서 채우게 한다.
                for ( const IdentityToken& addedID : _listScratchAdded )
                {
                    Entry& added     = _mapEntry.find( addedID )->second;
                    added._base      = kInvalidBase;
                    added._indexBase = kInvalidBase;
                }
                return false;
            }
            entry._base      = base;
            entry._indexBase = indexBase;
            _liveVertexCount += entry._count;
            _listScratchIndexRegion.push_back( RHIBufferCopyRegion{ static_cast<uint32>( _listScratchIndex.size() * sizeof( uint32 ) ), indexBase * static_cast<uint32>( sizeof( uint32 ) ),
                                                                    entry._indexCount * static_cast<uint32>( sizeof( uint32 ) ) } );
            appendDrawIndices( *entry._pMesh );
        }
        // 정점은 GPU 원본(렌더 프록시)에서 복사로 채운다 — 프레임 업로드 단계가 기록한다(R-15). 인덱스는 CPU 인덱스에서 그대로 올린다(업로드 힙 인덱스 버퍼).
        for ( const IdentityToken& contentID : _listScratchAdded )
        {
            const Entry& entry = _mapEntry.find( contentID )->second;
            if ( queueMeshFill( *pDevice, *entry._pMesh, entry._base, _allocator.getCapacity() ) == false )
                SW_LOG_ERROR_ONCE_ID( "GPUMeshVertexPool.NoVertexSource", "A pooled mesh has neither a render proxy vertex buffer nor CPU vertices - its pool span stays empty" );
        }
        if ( _listScratchIndexRegion.empty() == false )
            pDevice->getResourceFactory()->updateGeometryBufferRegions( _indexBuffer, _listScratchIndex.data(), _listScratchIndexRegion.data(),
                                                                        static_cast<uint32>( _listScratchIndexRegion.size() ) );
        return true;
    }

    void GPUMeshVertexPool::repack( IRHIDevice* pDevice )
    {
        // 새 버퍼는 아무 프레임도 읽지 않는다 — 대기 중인 반납까지 버리고 살아 있는 메시를 앞에서부터 잇는다.
        uint32 liveCount      = 0;
        uint32 liveIndexCount = 0;
        for ( const auto& [contentID, entry] : _mapEntry )
        {
            liveCount += entry._count;
            liveIndexCount += entry._indexCount;
        }
        const uint32 capacity      = MathUtil::clamp( liveCount + liveCount / 2u, kMinPoolVertices, kMaxPoolVertices );
        const uint32 indexCapacity = MathUtil::clamp( liveIndexCount + liveIndexCount / 2u, kMinPoolIndices, kMaxPoolIndices );

        // 옛 풀 정점 버퍼는 새 풀로 GPU 복사하는 원본이다(R-15 — CPU 정점을 다시 읽지 않는다). 그 복사를 프레임 업로드 단계가 기록한 뒤에 지연 해제한다.
        // 옛 자리(구간 시작)를 먼저 적어 둔다 — 아래에서 새 자리로 덮는다. 내용이 바뀌어 다시 들어온 메시는 옛 자리가 없다(`retireEntry`).
        IRHIResourceFactory*               pFactory          = pDevice->getResourceFactory();
        const RHIBufferHandle              oldVertexBuffer   = _vertexBuffer;
        const uint32                       oldCapacity       = _allocator.getCapacity();
        const uint32                       oldAttributeStart = RHIMeshVertexStreams::computeAttributeOffset( oldCapacity );
        unordered_map<const Mesh*, uint32> mapOldBase; // 이 호출 안에서만 — 맵의 메시는 모두 이번 update 에서 본 것이라 살아 있다
        for ( const auto& [contentID, entry] : _mapEntry )
        {
            if ( oldVertexBuffer != 0 && entry._base != kInvalidBase )
                mapOldBase[entry._pMesh] = entry._base;
        }
        if ( oldVertexBuffer != 0 )
            _listRetiredBuffer.push_back( oldVertexBuffer );
        if ( _indexBuffer != 0 )
            pDevice->releaseHandle( _indexBuffer ); // 인덱스는 CPU 인덱스에서 새로 올린다(업로드 힙)
        _vertexBuffer = 0;
        _indexBuffer  = 0;
        _allocator.reset( capacity );
        _indexAllocator.reset( indexCapacity );
        _liveVertexCount = 0;

        _vertexBuffer = pFactory->createGPUVertexBuffer( RHIMeshVertexStreams::computeBufferBytes( _format, capacity ) );
        if ( _vertexBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "GPUMeshVertexPool.FailedCreateVertexPool", "Failed to create the vertex pool buffers (%# vertices, %# indices) - the batches are drawn with their own buffers.", capacity, indexCapacity );
            for ( auto& [contentID, entry] : _mapEntry )
            {
                entry._base      = kInvalidBase;
                entry._indexBase = kInvalidBase;
            }
            _allocator.reset( 0 );
            _indexAllocator.reset( 0 );
            return;
        }

        // 목록 순서 = 맵의 밀집 순서다. 정렬하지 않는다 — 구간 위치는 그림에 드러나지 않는다.
        _listScratchIndex.clear();
        _listScratchIndex.reserve( indexCapacity );
        const uint32 attributeStride = constant::render::getMeshAttributeStride( _format );
        const uint32 newAttribute    = RHIMeshVertexStreams::computeAttributeOffset( capacity );
        bool         bWarnedFull     = false;
        for ( auto& [contentID, entry] : _mapEntry )
        {
            const Mesh* const pMesh = entry._pMesh;
            entry._base             = _allocator.allocate( entry._count );
            entry._indexBase        = entry._base != SpanAllocator::kInvalidOffset ? _indexAllocator.allocate( entry._indexCount ) : SpanAllocator::kInvalidOffset;
            if ( entry._indexBase == SpanAllocator::kInvalidOffset )
            {
                if ( entry._base != SpanAllocator::kInvalidOffset )
                    _allocator.free( entry._base, entry._count ); // 새 버퍼라 아무 프레임도 읽지 않는다 — 곧바로 돌려준다
                entry._base      = kInvalidBase;
                entry._indexBase = kInvalidBase;
                if ( bWarnedFull == false )
                    SW_LOG_WARNING_ID( "GPUMeshVertexPool.VertexPoolFullVertices",
                                       "The vertex pool is full (%# vertices, %# indices) - the remaining meshes are drawn with their own vertex and index buffers (they cannot join multi-draw).",
                                       kMaxPoolVertices, kMaxPoolIndices );
                bWarnedFull = true;
                continue;
            }
            // 맵의 메시는 모두 이번 update 에서 본 것이라 살아 있다(못 본 것은 쓸기에서 지웠다). 인덱스 구간은 앞에서부터 이어지므로 목록 끝이 곧 구간 시작이다.
            const auto oldIt = mapOldBase.find( pMesh );
            if ( oldIt != mapOldBase.end() )
            {
                // 옛 풀 → 새 풀 — 위치 구간과 속성 구간을 한 번씩(두 풀의 속성 구간 시작은 용량이 정한다).
                const uint64 count = entry._count;
                _listPendingCopy.push_back( PendingCopy{ _vertexBuffer, static_cast<uint64>( entry._base ) * constant::render::kMeshPositionStride, oldVertexBuffer,
                                                         static_cast<uint64>( oldIt->second ) * constant::render::kMeshPositionStride, count * constant::render::kMeshPositionStride } );
                _listPendingCopy.push_back( PendingCopy{ _vertexBuffer, newAttribute + static_cast<uint64>( entry._base ) * attributeStride, oldVertexBuffer,
                                                         oldAttributeStart + static_cast<uint64>( oldIt->second ) * attributeStride, count * attributeStride } );
            }
            else if ( queueMeshFill( *pDevice, *pMesh, entry._base, capacity ) == false )
            {
                SW_LOG_ERROR_ONCE_ID( "GPUMeshVertexPool.NoVertexSource", "A pooled mesh has neither a render proxy vertex buffer nor CPU vertices - its pool span stays empty" );
            }
            appendDrawIndices( *pMesh );
            _liveVertexCount += entry._count;
        }
        // 꼬리는 비워 둔 용량이다 — 0 으로 채워 버퍼 전체를 한 번에 만든다(createIndexBuffer 는 내용 없이 만들 수 없다).
        _listScratchIndex.resize( indexCapacity );
        _indexBuffer = pFactory->createIndexBuffer( _listScratchIndex.data(), indexCapacity * static_cast<uint32>( sizeof( uint32 ) ) );
        if ( _indexBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "GPUMeshVertexPool.FailedCreateVertexPool", "Failed to create the vertex pool buffers (%# vertices, %# indices) - the batches are drawn with their own buffers.", capacity, indexCapacity );
            _listRetiredBuffer.push_back( _vertexBuffer ); // 쌓인 복사가 읽거나 쓴다 — 기록한 뒤 놓는다
            _vertexBuffer = 0;
            // 항목은 남긴다 — 내용이 같으면 집합이 바뀐 프레임에만 다시 시도한다(매 프레임 다시 만들며 오류를 쌓지 않는다).
            for ( auto& [contentID, entry] : _mapEntry )
            {
                entry._base      = kInvalidBase;
                entry._indexBase = kInvalidBase;
            }
            _allocator.reset( 0 );
            _indexAllocator.reset( 0 );
            _liveVertexCount = 0;
        }
    }

    void GPUMeshVertexPool::appendDrawIndices( const Mesh& mesh )
    {
        _listScratchIndex.insert( _listScratchIndex.end(), mesh.getIndices().begin(), mesh.getIndices().end() );
        _listScratchIndex.insert( _listScratchIndex.end(), mesh.getLODIndices().begin(), mesh.getLODIndices().end() );
    }

    void GPUMeshVertexPool::release( IRHIDevice* pDevice )
    {
        IRHIResourceFactory* pFactory = pDevice != nullptr ? pDevice->getResourceFactory() : nullptr;
        if ( pFactory != nullptr && _vertexBuffer != 0 )
            pDevice->releaseHandle( _vertexBuffer );
        // 기록하지 못한 복사는 버린다(디바이스가 바뀌거나 내려간다) — 옛 풀 버퍼도 함께 놓는다.
        for ( const RHIBufferHandle retired : _listRetiredBuffer )
        {
            if ( pFactory != nullptr )
                pDevice->releaseHandle( retired );
        }
        _listRetiredBuffer.clear();
        _listPendingCopy.clear();
        if ( pFactory != nullptr && _indexBuffer != 0 )
            pDevice->releaseHandle( _indexBuffer );
        _vertexBuffer = 0;
        _indexBuffer  = 0;
        _mapEntry.clear();
        _allocator.reset( 0 );
        _indexAllocator.reset( 0 );
        _liveVertexCount = 0;
    }
} // namespace sw
