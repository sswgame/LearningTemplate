#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

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

    bool GpuMeshMorphPool::isSkinDispatchable() const
    {
        return getSkinVertexCount() > 0 && _skinBoneCount > 0 && isDispatchable() && _skinWeight._srv != kInvalidDescriptorIndex &&
               _skinPalette._srv != kInvalidDescriptorIndex;
    }

    void GpuMeshMorphPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        build( pDevice, listMesh, vector<Mesh*>{} );
    }

    void GpuMeshMorphPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh, const vector<Mesh*>& listSkinMesh )
    {
        if ( pDevice == nullptr )
            return;

        // 목록이 그대로면 다시 만들지 않는다. 레스트 포즈 · 스킨 가중치는 변하지 않으므로 **한 번만** 올린다.
        // 매 프레임 올리면 이 클래스가 없애려던 바로 그 비용(정점 재업로드)을 다시 치르게 된다.
        // 포인터와 **내용 번호**를 함께 본다(GpuMeshVertexPool::build 와 같은 이유). 번호는 지금 받은 목록에서 읽는다.
        const size_t totalCount = listMorphMesh.size() + listSkinMesh.size();
        bool         bSameSet   = _listBuilt.size() == totalCount;
        for ( size_t index = 0; bSameSet && index < totalCount; ++index )
        {
            const Mesh* pMesh = index < listMorphMesh.size() ? listMorphMesh[index] : listSkinMesh[index - listMorphMesh.size()];
            bSameSet          = _listBuilt[index] == pMesh && pMesh != nullptr && _listBuiltContentId[index] == pMesh->getContentId();
        }
        if ( bSameSet && _vertexCount > 0 )
            return;

        _mapBase.clear();
        _listBuilt.clear();
        _listBuilt.reserve( totalCount );
        _listBuiltContentId.clear();
        _listSkinMesh.clear();
        _listSkinBoneBase.clear();
        _vertexCount    = 0;
        _skinVertexBase = 0;
        _skinBoneCount  = 0;

        // 레스트 정점을 한 줄로 잇는다. 구간 시작이 곧 그 메시의 base 다. 모프 메시가 앞, 스킨드 메시가 뒤다.
        vector<GpuMorphVertex> listRest;
        vector<float4>         listSkinWeight;
        for ( size_t index = 0; index < totalCount; ++index )
        {
            const bool bSkinPart = index >= listMorphMesh.size();
            Mesh*      pMesh     = bSkinPart ? listSkinMesh[index - listMorphMesh.size()] : listMorphMesh[index];
            if ( index == listMorphMesh.size() )
                _skinVertexBase = _vertexCount;
            // 같은 목록 판정이 매 프레임 다시 짓지 않게, 받은 목록 그대로를 적는다(풀에 못 든 것도).
            _listBuilt.push_back( pMesh );
            _listBuiltContentId.push_back( pMesh != nullptr ? pMesh->getContentId() : 0u );
            if ( pMesh == nullptr || ( bSkinPart && pMesh->hasSkin() == false ) )
                continue;
            const vector<RHIVertex>& listVertex = pMesh->getVertices();
            if ( listVertex.empty() )
                continue;
            const uint32 count = static_cast<uint32>( listVertex.size() );
            if ( _vertexCount + count > kMaxPoolVertices )
            {
                // 예산 초과. 이 메시는 풀에 넣지 않는다. `baseOf` 가 kInvalidBase 를 반환하고 셰이더는
                // 레스트 포즈로 그린다. 언리얼 스킨 캐시가 가득 차면 일반 경로로 되돌리는 것과 같다.
                SW_LOG_WARNING( "모프 풀이 가득 찼습니다(%# 정점) — 남은 메시는 레스트 포즈로 그립니다.", kMaxPoolVertices );
                continue;
            }
            _mapBase.emplace( pMesh, _vertexCount );
            for ( const RHIVertex& vertex : listVertex )
            {
                GpuMorphVertex morphVertex{};
                morphVertex._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                // 레스트 노멀도 함께 올린다. 컴퓨트가 변형된 노멀을 만들려면 원래 노멀이 있어야 한다.
                morphVertex._normal = float4{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2], 0.0f };
                listRest.push_back( morphVertex );
            }
            if ( bSkinPart )
            {
                // 팔레트 시작 본을 행 번호에 미리 더한다 — 컴퓨트는 메시를 가르지 않고 한 번에 돈다.
                _listSkinMesh.push_back( pMesh );
                _listSkinBoneBase.push_back( _skinBoneCount );
                for ( const MeshSkinVertex& skin : pMesh->getSkinVertices() )
                {
                    listSkinWeight.push_back( float4{ skin._arrWeight[0], skin._arrWeight[1], skin._arrWeight[2], skin._arrWeight[3] } );
                    listSkinWeight.push_back( float4{ static_cast<float32>( _skinBoneCount + skin._arrJoint[0] ), static_cast<float32>( _skinBoneCount + skin._arrJoint[1] ),
                                                      static_cast<float32>( _skinBoneCount + skin._arrJoint[2] ), static_cast<float32>( _skinBoneCount + skin._arrJoint[3] ) } );
                }
                _skinBoneCount += pMesh->getSkinBoneCount();
            }
            _vertexCount += count;
        }
        if ( listSkinMesh.empty() )
            _skinVertexBase = _vertexCount;

        if ( _vertexCount == 0 )
        {
            _rest.release( pDevice );
            _morph.release( pDevice );
            _skinWeight.release( pDevice );
            _skinPalette.release( pDevice );
            return;
        }

        constexpr RHIBufferUsage kRestUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kMorphUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource |
                                               RHIBufferUsage::UnorderedAccess;
        // 버퍼 원소는 **float4** 이고 정점 하나가 원소 둘이다. 셰이더 선언(`StructuredBuffer<float4>`)과
        // stride 가 같아야 DX11 이 SRV 를 받는다. 바이트 수는 정점 × sizeof(GpuMorphVertex) 그대로다.
        const uint32 stride       = static_cast<uint32>( sizeof( float4 ) );
        const uint32 elementCount = _vertexCount * kMorphFloat4PerVertex;

        // 레스트는 내용을 실어 만든다(한 번). 결과는 컴퓨트가 채우므로 초기값이 필요 없다.
        if ( _rest.ensureCapacity( pDevice, stride, elementCount, kRestUsage, true, false, listRest.data() ) )
            _rest.upload( pDevice, listRest.data(), elementCount * stride );
        _morph.ensureCapacity( pDevice, stride, elementCount, kMorphUsage, true, true, nullptr );

        if ( listSkinWeight.empty() == false )
        {
            const uint32 skinElementCount = static_cast<uint32>( listSkinWeight.size() );
            if ( _skinWeight.ensureCapacity( pDevice, stride, skinElementCount, kRestUsage, true, false, listSkinWeight.data() ) )
                _skinWeight.upload( pDevice, listSkinWeight.data(), skinElementCount * stride );
            _skinPalette.ensureCapacity( pDevice, stride, _skinBoneCount * kSkinFloat4PerBone, kRestUsage, true, false, nullptr );
        }

        // UAV 를 못 받으면(백엔드 · 드라이버가 거절) 모프는 조용히 꺼진다. 그리기는 레스트 포즈로 살아 있다.
        if ( _morph._uav == kInvalidDescriptorIndex )
            SW_LOG_WARNING( "모프 결과 버퍼에 UAV 를 걸지 못했습니다 — 이 백엔드에서는 모프가 꺼집니다." );
    }

    void GpuMeshMorphPool::uploadSkinPalettes( IRHIDevice* pDevice, const vector<GpuSkinPalette>& listPalette, const vector<float4>* pListRow )
    {
        if ( pDevice == nullptr || _skinBoneCount == 0 || _skinPalette.isValid() == false )
            return;

        // 풀 순서로 다시 모은다. 팔레트가 없는(아직 평가되지 않은) 메시는 단위 행렬 — 바인드 포즈다.
        _listScratchPaletteRow.resize( static_cast<size_t>( _skinBoneCount ) * kSkinFloat4PerBone );
        for ( size_t skinIndex = 0; skinIndex < _listSkinMesh.size(); ++skinIndex )
        {
            const Mesh*           pMesh     = _listSkinMesh[skinIndex];
            const uint32          boneBase  = _listSkinBoneBase[skinIndex];
            const uint32          boneCount = pMesh->getSkinBoneCount();
            const GpuSkinPalette* pFound    = nullptr;
            for ( const GpuSkinPalette& palette : listPalette )
            {
                if ( palette._pMesh == pMesh )
                {
                    pFound = &palette;
                    break;
                }
            }
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                float4*      pRow     = &_listScratchPaletteRow[( static_cast<size_t>( boneBase ) + boneIndex ) * kSkinFloat4PerBone];
                const size_t rowStart = ( pFound != nullptr ) ? static_cast<size_t>( pFound->_firstRow ) + static_cast<size_t>( boneIndex ) * kSkinFloat4PerBone : 0u;
                const bool   bHasBone = pFound != nullptr && pListRow != nullptr && boneIndex < pFound->_boneCount && rowStart + kSkinFloat4PerBone <= pListRow->size();
                if ( bHasBone )
                {
                    for ( uint32 rowIndex = 0; rowIndex < kSkinFloat4PerBone; ++rowIndex )
                        pRow[rowIndex] = ( *pListRow )[rowStart + rowIndex];
                    continue;
                }
                pRow[0] = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
                pRow[1] = float4{ 0.0f, 1.0f, 0.0f, 0.0f };
                pRow[2] = float4{ 0.0f, 0.0f, 1.0f, 0.0f };
            }
        }
        _skinPalette.upload( pDevice, _listScratchPaletteRow.data(), static_cast<uint32>( _listScratchPaletteRow.size() * sizeof( float4 ) ) );
    }

    void GpuMeshMorphPool::release( IRHIDevice* pDevice )
    {
        _rest.release( pDevice );
        _morph.release( pDevice );
        _skinWeight.release( pDevice );
        _skinPalette.release( pDevice );
        _mapBase.clear();
        _listBuilt.clear();
        _listBuiltContentId.clear();
        _listSkinMesh.clear();
        _listSkinBoneBase.clear();
        _vertexCount    = 0;
        _skinVertexBase = 0;
        _skinBoneCount  = 0;
    }
} // namespace sw
