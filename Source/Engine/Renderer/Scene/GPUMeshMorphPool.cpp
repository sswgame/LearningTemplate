#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    SW_LOG_CALLER( "GPUMeshMorphPool" );

    namespace
    {
        struct GPUMeshMorphPoolInternal
        {
            static constexpr RHIBufferUsage kReadUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
            static constexpr RHIBufferUsage kWriteUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            /// @brief 버퍼 원소는 float4 / uint4 다. 셰이더 선언과 stride 가 같아야 DX11 이 SRV 를 받는다.
            static constexpr uint32 kElementStride = static_cast<uint32>( sizeof( float4 ) );

            /** @brief 정점의 위치 · 노멀을 float4 둘로 담습니다(레스트). */
            static GPUMorphVertex makeRestVertex( const RHIVertex& vertex )
            {
                GPUMorphVertex restVertex{};
                restVertex._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                // 레스트 노멀도 함께 올린다. 컴퓨트가 변형된 노멀을 만들려면 원래 노멀이 있어야 한다.
                restVertex._normal = float4{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2], 0.0f };
                return restVertex;
            }

            /** @brief 내용을 담아 용량을 맞추고 올립니다. */
            static void uploadAll( IRHIDevice* pDevice, RHIStructuredBufferSlot& slot, const void* pData, uint32 elementCount )
            {
                if ( elementCount == 0 )
                {
                    slot.release( pDevice );
                    return;
                }
                if ( slot.ensureCapacity( pDevice, kElementStride, elementCount, kReadUsage, true, false, pData ) )
                    slot.upload( pDevice, pData, elementCount * kElementStride );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GPUMeshMorphPool::isDispatchable() const
    {
        return getMorphVertexCount() > 0 && _rest._srv != kInvalidDescriptorIndex && _morph._uav != kInvalidDescriptorIndex;
    }

    uint32 GPUMeshMorphPool::baseOf( const Mesh* pMesh ) const
    {
        const auto it = _mapBase.find( pMesh );
        return ( it != _mapBase.end() ) ? it->second : kInvalidBase;
    }

    bool GPUMeshMorphPool::isSkinDispatchable() const
    {
        return getSkinVertexCount() > 0 && _skinBoneCount > 0 && _morph._uav != kInvalidDescriptorIndex && _skinRest._srv != kInvalidDescriptorIndex &&
               _skinWeight._srv != kInvalidDescriptorIndex && _skinInstance._srv != kInvalidDescriptorIndex && _skinPalette._srv != kInvalidDescriptorIndex;
    }

    void GPUMeshMorphPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        build( pDevice, listMesh, vector<Mesh*>{} );
    }

    bool GPUMeshMorphPool::isSameList( const vector<Mesh*>& listMesh, const vector<const Mesh*>& listBuilt, const vector<uint64>& listBuiltContentId )
    {
        // 포인터와 **내용 번호**를 함께 본다(GPUMeshVertexPool::build 와 같은 이유 — 지워진 자리에 새 메시가 생기거나 정점을 바꾼 메시).
        if ( listBuilt.size() != listMesh.size() )
            return false;
        for ( size_t index = 0; index < listMesh.size(); ++index )
        {
            const Mesh* pMesh = listMesh[index];
            if ( listBuilt[index] != pMesh || pMesh == nullptr || listBuiltContentId[index] != pMesh->getContentId() )
                return false;
        }
        return true;
    }

    void GPUMeshMorphPool::rebuildSkinSources( IRHIDevice* pDevice, const vector<Mesh*>& listSkinMesh )
    {
        // 원본 = 스킨 데이터 번호. 사본(`Mesh::createSkinInstance`)은 원본의 번호를 나누므로 레스트 · 가중치가 한 벌만 올라간다.
        vector<uint64>      listDataId;
        vector<const Mesh*> listSource;
        for ( const Mesh* pMesh : listSkinMesh )
        {
            if ( pMesh == nullptr || pMesh->hasSkin() == false || pMesh->getVertexCount() == 0 )
                continue;
            const uint64 dataId = pMesh->getSkinDataId();
            if ( std::find( listDataId.begin(), listDataId.end(), dataId ) != listDataId.end() )
                continue;
            listDataId.push_back( dataId );
            listSource.push_back( pMesh );
        }
        if ( listDataId == _listSourceDataId && _skinRest.isValid() == ( listDataId.empty() == false ) )
            return;

        _listSourceDataId = listDataId;
        _listSourceBase.clear();
        _skinSourceVertexCount = 0;
        vector<GPUMorphVertex> listRest;
        vector<float4>         listWeight;
        vector<GPUMorphVertex> listDelta;
        vector<uint32>         listVertexDeltaStart;
        vector<uint32>         listVertexDeltaCount;
        for ( const Mesh* pSource : listSource )
        {
            _listSourceBase.push_back( _skinSourceVertexCount );
            for ( const RHIVertex& vertex : pSource->getVertices() )
            {
                listRest.push_back( GPUMeshMorphPoolInternal::makeRestVertex( vertex ) );
            }
            // 모프 차이를 정점별로 모은다 — 컴퓨트는 정점 하나의 차이 구간(시작 · 수)을 돌며 (위치 · 노멀 차이, 타깃 번호)를 가중치만큼 더한다.
            const uint32                   vertexCount = pSource->getVertexCount();
            vector<vector<uint32>>         listVertexTarget( vertexCount );
            vector<vector<uint32>>         listVertexDelta( vertexCount );
            const vector<MeshMorphTarget>& listTarget = pSource->getMorphTargets();
            for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( listTarget.size() ); ++targetIndex )
            {
                for ( uint32 deltaIndex = 0; deltaIndex < static_cast<uint32>( listTarget[targetIndex]._listDelta.size() ); ++deltaIndex )
                {
                    const uint32 vertexIndex = listTarget[targetIndex]._listDelta[deltaIndex]._vertexIndex;
                    listVertexTarget[vertexIndex].push_back( targetIndex );
                    listVertexDelta[vertexIndex].push_back( deltaIndex );
                }
            }
            listVertexDeltaStart.clear();
            listVertexDeltaCount.clear();
            for ( uint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
            {
                listVertexDeltaStart.push_back( static_cast<uint32>( listDelta.size() ) );
                listVertexDeltaCount.push_back( static_cast<uint32>( listVertexTarget[vertexIndex].size() ) );
                for ( size_t entry = 0; entry < listVertexTarget[vertexIndex].size(); ++entry )
                {
                    const uint32          targetIndex = listVertexTarget[vertexIndex][entry];
                    const MeshMorphDelta& delta       = listTarget[targetIndex]._listDelta[listVertexDelta[vertexIndex][entry]];
                    GPUMorphVertex        packed{};
                    packed._position = float4{ delta._position._x, delta._position._y, delta._position._z, static_cast<float32>( targetIndex ) };
                    packed._normal   = float4{ delta._normal._x, delta._normal._y, delta._normal._z, 0.0f };
                    listDelta.push_back( packed );
                }
            }
            // 본 번호는 원본 스켈레톤의 번호 그대로다 — 팔레트 시작은 인스턴스 표가 더한다(사본마다 다르다).
            const vector<MeshSkinVertex>& listSkin = pSource->getSkinVertices();
            for ( uint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
            {
                const MeshSkinVertex& skin = listSkin[vertexIndex];
                listWeight.push_back( float4{ skin._arrWeight[0], skin._arrWeight[1], skin._arrWeight[2], skin._arrWeight[3] } );
                listWeight.push_back( float4{ static_cast<float32>( skin._arrJoint[0] ), static_cast<float32>( skin._arrJoint[1] ), static_cast<float32>( skin._arrJoint[2] ),
                                              static_cast<float32>( skin._arrJoint[3] ) } );
                listWeight.push_back( float4{ static_cast<float32>( listVertexDeltaStart[vertexIndex] ), static_cast<float32>( listVertexDeltaCount[vertexIndex] ), 0.0f, 0.0f } );
            }
            _skinSourceVertexCount += vertexCount;
        }
        // 레스트 버퍼 = [원본 레스트 정점][모프 차이] — 둘 다 float4 둘이라 한 버퍼에 잇는다(컴퓨트 SRV 슬롯은 넷뿐이다).
        _skinDeltaBase = static_cast<uint32>( listRest.size() ) * shaderslot::kMorphFloat4PerVertex;
        listRest.insert( listRest.end(), listDelta.begin(), listDelta.end() );
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _skinRest, listRest.data(), static_cast<uint32>( listRest.size() ) * shaderslot::kMorphFloat4PerVertex );
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _skinWeight, listWeight.data(), static_cast<uint32>( listWeight.size() ) );
    }

    void GPUMeshMorphPool::build( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh, const vector<Mesh*>& listSkinMesh )
    {
        if ( pDevice == nullptr )
            return;

        // 목록이 그대로면 다시 만들지 않는다. 레스트 포즈 · 스킨 가중치는 변하지 않으므로 **한 번만** 올린다.
        // 매 프레임 올리면 이 클래스가 없애려던 바로 그 비용(정점 재업로드)을 다시 치르게 된다.
        const bool bSameMorph = isSameList( listMorphMesh, _listBuiltMorph, _listBuiltMorphContentId );
        const bool bSameSkin  = isSameList( listSkinMesh, _listBuiltSkin, _listBuiltSkinContentId );
        if ( bSameMorph && bSameSkin )
            return;

        _mapBase.clear();
        _listBuiltMorph.assign( listMorphMesh.begin(), listMorphMesh.end() );
        _listBuiltMorphContentId.clear();
        for ( const Mesh* pMesh : listMorphMesh )
        {
            _listBuiltMorphContentId.push_back( pMesh != nullptr ? pMesh->getContentId() : 0u );
        }
        _listBuiltSkin.assign( listSkinMesh.begin(), listSkinMesh.end() );
        _listBuiltSkinContentId.clear();
        for ( const Mesh* pMesh : listSkinMesh )
        {
            _listBuiltSkinContentId.push_back( pMesh != nullptr ? pMesh->getContentId() : 0u );
        }

        // 모프 구간 — 레스트 정점을 한 줄로 잇는다. 구간 시작이 곧 그 메시의 base 다.
        vector<GPUMorphVertex> listRest;
        uint32                 morphCount = 0;
        for ( Mesh* pMesh : listMorphMesh )
        {
            if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                continue;
            const uint32 count = pMesh->getVertexCount();
            if ( morphCount + count > kMaxPoolVertices )
            {
                // 예산 초과. 이 메시는 풀에 넣지 않는다. `baseOf` 가 kInvalidBase 를 반환하고 셰이더는
                // 레스트 포즈로 그린다. 언리얼 스킨 캐시가 가득 차면 일반 경로로 되돌리는 것과 같다.
                SW_LOG_WARNING( "모프 풀이 가득 찼습니다(%# 정점) — 남은 메시는 레스트 포즈로 그립니다.", kMaxPoolVertices );
                continue;
            }
            _mapBase.emplace( pMesh, morphCount );
            if ( bSameMorph == false )
            {
                for ( const RHIVertex& vertex : pMesh->getVertices() )
                {
                    listRest.push_back( GPUMeshMorphPoolInternal::makeRestVertex( vertex ) );
                }
            }
            morphCount += count;
        }
        _skinVertexBase = morphCount;
        if ( bSameMorph == false )
            GPUMeshMorphPoolInternal::uploadAll( pDevice, _rest, listRest.data(), static_cast<uint32>( listRest.size() ) * shaderslot::kMorphFloat4PerVertex );

        // 스킨 원본(바뀐 때만 올린다)과 인스턴스 — 인스턴스는 결과 구간 · 원본 구간 · 팔레트 시작을 표에 한 줄씩 갖는다.
        rebuildSkinSources( pDevice, listSkinMesh );
        _listSkinMesh.clear();
        _listSkinRow.clear();
        _skinBoneCount        = 0;
        _skinMorphWeightCount = 0;
        uint32 resultOffset   = 0;
        for ( Mesh* pMesh : listSkinMesh )
        {
            if ( pMesh == nullptr || pMesh->hasSkin() == false || pMesh->getVertexCount() == 0 || _mapBase.find( pMesh ) != _mapBase.end() )
                continue;
            const auto sourceIt = std::find( _listSourceDataId.begin(), _listSourceDataId.end(), pMesh->getSkinDataId() );
            if ( sourceIt == _listSourceDataId.end() )
                continue;
            const uint32 count = pMesh->getVertexCount();
            if ( _skinVertexBase + resultOffset + count > kMaxPoolVertices )
            {
                SW_LOG_WARNING( "모프 풀이 가득 찼습니다(%# 정점) — 남은 스킨드 메시는 바인드 포즈로 그립니다.", kMaxPoolVertices );
                continue;
            }
            GPUSkinInstanceRow row{};
            row._resultOffset     = resultOffset;
            row._sourceBase       = _listSourceBase[static_cast<size_t>( sourceIt - _listSourceDataId.begin() )];
            row._vertexCount      = count;
            row._paletteBase      = _skinBoneCount;
            row._morphTargetCount = pMesh->getMorphTargetCount();
            row._morphWeightBase  = _skinMorphWeightCount; // 팔레트 뒤 구간 기준 — 팔레트 본 수가 정해진 뒤 아래에서 절대 위치로 옮긴다
            _mapBase.emplace( pMesh, _skinVertexBase + resultOffset );
            _listSkinMesh.push_back( pMesh );
            _listSkinRow.push_back( row );
            _skinBoneCount += pMesh->getSkinBoneCount();
            _skinMorphWeightCount += row._morphTargetCount;
            resultOffset += count;
        }
        // 모프 가중치는 팔레트 버퍼의 본 행 뒤에 float4 로 싣는다 — 셰이더는 그 버퍼를 float 배열로 본다(float4 셋 × 본 수 = float 열둘 × 본 수).
        for ( GPUSkinInstanceRow& row : _listSkinRow )
        {
            row._morphWeightBase += _skinBoneCount * shaderslot::kSkinFloat4PerBone * 4u;
        }
        _vertexCount = _skinVertexBase + resultOffset;

        if ( _vertexCount == 0 )
        {
            _morph.release( pDevice );
            _skinInstance.release( pDevice );
            _skinPalette.release( pDevice );
            return;
        }

        // 결과는 컴퓨트가 채우므로 초기값이 필요 없다 — 구간이 바뀌어도 올릴 것이 없다(용량만 맞춘다).
        _morph.ensureCapacity( pDevice, GPUMeshMorphPoolInternal::kElementStride, _vertexCount * shaderslot::kMorphFloat4PerVertex, GPUMeshMorphPoolInternal::kWriteUsage, true,
                               true, nullptr );
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _skinInstance, _listSkinRow.data(), static_cast<uint32>( _listSkinRow.size() ) * shaderslot::kSkinUint4PerInstance );
        if ( _skinBoneCount > 0 )
            _skinPalette.ensureCapacity( pDevice, GPUMeshMorphPoolInternal::kElementStride, _skinBoneCount * shaderslot::kSkinFloat4PerBone + ( _skinMorphWeightCount + 3u ) / 4u,
                                         GPUMeshMorphPoolInternal::kReadUsage, true, false, nullptr );
        else
            _skinPalette.release( pDevice );

        // UAV 를 못 받으면(백엔드 · 드라이버가 거절) 모프는 조용히 꺼진다. 그리기는 레스트 포즈로 살아 있다.
        if ( _morph._uav == kInvalidDescriptorIndex )
            SW_LOG_WARNING( "모프 결과 버퍼에 UAV 를 걸지 못했습니다 — 이 백엔드에서는 모프가 꺼집니다." );
    }

    void GPUMeshMorphPool::uploadSkinPalettes( IRHIDevice* pDevice, const vector<GPUSkinPalette>& listPalette, const vector<float4>* pListRow,
                                               const vector<float32>* pListMorphWeight )
    {
        if ( pDevice == nullptr || _skinBoneCount == 0 || _skinPalette.isValid() == false )
            return;

        // 메시 → 팔레트 항목 표를 한 번 짓는다 — 인스턴스마다 목록을 훑으면 캐릭터 천 명에서 백만 번 비교다.
        _mapScratchPaletteIndex.clear();
        for ( uint32 paletteIndex = 0; paletteIndex < static_cast<uint32>( listPalette.size() ); ++paletteIndex )
        {
            _mapScratchPaletteIndex.emplace( listPalette[paletteIndex]._pMesh, paletteIndex );
        }

        // 풀 순서로 다시 모은다. 팔레트가 없는(아직 평가되지 않은) 메시는 단위 행렬 — 바인드 포즈다.
        const size_t paletteElementCount = static_cast<size_t>( _skinBoneCount ) * shaderslot::kSkinFloat4PerBone;
        _listScratchPaletteRow.assign( paletteElementCount + ( _skinMorphWeightCount + 3u ) / 4u, float4{} );
        for ( size_t skinIndex = 0; skinIndex < _listSkinMesh.size(); ++skinIndex )
        {
            const Mesh*           pMesh     = _listSkinMesh[skinIndex];
            const uint32          boneBase  = _listSkinRow[skinIndex]._paletteBase;
            const uint32          boneCount = pMesh->getSkinBoneCount();
            const auto            found     = _mapScratchPaletteIndex.find( pMesh );
            const GPUSkinPalette* pFound    = ( found != _mapScratchPaletteIndex.end() ) ? &listPalette[found->second] : nullptr;
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                float4*      pRow     = &_listScratchPaletteRow[( static_cast<size_t>( boneBase ) + boneIndex ) * shaderslot::kSkinFloat4PerBone];
                const size_t rowStart = ( pFound != nullptr ) ? static_cast<size_t>( pFound->_firstRow ) + static_cast<size_t>( boneIndex ) * shaderslot::kSkinFloat4PerBone : 0u;
                const bool   bHasBone = pFound != nullptr && pListRow != nullptr && boneIndex < pFound->_boneCount && rowStart + shaderslot::kSkinFloat4PerBone <= pListRow->size();
                if ( bHasBone )
                {
                    for ( uint32 rowIndex = 0; rowIndex < shaderslot::kSkinFloat4PerBone; ++rowIndex )
                    {
                        pRow[rowIndex] = ( *pListRow )[rowStart + rowIndex];
                    }
                    continue;
                }
                pRow[0] = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
                pRow[1] = float4{ 0.0f, 1.0f, 0.0f, 0.0f };
                pRow[2] = float4{ 0.0f, 0.0f, 1.0f, 0.0f };
            }
            // 모프 가중치 — 팔레트 뒤 구간에 이 인스턴스 몫(타깃 수)만큼. 없으면 0(레스트).
            const GPUSkinInstanceRow& row = _listSkinRow[skinIndex];
            if ( row._morphTargetCount == 0 || pFound == nullptr || pListMorphWeight == nullptr )
                continue;
            float32* pWeight = reinterpret_cast<float32*>( _listScratchPaletteRow.data() ) + row._morphWeightBase;
            for ( uint32 targetIndex = 0; targetIndex < row._morphTargetCount && targetIndex < pFound->_morphWeightCount; ++targetIndex )
            {
                const size_t source = static_cast<size_t>( pFound->_firstMorphWeight ) + targetIndex;
                if ( source < pListMorphWeight->size() )
                    pWeight[targetIndex] = ( *pListMorphWeight )[source];
            }
        }
        _skinPalette.upload( pDevice, _listScratchPaletteRow.data(), static_cast<uint32>( _listScratchPaletteRow.size() * sizeof( float4 ) ) );
    }

    void GPUMeshMorphPool::release( IRHIDevice* pDevice )
    {
        _rest.release( pDevice );
        _morph.release( pDevice );
        _skinRest.release( pDevice );
        _skinWeight.release( pDevice );
        _skinInstance.release( pDevice );
        _skinPalette.release( pDevice );
        _mapBase.clear();
        _listBuiltMorph.clear();
        _listBuiltMorphContentId.clear();
        _listBuiltSkin.clear();
        _listBuiltSkinContentId.clear();
        _listSkinMesh.clear();
        _listSkinRow.clear();
        _listSourceDataId.clear();
        _listSourceBase.clear();
        _vertexCount           = 0;
        _skinVertexBase        = 0;
        _skinBoneCount         = 0;
        _skinSourceVertexCount = 0;
        _skinDeltaBase         = 0;
        _skinMorphWeightCount  = 0;
    }
} // namespace sw
