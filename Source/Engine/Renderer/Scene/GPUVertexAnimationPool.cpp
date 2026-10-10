#include "pch.h"

#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "GPUVertexAnimationPool" );

    void GPUVertexAnimationPool::rebuild( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr )
            return;
        // 표는 굽고 나면 변하지 않는다 — 목록(포인터 · 내용 번호)이 같으면 다시 올리지 않는다.
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

        vector<float4> listElement;
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
            const size_t needed = 1u + pAnimation->_listFrameVertex.size();
            if ( listElement.size() + needed > kMaxElementCount )
            {
                SW_LOG_WARNING( "Vertex animation pool is full (%# elements) - remaining crowds draw in bind pose", kMaxElementCount );
                continue;
            }
            const uint32 base = static_cast<uint32>( listElement.size() );
            // 머리 원소 — 셰이더(swComputeVertexAnimationElements)가 프레임 수 · 프레임율 · 정점 수 · 반복을 여기서 읽는다.
            listElement.push_back( float4{ static_cast<float32>( pAnimation->_frameCount ), pAnimation->_framesPerSecond, static_cast<float32>( pAnimation->_vertexCount ),
                                           pAnimation->_bLoop == SW_TRUE ? 1.0f : 0.0f } );
            listElement.insert( listElement.end(), pAnimation->_listFrameVertex.begin(), pAnimation->_listFrameVertex.end() );
            _mapBaseByAnimation.emplace( pAnimation, base );
            _mapBase.emplace( pMesh, base );
        }
        _elementCount = static_cast<uint32>( listElement.size() );
        if ( _elementCount == 0 )
        {
            _table.release( pDevice );
            return;
        }
        constexpr RHIBufferUsage kUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             kStride = static_cast<uint32>( sizeof( float4 ) );
        if ( _table.ensureCapacity( pDevice, kStride, _elementCount, kUsage, true, false, listElement.data() ) )
            _table.upload( pDevice, listElement.data(), _elementCount * kStride );
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
        _elementCount = 0;
    }
} // namespace sw
