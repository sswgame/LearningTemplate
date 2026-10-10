#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "GPUMeshVertexPool" );

    uint32 GPUMeshVertexPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    bool GPUMeshVertexPool::rebuild( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;

        // 집합 비교: 포인터를 정렬해 지난번과 같은지 본다. 배치 순서는 프레임마다 바뀔 수 있지만 메시 집합은 드물게 바뀐다.
        _listScratchSorted.clear();
        _listScratchSorted.reserve( listMesh.size() );
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh != nullptr && pMesh->getVertices().empty() == false )
                _listScratchSorted.push_back( pMesh );
        }
        std::sort( _listScratchSorted.begin(), _listScratchSorted.end() );
        _listScratchSorted.erase( std::unique( _listScratchSorted.begin(), _listScratchSorted.end() ), _listScratchSorted.end() );

        // 포인터와 **내용 번호**를 함께 본다. 포인터만 보면 지워진 메시 자리에 새 메시가 생기거나 같은 메시의 정점이 바뀌어도(setVertices)
        // 같은 집합으로 보여 옛 정점을 그린다. 번호는 지금 살아 있는 쪽(scratch)에서 읽는다 — 지난 목록의 포인터는 이미 죽었을 수 있다.
        bool bSameSet = ( _listBuilt.size() == _listScratchSorted.size() ) &&
                        std::equal( _listBuilt.begin(), _listBuilt.end(), _listScratchSorted.begin() );
        for ( size_t index = 0; bSameSet && index < _listScratchSorted.size(); ++index )
        {
            bSameSet = _listBuiltContentID[index] == _listScratchSorted[index]->getContentID();
        }
        if ( bSameSet && ( _vertexBuffer != 0 || _listScratchSorted.empty() ) )
            return false;

        release( pDevice );
        _listBuilt = _listScratchSorted;
        _listBuiltContentID.clear();
        for ( const Mesh* pMesh : _listBuilt )
        {
            _listBuiltContentID.push_back( pMesh->getContentID() );
        }

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
        _vertexBuffer      = pDevice->getResourceFactory()->createVertexBuffer( listVertex.data(), bytes );
        if ( _vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "정점 풀 버퍼를 만들지 못했습니다(%# 정점) — 배치는 자기 정점 버퍼로 그립니다.", _vertexCount );
            _mapBase.clear();
            _vertexCount = 0;
        }
        return true;
    }

    void GPUMeshVertexPool::release( IRHIDevice* pDevice )
    {
        if ( _vertexBuffer != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr )
            pDevice->getResourceFactory()->destroyBuffer( _vertexBuffer );
        _vertexBuffer = 0;
        _mapBase.clear();
        _listBuilt.clear();
        _listBuiltContentID.clear();
        _vertexCount = 0;
    }
} // namespace sw
