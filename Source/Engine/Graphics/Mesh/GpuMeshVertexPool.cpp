#include "pch.h"

#include "Engine/Graphics/Mesh/GpuMeshVertexPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
    SW_LOG_CALLER( "GpuMeshVertexPool" );

    uint32 GpuMeshVertexPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    bool GpuMeshVertexPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr || pDevice->getResource() == nullptr )
            return false;

        // 집합 비교 — 포인터를 정렬해 지난번과 같은지 본다. 배치 순서는 프레임마다 바뀔 수 있지만 메시 집합은 드물게 바뀐다.
        _listScratchSorted.clear();
        _listScratchSorted.reserve( listMesh.size() );
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh != nullptr && pMesh->getVertices().empty() == false )
                _listScratchSorted.push_back( pMesh );
        }
        std::sort( _listScratchSorted.begin(), _listScratchSorted.end() );
        _listScratchSorted.erase( std::unique( _listScratchSorted.begin(), _listScratchSorted.end() ), _listScratchSorted.end() );

        const bool bSameSet = ( _listBuilt.size() == _listScratchSorted.size() ) &&
                              std::equal( _listBuilt.begin(), _listBuilt.end(), _listScratchSorted.begin() );
        if ( bSameSet && ( _vertexBuffer != 0 || _listScratchSorted.empty() ) )
            return false;

        release( pDevice );
        _listBuilt = _listScratchSorted;

        vector<RHIVertex> listVertex;
        for ( const Mesh* pMesh : _listBuilt )
        {
            const vector<RHIVertex>& listMeshVertex = pMesh->getVertices();
            const uint32             count          = static_cast<uint32>( listMeshVertex.size() );
            if ( _vertexCount + count > kMaxPoolVertices )
            {
                SW_LOG_WARNING( "정점 풀이 가득 찼습니다(%# 정점) — 남은 메시는 자기 정점 버퍼로 그립니다(멀티 드로우에 못 묶인다).", kMaxPoolVertices );
                break;
            }
            _mapBase.emplace( pMesh, _vertexCount );
            listVertex.insert( listVertex.end(), listMeshVertex.begin(), listMeshVertex.end() );
            _vertexCount += count;
        }

        if ( _vertexCount == 0 )
            return true;

        const uint32 bytes = _vertexCount * static_cast<uint32>( sizeof( RHIVertex ) );
        _vertexBuffer      = pDevice->getResource()->createVertexBuffer( listVertex.data(), bytes );
        if ( _vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "정점 풀 버퍼를 만들지 못했습니다(%# 정점) — 배치는 자기 정점 버퍼로 그립니다.", _vertexCount );
            _mapBase.clear();
            _vertexCount = 0;
        }
        return true;
    }

    void GpuMeshVertexPool::release( IRHIDevice* pDevice )
    {
        if ( _vertexBuffer != 0 && pDevice != nullptr && pDevice->getResource() != nullptr )
            pDevice->getResource()->destroyBuffer( _vertexBuffer );
        _vertexBuffer = 0;
        _mapBase.clear();
        _listBuilt.clear();
        _vertexCount = 0;
    }
} // namespace sw
