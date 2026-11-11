#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
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
        , _liveVertexCount{ 0 }
    {
    }

    uint32 GPUMeshVertexPool::getAttributeOffset() const
    {
        return RHIMeshVertexStreams::computeAttributeOffset( _allocator.getCapacity() );
    }

    void GPUMeshVertexPool::writeDrawVertices( const Mesh& mesh, uint32 firstVertex, uint32 vertexCapacity )
    {
        const uint32 lod0Count = static_cast<uint32>( mesh.getVertices().size() );
        RHIMeshVertexStreams::write( _format, mesh.getVertices().data(), lod0Count, firstVertex, vertexCapacity, _listScratchByte.data() );
        RHIMeshVertexStreams::write( _format, mesh.getLODVertices().data(), static_cast<uint32>( mesh.getLODVertices().size() ), firstVertex + lod0Count, vertexCapacity,
                                     _listScratchByte.data() );
    }

    GPUMeshVertexPool::Placement GPUMeshVertexPool::getPlacementOf( const Mesh* pMesh ) const
    {
        const auto it = _mapEntry.find( pMesh );
        if ( it == _mapEntry.end() || it->second._base == kInvalidBase )
            return Placement{};
        return Placement{ it->second._base, it->second._indexBase };
    }

    bool GPUMeshVertexPool::update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;

        // 번호 = 이 호출 수(프레임마다 한 번). N 번째에 돌려받은 구간은 N + kMaxFrameCountInFlight 번째부터 다시 나간다 — 그 사이에 N - 1 번째까지
        // 제출한 프레임이 모두 끝난다.
        ++_updateCount;
        _allocator.retireDeferred( _updateCount );
        _indexAllocator.retireDeferred( _updateCount );
        const uint64 retireStamp = _updateCount + constant::render::kMaxFrameCountInFlight;

        const bool bMarkedChange = markListedMeshes( listMesh, retireStamp );
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

    bool GPUMeshVertexPool::markListedMeshes( const vector<Mesh*>& listMesh, uint64 retireStamp )
    {
        // 1) 표시: 이번 목록의 메시를 본다. 처음 보거나 내용이 바뀐 메시는 새 구간이 필요하다.
        bool bChanged = false;
        _listScratchAdded.clear();
        _listScratchRetry.clear();
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh == nullptr || pMesh->getIndices().empty() || pMesh->getVertexFormat() != _format )
                continue; // 다른 형식은 그 형식의 풀이 담는다
            const IdentityToken& contentID   = pMesh->getContentID();
            const uint32         vertexCount = pMesh->getDrawVertexCount(); // LOD 0 뒤에 낮은 단계까지(배치는 단계의 범위로 그린다)
            const uint32         indexCount  = pMesh->getDrawIndexCount();
            const auto           it          = _mapEntry.find( pMesh );
            if ( it == _mapEntry.end() )
            {
                _mapEntry.emplace( pMesh, Entry{ contentID, _updateCount, kInvalidBase, vertexCount, kInvalidBase, indexCount } );
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
            // 포인터는 같고 내용이 다르다(지워진 자리에 새 메시, 또는 setGeometry) — 옛 구간을 돌려주고 새 구간을 받는다.
            retireEntry( entry, retireStamp );
            entry._contentID  = contentID;
            entry._count      = vertexCount;
            entry._indexCount = indexCount;
            _listScratchAdded.push_back( pMesh );
            bChanged = true;
        }
        return bChanged;
    }

    bool GPUMeshVertexPool::sweepUnlistedMeshes( uint64 retireStamp )
    {
        // 2) 쓸기: 이번 목록에 없는 메시의 구간을 돌려준다. 포인터는 따라가지 않는다(이미 지워졌을 수 있다).
        _listScratchRemoved.clear();
        for ( const auto& [pMesh, entry] : _mapEntry )
        {
            if ( entry._seenStamp != _updateCount )
                _listScratchRemoved.push_back( pMesh );
        }
        for ( const Mesh* pMesh : _listScratchRemoved )
        {
            retireEntry( _mapEntry.find( pMesh )->second, retireStamp );
            _mapEntry.erase( pMesh );
        }
        return _listScratchRemoved.empty() == false;
    }

    bool GPUMeshVertexPool::placeAddedMeshes( IRHIDevice* pDevice )
    {
        _listScratchIndex.clear();
        _listScratchRegion.clear();
        _listScratchIndexRegion.clear();
        uint32 addedCount{ 0 };
        for ( const Mesh* pMesh : _listScratchAdded )
        {
            Entry&       entry     = _mapEntry.find( pMesh )->second;
            const uint32 base      = _allocator.allocate( entry._count );
            const uint32 indexBase = base != SpanAllocator::kInvalidOffset ? _indexAllocator.allocate( entry._indexCount ) : SpanAllocator::kInvalidOffset;
            if ( indexBase == SpanAllocator::kInvalidOffset )
                return false; // 이미 받은 구간들은 repack 이 처음부터 다시 나눈다.
            entry._base      = base;
            entry._indexBase = indexBase;
            _liveVertexCount += entry._count;
            // 정점 조각은 아래에서 위치 · 속성 두 조각으로 낸다 — 여기서는 올릴 원본 안 정점 자리(_srcOffset)와 풀 안 시작(_dstOffset)을 정점 단위로 적어 둔다.
            _listScratchRegion.push_back( RHIBufferCopyRegion{ addedCount, base, entry._count } );
            addedCount += entry._count;
            _listScratchIndexRegion.push_back( RHIBufferCopyRegion{ static_cast<uint32>( _listScratchIndex.size() * sizeof( uint32 ) ), indexBase * static_cast<uint32>( sizeof( uint32 ) ),
                                                                    entry._indexCount * static_cast<uint32>( sizeof( uint32 ) ) } );
            appendDrawIndices( *pMesh );
        }
        if ( _listScratchRegion.empty() == false )
        {
            // 올릴 원본 = 이번에 들어온 정점들의 [위치 구간][속성 구간](메시마다 받은 자리에 원본에서 바로 쓴다), 목적 = 풀 버퍼의 같은 두 구간.
            const uint32 attributeStride = constant::render::getMeshAttributeStride( _format );
            const uint32 srcAttribute    = RHIMeshVertexStreams::computeAttributeOffset( addedCount );
            const uint32 dstAttribute    = getAttributeOffset();
            _listScratchByte.resize( RHIMeshVertexStreams::computeBufferBytes( _format, addedCount ) ); // 모든 칸을 아래에서 쓴다
            const size_t meshCount = _listScratchRegion.size();
            for ( size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex )
            {
                writeDrawVertices( *_listScratchAdded[meshIndex], _listScratchRegion[meshIndex]._srcOffset, addedCount );
            }
            for ( size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex )
            {
                const RHIBufferCopyRegion vertexSpan = _listScratchRegion[meshIndex];
                _listScratchRegion[meshIndex]        = RHIBufferCopyRegion{ vertexSpan._srcOffset * constant::render::kMeshPositionStride, vertexSpan._dstOffset * constant::render::kMeshPositionStride,
                                                                     vertexSpan._sizeBytes * constant::render::kMeshPositionStride };
                _listScratchRegion.push_back( RHIBufferCopyRegion{ srcAttribute + vertexSpan._srcOffset * attributeStride, dstAttribute + vertexSpan._dstOffset * attributeStride,
                                                                   vertexSpan._sizeBytes * attributeStride } );
            }
            IRHIResourceFactory* pFactory = pDevice->getResourceFactory();
            pFactory->updateGeometryBufferRegions( _vertexBuffer, _listScratchByte.data(), _listScratchRegion.data(), static_cast<uint32>( _listScratchRegion.size() ) );
            pFactory->updateGeometryBufferRegions( _indexBuffer, _listScratchIndex.data(), _listScratchIndexRegion.data(), static_cast<uint32>( _listScratchIndexRegion.size() ) );
        }
        return true;
    }

    void GPUMeshVertexPool::repack( IRHIDevice* pDevice )
    {
        // 새 버퍼는 아무 프레임도 읽지 않는다 — 대기 중인 반납까지 버리고 살아 있는 메시를 앞에서부터 잇는다.
        uint32 liveCount      = 0;
        uint32 liveIndexCount = 0;
        for ( const auto& [pMesh, entry] : _mapEntry )
        {
            liveCount += entry._count;
            liveIndexCount += entry._indexCount;
        }
        const uint32 capacity      = MathUtil::clamp( liveCount + liveCount / 2u, kMinPoolVertices, kMaxPoolVertices );
        const uint32 indexCapacity = MathUtil::clamp( liveIndexCount + liveIndexCount / 2u, kMinPoolIndices, kMaxPoolIndices );

        IRHIResourceFactory* pFactory = pDevice->getResourceFactory();
        if ( _vertexBuffer != 0 )
            pFactory->destroyBuffer( _vertexBuffer );
        if ( _indexBuffer != 0 )
            pFactory->destroyBuffer( _indexBuffer );
        _vertexBuffer = 0;
        _indexBuffer  = 0;
        _allocator.reset( capacity );
        _indexAllocator.reset( indexCapacity );
        _liveVertexCount = 0;

        // 목록 순서 = 맵의 밀집 순서다. 정렬하지 않는다 — 구간 위치는 그림에 드러나지 않는다. 정점은 받은 구간 자리에 원본에서 바로 쓴다.
        _listScratchByte.assign( RHIMeshVertexStreams::computeBufferBytes( _format, capacity ), uint8{ 0 } ); // 꼬리(빈 용량)는 0 — 앞 호출의 내용을 남기지 않는다
        _listScratchIndex.clear();
        _listScratchIndex.reserve( indexCapacity );
        bool bWarnedFull = false;
        for ( auto& [pMesh, entry] : _mapEntry )
        {
            entry._base      = _allocator.allocate( entry._count );
            entry._indexBase = entry._base != SpanAllocator::kInvalidOffset ? _indexAllocator.allocate( entry._indexCount ) : SpanAllocator::kInvalidOffset;
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
            writeDrawVertices( *pMesh, entry._base, capacity );
            appendDrawIndices( *pMesh );
            _liveVertexCount += entry._count;
        }
        if ( _liveVertexCount == 0 )
            return;
        // 꼬리는 비워 둔 용량이다 — 0 으로 채워 버퍼 전체를 한 번에 만든다(createVertexBuffer · createIndexBuffer 는 내용 없이 만들 수 없다).
        _listScratchIndex.resize( indexCapacity );
        _vertexBuffer = pFactory->createVertexBuffer( _listScratchByte.data(), static_cast<uint32>( _listScratchByte.size() ) );
        _indexBuffer  = pFactory->createIndexBuffer( _listScratchIndex.data(), indexCapacity * static_cast<uint32>( sizeof( uint32 ) ) );
        if ( _vertexBuffer == 0 || _indexBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "GPUMeshVertexPool.FailedCreateVertexPool", "Failed to create the vertex pool buffers (%# vertices, %# indices) - the batches are drawn with their own buffers.", capacity, indexCapacity );
            if ( _vertexBuffer != 0 )
                pFactory->destroyBuffer( _vertexBuffer );
            if ( _indexBuffer != 0 )
                pFactory->destroyBuffer( _indexBuffer );
            _vertexBuffer = 0;
            _indexBuffer  = 0;
            // 항목은 남긴다 — 내용이 같으면 집합이 바뀐 프레임에만 다시 시도한다(매 프레임 다시 만들며 오류를 쌓지 않는다).
            for ( auto& [pMesh, entry] : _mapEntry )
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
            pFactory->destroyBuffer( _vertexBuffer );
        if ( pFactory != nullptr && _indexBuffer != 0 )
            pFactory->destroyBuffer( _indexBuffer );
        _vertexBuffer = 0;
        _indexBuffer  = 0;
        _mapEntry.clear();
        _allocator.reset( 0 );
        _indexAllocator.reset( 0 );
        _liveVertexCount = 0;
    }
} // namespace sw
