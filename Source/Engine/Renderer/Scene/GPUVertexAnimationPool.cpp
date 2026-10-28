#include "pch.h"

#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

#include "Core/Common/BitUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "GPUVertexAnimationPool" );

    void GPUVertexAnimationPool::rebuild( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr )
            return;
        // 테이블은 베이크하고 나면 변하지 않는다. 목록(포인터와 내용 번호)이 같으면 다시 업로드하지 않는다.
        bool bSame = _listBuilt.size() == listMesh.size();
        for ( size_t index = 0; bSame && index < listMesh.size(); ++index )
        {
            bSame = _listBuilt[index] == listMesh[index] && listMesh[index] != nullptr && _listBuiltContentID[index] == listMesh[index]->getContentID();
        }
        if ( bSame )
            return;

        _listBuilt.assign( listMesh.begin(), listMesh.end() );
        _listBuiltContentID.clear();
        for ( const Mesh* pMesh : listMesh )
        {
            _listBuiltContentID.push_back( pMesh != nullptr ? pMesh->getContentID() : 0u );
        }
        _mapBase.clear();
        _mapBaseByAnimation.clear();

        vector<uint32> listWord;
        for ( const Mesh* pMesh : listMesh )
        {
            const MeshVertexAnimation* pAnimation = ( pMesh != nullptr ) ? pMesh->findVertexAnimation() : nullptr;
            if ( pAnimation == nullptr || pAnimation->isEmpty() )
                continue;
            const auto shared = _mapBaseByAnimation.find( pAnimation );
            if ( shared != _mapBaseByAnimation.end() )
            {
                _mapBase.emplace( pMesh, shared->second );
                continue;
            }
            const size_t needed = shaderslot::kVertexAnimationHeaderWords + pAnimation->_listFrameVertex.size() * shaderslot::kVertexAnimationWordsPerElement;
            if ( listWord.size() + needed > kMaxWordCount )
            {
                // 상한 초과는 오류다 — 이 메시는 풀에 없고(baseOf = kInvalidBase) 그 배치는 그리지 않는다(바인드 포즈로 대신 그리지 않는다, GPUMeshBatch::_bDeformRefused).
                SW_LOG_ERROR( "Vertex animation pool limit exceeded (%# words, table needs %#) - that mesh is not drawn", kMaxWordCount, static_cast<uint32>( needed ) );
                continue;
            }
            const uint32 base = static_cast<uint32>( listWord.size() );
            // 머리 단어: 셰이더(InstanceData::computeVertexAnimationElements · VertexAnimation::computePosition)가 프레임 수, 프레임율, 정점 수, 반복, 위치 경계를 여기서 읽는다.
            const uint32 arrHeader[shaderslot::kVertexAnimationHeaderWords] = {
                pAnimation->_frameCount,
                BitUtil::floatToBits( pAnimation->_framesPerSecond ),
                pAnimation->_vertexCount,
                pAnimation->_bLoop == SW_TRUE ? 1u : 0u,
                BitUtil::floatToBits( pAnimation->_boundsCenter._x ),
                BitUtil::floatToBits( pAnimation->_boundsCenter._y ),
                BitUtil::floatToBits( pAnimation->_boundsCenter._z ),
                BitUtil::floatToBits( pAnimation->_boundsHalfExtent._x ),
                BitUtil::floatToBits( pAnimation->_boundsHalfExtent._y ),
                BitUtil::floatToBits( pAnimation->_boundsHalfExtent._z ),
            };
            listWord.insert( listWord.end(), std::begin( arrHeader ), std::end( arrHeader ) );
            // 원소(8 B)는 리틀 엔디언 단어 둘 그대로다(MeshVertexAnimationElement).
            const size_t wordOffset = listWord.size();
            listWord.resize( wordOffset + pAnimation->_listFrameVertex.size() * shaderslot::kVertexAnimationWordsPerElement );
            Memory::copy( listWord.data() + wordOffset, pAnimation->_listFrameVertex.data(), pAnimation->_listFrameVertex.size() * sizeof( MeshVertexAnimationElement ) );
            _mapBaseByAnimation.emplace( pAnimation, base );
            _mapBase.emplace( pMesh, base );
        }
        _wordCount = static_cast<uint32>( listWord.size() );
        if ( _wordCount == 0 )
        {
            _table.release( pDevice );
            return;
        }
        constexpr RHIBufferUsage kUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             kStride = static_cast<uint32>( sizeof( uint32 ) );
        // 풀은 집합이 바뀔 때마다 필요한 크기로 다시 만든다(ensureCapacity 가 키우고 옛 버퍼는 프레임 지연으로 놓는다). 못 만들면(장치 메모리 부족)
        // 테이블 없이 번호만 나가지 않게 모든 메시를 풀에서 빼고 오류를 남긴다 — 그 배치들은 그리지 않는다. 다음 프레임에 다시 시도한다.
        if ( _table.ensureCapacity( pDevice, kStride, _wordCount, kUsage, true, false, listWord.data() ) == false )
        {
            SW_LOG_ERROR( "Could not create the vertex animation table (%# words) - meshes with vertex animation are not drawn", _wordCount );
            _mapBase.clear();
            _mapBaseByAnimation.clear();
            _listBuilt.clear();
            _listBuiltContentID.clear();
            _wordCount = 0;
            return;
        }
        _table.upload( pDevice, listWord.data(), _wordCount * kStride );
    }

    uint32 GPUVertexAnimationPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    void GPUVertexAnimationPool::release( IRHIDevice* pDevice )
    {
        _table.release( pDevice );
        _mapBase.clear();
        _mapBaseByAnimation.clear();
        _listBuilt.clear();
        _listBuiltContentID.clear();
        _wordCount = 0;
    }
} // namespace sw
