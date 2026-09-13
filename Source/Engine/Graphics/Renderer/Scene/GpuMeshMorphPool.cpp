#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
    SW_LOG_CALLER( "GpuMeshMorphPool" );

    bool GpuMeshMorphPool::isDispatchable() const
    {
        return _vertexCount > 0 && _rest._srv != kInvalidDescriptorIndex && _morph._uav != kInvalidDescriptorIndex;
    }

    uint32 GpuMeshMorphPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    void GpuMeshMorphPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        if ( pDevice == nullptr )
            return;

        // 목록이 그대로면 다시 만들지 않는다. 레스트 포즈는 변하지 않으므로 **한 번만** 올린다 —
        // 매 프레임 올리면 이 클래스가 없애려던 바로 그 비용(정점 재업로드)을 다시 치르게 된다.
        const bool bSameSet = ( _listBuilt.size() == listMesh.size() ) &&
                              std::equal( _listBuilt.begin(), _listBuilt.end(), listMesh.begin() );
        if ( bSameSet && _vertexCount > 0 )
            return;

        _mapBase.clear();
        _listBuilt.clear();
        _listBuilt.reserve( listMesh.size() );
        _vertexCount = 0;

        // 레스트 정점을 한 줄로 잇는다. 구간 시작이 곧 그 메시의 base 다.
        vector<GpuMorphVertex> listRest;
        for ( Mesh* pMesh : listMesh )
        {
            if ( pMesh == nullptr )
                continue;
            const vector<RHIVertex>& listVertex = pMesh->getVertices();
            if ( listVertex.empty() )
                continue;
            const uint32 count = static_cast<uint32>( listVertex.size() );
            if ( _vertexCount + count > kMaxPoolVertices )
            {
                // 예산 초과 — 이 메시는 풀에 넣지 않는다. `baseOf` 가 kInvalidBase 를 돌려주고 셰이더는
                // 레스트 포즈로 그린다. 언리얼 스킨 캐시가 가득 차면 일반 경로로 되돌리는 것과 같다.
                SW_LOG_WARNING( "모프 풀이 가득 찼습니다(%# 정점) — 남은 메시는 레스트 포즈로 그립니다.", kMaxPoolVertices );
                break;
            }
            _mapBase.emplace( pMesh, _vertexCount );
            _listBuilt.push_back( pMesh );
            for ( const RHIVertex& vertex : listVertex )
            {
                GpuMorphVertex morphVertex{};
                morphVertex._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                // 레스트 노멀도 함께 올린다 — 컴퓨트가 변형된 노멀을 만들려면 원래 노멀이 있어야 한다.
                morphVertex._normal = float4{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2], 0.0f };
                listRest.push_back( morphVertex );
            }
            _vertexCount += count;
        }

        if ( _vertexCount == 0 )
        {
            _rest.release( pDevice );
            _morph.release( pDevice );
            return;
        }

        constexpr RHIBufferUsage kRestUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kMorphUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource |
                                               RHIBufferUsage::UnorderedAccess;
        // 버퍼 원소는 **float4** 이고 정점 하나가 원소 둘이다 — 셰이더 선언(`StructuredBuffer<float4>`)과
        // stride 가 같아야 DX11 이 SRV 를 받는다. 바이트 수는 정점 × sizeof(GpuMorphVertex) 그대로다.
        const uint32 stride       = static_cast<uint32>( sizeof( float4 ) );
        const uint32 elementCount = _vertexCount * kMorphFloat4PerVertex;

        // 레스트는 내용을 실어 만든다(한 번). 결과는 컴퓨트가 채우므로 초기값이 필요 없다.
        if ( _rest.ensureCapacity( pDevice, stride, elementCount, kRestUsage, true, false, listRest.data() ) )
            _rest.upload( pDevice, listRest.data(), elementCount * stride );
        _morph.ensureCapacity( pDevice, stride, elementCount, kMorphUsage, true, true, nullptr );

        // UAV 를 못 받으면(백엔드·드라이버가 거절) 모프는 조용히 꺼진다 — 그리기는 레스트로 살아 있다.
        if ( _morph._uav == kInvalidDescriptorIndex )
            SW_LOG_WARNING( "모프 결과 버퍼에 UAV 를 걸지 못했습니다 — 이 백엔드에서는 모프가 꺼집니다." );
    }

    void GpuMeshMorphPool::release( IRHIDevice* pDevice )
    {
        _rest.release( pDevice );
        _morph.release( pDevice );
        _mapBase.clear();
        _listBuilt.clear();
        _vertexCount = 0;
    }
} // namespace sw
