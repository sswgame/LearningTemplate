#include "pch.h"

#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "GPUVertexAnimationPool" );

    void GPUVertexAnimationPool::uploadEntry( IRHIDevice* pDevice, const MeshVertexAnimation& animation, uint32 base )
    {
        vector<uint32> listWord( shaderslot::kVertexAnimationHeaderWords + animation._listFrameVertex.size() * shaderslot::kVertexAnimationWordsPerElement );
        // 머리 단어: 셰이더(InstanceData::computeVertexAnimationElements · VertexAnimation::computePosition)가 프레임 수, 프레임율, 정점 수, 반복, 위치 경계를 여기서 읽는다.
        const uint32 arrHeader[shaderslot::kVertexAnimationHeaderWords] = {
            animation._frameCount,
            BitUtil::floatToBits( animation._framesPerSecond ),
            animation._vertexCount,
            animation._bLoop == SW_TRUE ? 1u : 0u,
            BitUtil::floatToBits( animation._boundsCenter._x ),
            BitUtil::floatToBits( animation._boundsCenter._y ),
            BitUtil::floatToBits( animation._boundsCenter._z ),
            BitUtil::floatToBits( animation._boundsHalfExtent._x ),
            BitUtil::floatToBits( animation._boundsHalfExtent._y ),
            BitUtil::floatToBits( animation._boundsHalfExtent._z ),
        };
        Memory::copy( listWord.data(), arrHeader, sizeof( arrHeader ) );
        // 원소(8 B)는 리틀 엔디언 단어 둘 그대로다(MeshVertexAnimationElement).
        if ( animation._listFrameVertex.empty() == false )
            Memory::copy( listWord.data() + shaderslot::kVertexAnimationHeaderWords, animation._listFrameVertex.data(),
                          animation._listFrameVertex.size() * sizeof( MeshVertexAnimationElement ) );
        _table.upload( pDevice, listWord.data(), static_cast<uint32>( listWord.size() * sizeof( uint32 ) ), base * static_cast<uint32>( sizeof( uint32 ) ) );
    }

    bool GPUVertexAnimationPool::resetStorage( IRHIDevice* pDevice, uint32 capacity )
    {
        _mapEntry.clear();
        _mapRecord.clear();
        _mapBase.clear();
        constexpr RHIBufferUsage kUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        // 풀은 필요한 크기로 다시 만든다(ensureCapacity 가 키우고 옛 버퍼는 프레임 지연으로 놓는다). 못 만들면(장치 메모리 부족) 테이블 없이 번호만 나가지 않게
        // 모든 메시를 풀에서 빼고 오류를 남긴다 — 그 배치들은 그리지 않는다. 다음 프레임에 다시 시도한다.
        if ( _table.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), capacity, kUsage, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Could not create the vertex animation table (%# words) - meshes with vertex animation are not drawn", capacity );
            _allocator.reset( 0 );
            _listBuilt.clear();
            _listBuiltContentID.clear();
            _wordCount = 0;
            return false;
        }
        _allocator.reset( capacity );
        _wordCount = capacity;
        return true;
    }

    bool GPUVertexAnimationPool::placeWanted( IRHIDevice* pDevice, const vector<Wanted>& listWanted, const vector<size_t>& listIndex )
    {
        for ( const size_t index : listIndex )
        {
            const Wanted& wanted = listWanted[index];
            const auto    shared = _mapEntry.find( wanted._pAnimation );
            if ( shared != _mapEntry.end() )
            {
                ++shared->second._refCount;
                _mapRecord[wanted._pMesh] = MeshRecord{ wanted._pAnimation, wanted._contentID };
                continue;
            }
            const size_t needed = shaderslot::kVertexAnimationHeaderWords + wanted._pAnimation->_listFrameVertex.size() * shaderslot::kVertexAnimationWordsPerElement;
            if ( needed > kMaxWordCount )
            {
                // 상한 초과는 오류다 — 이 메시는 풀에 없고(baseOf = kInvalidBase) 그 배치는 그리지 않는다(바인드 포즈로 대신 그리지 않는다, GPUMeshBatch::_bDeformRefused).
                SW_LOG_ERROR( "Vertex animation pool limit exceeded (%# words, table needs %#) - that mesh is not drawn", kMaxWordCount, static_cast<uint32>( needed ) );
                continue;
            }
            const uint32 base = _allocator.allocate( static_cast<uint32>( needed ) );
            if ( base == SpanAllocator::kInvalidOffset )
                return false;
            _mapEntry.emplace( wanted._pAnimation, Entry{ base, static_cast<uint32>( needed ), 1u } );
            _mapRecord[wanted._pMesh] = MeshRecord{ wanted._pAnimation, wanted._contentID };
            uploadEntry( pDevice, *wanted._pAnimation, base );
        }
        return true;
    }

    void GPUVertexAnimationPool::refreshBases()
    {
        _mapBase.clear();
        for ( const auto& record : _mapRecord )
        {
            const auto entry = _mapEntry.find( record.second._pAnimation );
            if ( entry != _mapEntry.end() )
                _mapBase.emplace( record.first, entry->second._base );
        }
    }

    void GPUVertexAnimationPool::rebuild( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr )
            return;
        // 테이블은 베이크하고 나면 변하지 않는다. 목록(포인터와 내용 번호)이 같으면 아무것도 하지 않는다.
        bool bSame = _listBuilt.size() == listMesh.size();
        for ( size_t index = 0; bSame && index < listMesh.size(); ++index )
        {
            bSame = _listBuilt[index] == listMesh[index] && listMesh[index] != nullptr && _listBuiltContentID[index] == listMesh[index]->getContentID();
        }
        if ( bSame )
            return;

        _listBuilt.assign( listMesh.begin(), listMesh.end() );
        _listBuiltContentID.clear();
        vector<Wanted> listWanted;
        for ( const Mesh* pMesh : listMesh )
        {
            const uint64 contentID = pMesh != nullptr ? pMesh->getContentID() : 0u;
            _listBuiltContentID.push_back( contentID );
            const MeshVertexAnimation* pAnimation = ( pMesh != nullptr ) ? pMesh->findVertexAnimation() : nullptr;
            if ( pAnimation == nullptr || pAnimation->isEmpty() )
                continue;
            listWanted.push_back( Wanted{ pMesh, pAnimation, contentID } );
        }

        // 반납한 구간은 재구성 몇 번 뒤에야 다시 내준다(앞 프레임이 아직 읽는 구간을 덮어쓰지 않는다).
        ++_rebuildSerial;
        if ( _rebuildSerial > kRetireLag )
            _allocator.retireDeferred( _rebuildSerial - kRetireLag );

        // 1) 내용이 그대로인 메시의 기록은 남기고, 아무 메시도 쓰지 않게 된 테이블의 구간은 돌려받는다.
        unordered_map<const Mesh*, MeshRecord>            mapKeptRecord;
        unordered_map<const MeshVertexAnimation*, uint32> mapKeptRef;
        vector<size_t>                                    listPending;
        for ( size_t index = 0; index < listWanted.size(); ++index )
        {
            const Wanted& wanted = listWanted[index];
            const auto    record = _mapRecord.find( wanted._pMesh );
            const bool    bKept  = record != _mapRecord.end() && record->second._pAnimation == wanted._pAnimation && record->second._contentID == wanted._contentID &&
                               _mapEntry.find( wanted._pAnimation ) != _mapEntry.end();
            if ( bKept )
            {
                mapKeptRecord.emplace( wanted._pMesh, record->second );
                ++mapKeptRef[wanted._pAnimation];
            }
            else
            {
                listPending.push_back( index );
            }
        }
        vector<const MeshVertexAnimation*> listDropped;
        for ( auto& entry : _mapEntry )
        {
            const auto kept = mapKeptRef.find( entry.first );
            if ( kept == mapKeptRef.end() )
            {
                _allocator.freeDeferred( entry.second._base, entry.second._wordCount, _rebuildSerial );
                listDropped.push_back( entry.first );
            }
            else
            {
                entry.second._refCount = kept->second;
            }
        }
        for ( const MeshVertexAnimation* pDropped : listDropped )
        {
            _mapEntry.erase( pDropped );
        }
        _mapRecord = std::move( mapKeptRecord );

        // 2) 새로 들어온 메시를 둔다. 구간이 모자라면 용량을 넉넉히 잡아 전부 다시 올린다(버퍼는 자라면 새로 만들어져 옛 내용이 따라오지 않는다).
        if ( placeWanted( pDevice, listWanted, listPending ) == false )
        {
            size_t totalWords = 0;
            for ( const Wanted& wanted : listWanted )
            {
                totalWords += shaderslot::kVertexAnimationHeaderWords + wanted._pAnimation->_listFrameVertex.size() * shaderslot::kVertexAnimationWordsPerElement;
            }
            // 같은 테이블을 나누는 메시를 두 번 센 만큼 넉넉하다. 반을 더 얹어 다음 재구성이 구간 갱신으로 끝나게 한다.
            const size_t   wantedWords = totalWords + totalWords / 2u + 1024u;
            const uint32   capacity    = static_cast<uint32>( MathUtil::min( wantedWords, static_cast<size_t>( kMaxWordCount ) ) );
            vector<size_t> listAll( listWanted.size() );
            for ( size_t index = 0; index < listAll.size(); ++index )
            {
                listAll[index] = index;
            }
            if ( resetStorage( pDevice, capacity ) == false )
                return;
            if ( placeWanted( pDevice, listWanted, listAll ) == false )
            {
                // 상한 안에서 다 못 담는다 — 담은 것까지만 풀에 있고 나머지 메시는 그리지 않는다.
                SW_LOG_ERROR( "Vertex animation pool is full (%# words) - some meshes with vertex animation are not drawn", capacity );
            }
        }
        refreshBases();
    }

    uint32 GPUVertexAnimationPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    void GPUVertexAnimationPool::release( IRHIDevice* pDevice )
    {
        _table.release( pDevice );
        _allocator.reset( 0 );
        _mapEntry.clear();
        _mapRecord.clear();
        _mapBase.clear();
        _listBuilt.clear();
        _listBuiltContentID.clear();
        _wordCount = 0;
    }
} // namespace sw
