#include "pch.h"

#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Profiling/FrameProfiler.h"
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

            /** @brief 정점의 위치 · 노멀 · 탄젠트를 float4 셋으로 담습니다(레스트). */
            static GPUMorphVertex makeRestVertex( const RHIVertex& vertex )
            {
                GPUMorphVertex restVertex{};
                restVertex._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                // 레스트 노멀도 함께 올린다. 컴퓨트가 변형된 노멀을 만들려면 원래 노멀이 있어야 한다.
                restVertex._normal = float4{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2], 0.0f };
                // 탄젠트도 — 컴퓨트가 노멀과 같은 행렬로 돌린다. w(바이탄젠트 부호)는 변형으로 바뀌지 않는다.
                restVertex._tangent = float4{ vertex._arrTangent[0], vertex._arrTangent[1], vertex._arrTangent[2], vertex._arrTangent[3] };
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
        const auto morphIt = _mapMorphEntry.find( pMesh );
        if ( morphIt != _mapMorphEntry.end() && morphIt->second._base != kInvalidBase )
            return morphIt->second._base;
        const auto skinIt = _mapSkinBase.find( pMesh );
        return ( skinIt != _mapSkinBase.end() ) ? skinIt->second : kInvalidBase;
    }

    bool GPUMeshMorphPool::isSkinDispatchable() const
    {
        return getSkinVertexCount() > 0 && _skinBoneCount > 0 && _morph._uav != kInvalidDescriptorIndex && _skinRest._srv != kInvalidDescriptorIndex &&
               _skinWeight._srv != kInvalidDescriptorIndex && _skinInstance._srv != kInvalidDescriptorIndex && _skinPalette._srv != kInvalidDescriptorIndex;
    }

    void GPUMeshMorphPool::update( IRHIDevice* pDevice, const vector<Mesh*>& listMesh )
    {
        update( pDevice, listMesh, vector<Mesh*>{} );
    }

    bool GPUMeshMorphPool::isSameList( const vector<Mesh*>& listMesh, const vector<const Mesh*>& listBuilt, const vector<uint64>& listBuiltContentID )
    {
        // 포인터와 **내용 번호**를 함께 본다(GPUMeshVertexPool::update 와 같은 이유 — 지워진 자리에 새 메시가 생기거나 정점을 바꾼 메시).
        if ( listBuilt.size() != listMesh.size() )
            return false;
        for ( size_t index = 0; index < listMesh.size(); ++index )
        {
            const Mesh* pMesh           = listMesh[index];
            const bool  bStaleBuiltMesh = listBuilt[index] != pMesh || pMesh == nullptr || listBuiltContentID[index] != pMesh->getContentID();
            if ( bStaleBuiltMesh )
                return false;
        }
        return true;
    }

    void GPUMeshMorphPool::rebuildSkinSources( IRHIDevice* pDevice, const vector<Mesh*>& listSkinMesh )
    {
        // 원본 = 스킨 데이터 번호. 사본(`Mesh::createSkinInstance`)은 원본의 번호를 나누므로 레스트 · 가중치가 한 벌만 올라간다.
        vector<uint64>      listDataID;
        vector<const Mesh*> listSource;
        for ( const Mesh* pMesh : listSkinMesh )
        {
            const bool bSkipSkinMesh = pMesh == nullptr || pMesh->hasSkin() == false || pMesh->getVertexCount() == 0;
            if ( bSkipSkinMesh )
                continue;
            const uint64 dataID = pMesh->getSkinDataID();
            if ( VectorUtil::contains( listDataID, dataID ) )
                continue;
            listDataID.push_back( dataID );
            listSource.push_back( pMesh );
        }
        if ( listDataID == _listSourceDataID && _skinRest.isValid() == ( listDataID.empty() == false ) )
            return;

        // 스킨 원본 전체를 다시 올리는 구간이다. 이 시간이 SpanAllocator 구간 갱신으로 바꿀 이득의 측정값이다(집합이 바뀐 프레임에만 찍힌다).
        SW_PROFILE_SCOPE( "RT.Deform.rebuildSkinSources" );
        _listSourceDataID = listDataID;
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
        // 레스트 버퍼 = [원본 레스트 정점][모프 차이] — 둘 다 GPUMorphVertex(float4 셋)라 한 버퍼에 잇는다(컴퓨트 SRV 슬롯은 넷뿐이다). 차이의 탄젠트 칸은 비어 있다(탄젠트 차이는 없다).
        _skinDeltaBase = static_cast<uint32>( listRest.size() ) * shaderslot::kMorphFloat4PerVertex;
        listRest.insert( listRest.end(), listDelta.begin(), listDelta.end() );
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _skinRest, listRest.data(), static_cast<uint32>( listRest.size() ) * shaderslot::kMorphFloat4PerVertex );
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _skinWeight, listWeight.data(), static_cast<uint32>( listWeight.size() ) );
    }

    void GPUMeshMorphPool::update( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh, const vector<Mesh*>& listSkinMesh )
    {
        if ( pDevice == nullptr )
            return;

        // 번호 = 이 호출 수(프레임마다 한 번). 돌려받은 모프 구간은 kMaxFrameCountInFlight 프레임 뒤에 다시 나간다(GPUMeshVertexPool::update 와 같은 규칙).
        ++_updateCount;
        _morphAllocator.retireDeferred( _updateCount );
        _lastMorphUploadBytes = 0;

        // 모프 구간은 바뀐 메시만 올린다. 레스트 포즈 · 스킨 가중치는 변하지 않으므로 **한 번만** 올린다 — 매 프레임 올리면 이 클래스가 없애려던
        // 바로 그 비용(정점 재업로드)을 다시 치르게 된다.
        const bool bMorphChanged = updateMorphMeshes( pDevice, listMorphMesh );
        const bool bSameSkin     = isSameList( listSkinMesh, _listBuiltSkin, _listBuiltSkinContentID );
        if ( bMorphChanged == false && bSameSkin )
            return;

        _listBuiltSkin.assign( listSkinMesh.begin(), listSkinMesh.end() );
        _listBuiltSkinContentID.clear();
        for ( const Mesh* pMesh : listSkinMesh )
        {
            _listBuiltSkinContentID.push_back( pMesh != nullptr ? pMesh->getContentID() : 0u );
        }

        // 스킨 구간은 모프 **용량** 뒤에서 시작한다 — 모프 메시가 들고 나도 용량이 그대로면 스킨 인스턴스의 시작이 그대로다.
        _skinVertexBase = _morphAllocator.getCapacity();

        // 스킨 원본(바뀐 때만 올린다)과 인스턴스 — 인스턴스는 결과 구간 · 원본 구간 · 팔레트 시작을 표에 한 줄씩 갖는다.
        rebuildSkinSources( pDevice, listSkinMesh );
        _mapSkinBase.clear();
        _listSkinMesh.clear();
        _listSkinRow.clear();
        _skinBoneCount        = 0;
        _skinMorphWeightCount = 0;
        uint32 resultOffset   = 0;
        for ( Mesh* pMesh : listSkinMesh )
        {
            const auto morphIt   = ( pMesh != nullptr ) ? _mapMorphEntry.find( pMesh ) : _mapMorphEntry.end();
            const bool bInMorph  = morphIt != _mapMorphEntry.end() && morphIt->second._base != kInvalidBase;
            const bool bSkipMesh = pMesh == nullptr || pMesh->hasSkin() == false || pMesh->getVertexCount() == 0 || bInMorph ||
                                   _mapSkinBase.find( pMesh ) != _mapSkinBase.end();
            if ( bSkipMesh )
                continue;
            const std::optional<size_t> sourceIndex = VectorUtil::findIndex( _listSourceDataID, pMesh->getSkinDataID() );
            if ( sourceIndex.has_value() == false )
                continue;
            const uint32 count = pMesh->getVertexCount();
            if ( _skinVertexBase + resultOffset + count > kMaxPoolVertices )
            {
                SW_LOG_WARNING( "The morph pool is full (%# vertices) - the remaining skinned meshes are drawn in the bind pose.", kMaxPoolVertices );
                continue;
            }
            GPUSkinInstanceRow row{};
            row._resultOffset     = resultOffset;
            row._sourceBase       = _listSourceBase[*sourceIndex];
            row._vertexCount      = count;
            row._paletteBase      = _skinBoneCount;
            row._morphTargetCount = pMesh->getMorphTargetCount();
            row._morphWeightBase  = _skinMorphWeightCount; // 팔레트 뒤 구간 기준 — 팔레트 본 수가 정해진 뒤 아래에서 절대 위치로 옮긴다
            _mapSkinBase.emplace( pMesh, _skinVertexBase + resultOffset );
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
            SW_LOG_WARNING( "Failed to bind a UAV to the morph result buffer - morphing is off on this backend." );
    }

    bool GPUMeshMorphPool::updateMorphMeshes( IRHIDevice* pDevice, const vector<Mesh*>& listMorphMesh )
    {
        const uint64 retireStamp   = _updateCount + constant::kMaxFrameCountInFlight;
        const bool   bMarkedChange = markListedMorphMeshes( listMorphMesh, retireStamp );
        const bool   bChanged      = sweepUnlistedMorphMeshes( retireStamp ) || bMarkedChange;
        if ( _mapMorphEntry.empty() )
        {
            // 모프 메시가 없다 — 모프 구간을 통째로 놓는다. 스킨 구간은 0 에서 시작한다.
            const bool bHadMorph = _rest.isValid() || _morphAllocator.getCapacity() > 0;
            _rest.release( pDevice );
            _morphAllocator.reset( 0 );
            _listPoolMorph.clear();
            _listFreeMorphSlot.clear();
            _listDeformProgramGeneration.clear();
            _morphLiveVertexCount = 0;
            return bChanged || bHadMorph;
        }
        if ( bChanged == false )
            return false;

        // 칸이 바뀌었을 수 있다 — 다음 updateDeformPrograms 가 프로그램 표를 다시 올린다(같은 칸에 다른 메시가 들어와 번호가 우연히 같을 수 있다).
        _listDeformProgramGeneration.clear();
        // 예산 때문에 못 들어간 메시는 집합이 바뀐 프레임에만 다시 시도한다.
        _listScratchAdded.insert( _listScratchAdded.end(), _listScratchRetry.begin(), _listScratchRetry.end() );

        // 3) 들어온 메시에 구간과 칸을 준다. 하나라도 자리가 없으면 새 버퍼로 다시 채운다.
        if ( _rest.isValid() == false || placeAddedMorphMeshes( pDevice ) == false )
            repackMorph( pDevice );
        return true;
    }

    bool GPUMeshMorphPool::markListedMorphMeshes( const vector<Mesh*>& listMorphMesh, uint64 retireStamp )
    {
        // 1) 표시: 처음 보거나 내용이 바뀐 메시는 새 구간이 필요하다.
        bool bChanged = false;
        _listScratchAdded.clear();
        _listScratchRetry.clear();
        for ( Mesh* pMesh : listMorphMesh )
        {
            if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                continue;
            const uint64 contentID = pMesh->getContentID();
            const uint32 count     = pMesh->getVertexCount();
            const auto   it        = _mapMorphEntry.find( pMesh );
            if ( it == _mapMorphEntry.end() )
            {
                _mapMorphEntry.emplace( pMesh, MorphEntry{ contentID, _updateCount, kInvalidBase, count, kInvalidBase } );
                _listScratchAdded.push_back( pMesh );
                bChanged = true;
                continue;
            }
            MorphEntry& entry = it->second;
            if ( entry._seenStamp == _updateCount )
                continue; // 같은 메시가 목록에 또 있다
            entry._seenStamp = _updateCount;
            if ( entry._contentID == contentID )
            {
                if ( entry._base == kInvalidBase )
                    _listScratchRetry.push_back( pMesh ); // 예산 때문에 못 들어간 메시
                continue;
            }
            // 포인터는 같고 내용이 다르다(지워진 자리에 새 메시, 또는 setVertices) — 옛 구간과 칸을 돌려주고 새로 받는다.
            releaseMorphEntry( entry, retireStamp );
            entry._contentID = contentID;
            entry._count     = count;
            _listScratchAdded.push_back( pMesh );
            bChanged = true;
        }
        return bChanged;
    }

    bool GPUMeshMorphPool::sweepUnlistedMorphMeshes( uint64 retireStamp )
    {
        // 2) 쓸기: 이번 목록에 없는 메시의 구간과 칸을 돌려준다. 포인터는 따라가지 않는다.
        _listScratchRemoved.clear();
        for ( const auto& [pMesh, entry] : _mapMorphEntry )
        {
            if ( entry._seenStamp != _updateCount )
                _listScratchRemoved.push_back( pMesh );
        }
        for ( const Mesh* pMesh : _listScratchRemoved )
        {
            releaseMorphEntry( _mapMorphEntry.find( pMesh )->second, retireStamp );
            _mapMorphEntry.erase( pMesh );
        }
        return _listScratchRemoved.empty() == false;
    }

    bool GPUMeshMorphPool::placeAddedMorphMeshes( IRHIDevice* pDevice )
    {
        _listScratchRest.clear();
        _listScratchRegion.clear();
        for ( const Mesh* pMesh : _listScratchAdded )
        {
            MorphEntry&  entry = _mapMorphEntry.find( pMesh )->second;
            const uint32 base  = _morphAllocator.allocate( entry._count );
            if ( base == SpanAllocator::kInvalidOffset )
                return false;
            entry._base = base;
            entry._slot = allocateFreeListIndex( _listPoolMorph, _listFreeMorphSlot, pMesh );
            _morphLiveVertexCount += entry._count;
            RHIBufferCopyRegion region{};
            region._srcOffset = static_cast<uint32>( _listScratchRest.size() * sizeof( GPUMorphVertex ) );
            region._dstOffset = base * static_cast<uint32>( sizeof( GPUMorphVertex ) );
            region._sizeBytes = entry._count * static_cast<uint32>( sizeof( GPUMorphVertex ) );
            _listScratchRegion.push_back( region );
            appendRestVertices( *pMesh, entry._slot );
        }
        if ( _listScratchRegion.empty() == false )
        {
            pDevice->getResourceFactory()->updateStructuredBufferRegions( _rest._buffer, _listScratchRest.data(), _listScratchRegion.data(),
                                                                          static_cast<uint32>( _listScratchRegion.size() ) );
            _lastMorphUploadBytes = static_cast<uint32>( _listScratchRest.size() * sizeof( GPUMorphVertex ) );
        }
        return true;
    }

    void GPUMeshMorphPool::repackMorph( IRHIDevice* pDevice )
    {
        // 새 레스트 버퍼는 아무 프레임도 읽지 않는다 — 대기 중인 반납까지 버리고 살아 있는 메시를 앞에서부터 잇는다. 칸 번호는 그대로 둔다.
        uint32 liveCount = 0;
        for ( const auto& [pMesh, entry] : _mapMorphEntry )
        {
            liveCount += entry._count;
        }
        uint32 capacity = liveCount + liveCount / 2u;
        if ( capacity < kMinMorphVertices )
            capacity = kMinMorphVertices;
        if ( capacity > kMaxPoolVertices )
            capacity = kMaxPoolVertices;

        _morphAllocator.reset( capacity );
        _morphLiveVertexCount = 0;
        ++_morphRepackCount;

        // 빈 자리는 0(노멀 w = 0 → 칸 없음)으로 둔다. 컴퓨트는 빈 자리도 돌지만 그 결과는 아무도 읽지 않는다.
        _listScratchRest.assign( capacity, GPUMorphVertex{} );
        bool bWarnedFull = false;
        for ( auto& [pMesh, entry] : _mapMorphEntry )
        {
            entry._base = _morphAllocator.allocate( entry._count );
            if ( entry._base == SpanAllocator::kInvalidOffset )
            {
                // 예산 초과. 이 메시는 풀에 넣지 않는다 — `baseOf` 가 kInvalidBase 를 반환하고 셰이더는 레스트 포즈로 그린다.
                entry._base = kInvalidBase;
                if ( entry._slot != kInvalidBase )
                {
                    (void)releaseFreeListIndex( _listPoolMorph, _listFreeMorphSlot, entry._slot ); // 비우기 전 값(메시)은 쓰지 않는다
                    entry._slot = kInvalidBase;
                }
                if ( bWarnedFull == false )
                    SW_LOG_WARNING( "The morph pool is full (%# vertices) - the remaining meshes are drawn in the rest pose.", kMaxPoolVertices );
                bWarnedFull = true;
                continue;
            }
            if ( entry._slot == kInvalidBase )
                entry._slot = allocateFreeListIndex( _listPoolMorph, _listFreeMorphSlot, pMesh );
            // 맵의 메시는 모두 이번 update 에서 본 것이라 살아 있다(못 본 것은 쓸기에서 지웠다).
            const vector<RHIVertex>& listVertex = pMesh->getVertices();
            const float32            slotTag    = static_cast<float32>( entry._slot + 1u ); // float 로 정확한 정수(2^24 미만) - 비트 재해석은 비정규 수로 지워질 수 있다
            for ( uint32 vertexIndex = 0; vertexIndex < entry._count; ++vertexIndex )
            {
                GPUMorphVertex restVertex                                          = GPUMeshMorphPoolInternal::makeRestVertex( listVertex[vertexIndex] );
                restVertex._normal._w                                              = slotTag;
                _listScratchRest[static_cast<size_t>( entry._base ) + vertexIndex] = restVertex;
            }
            _morphLiveVertexCount += entry._count;
        }
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _rest, _listScratchRest.data(), capacity * shaderslot::kMorphFloat4PerVertex );
        _lastMorphUploadBytes = capacity * static_cast<uint32>( sizeof( GPUMorphVertex ) );
    }

    void GPUMeshMorphPool::releaseMorphEntry( MorphEntry& inoutEntry, uint64 retireStamp )
    {
        if ( inoutEntry._base != kInvalidBase )
        {
            _morphAllocator.freeDeferred( inoutEntry._base, inoutEntry._count, retireStamp );
            _morphLiveVertexCount -= inoutEntry._count;
        }
        if ( inoutEntry._slot != kInvalidBase )
            (void)releaseFreeListIndex( _listPoolMorph, _listFreeMorphSlot, inoutEntry._slot ); // 비우기 전 값(메시)은 쓰지 않는다
        inoutEntry._base = kInvalidBase;
        inoutEntry._slot = kInvalidBase;
    }

    void GPUMeshMorphPool::appendRestVertices( const Mesh& mesh, uint32 slot )
    {
        // 레스트 노멀의 w 는 칸 번호 + 1 이다 - 컴퓨트가 프로그램 버퍼의 칸 표에서 그 메시의 변형 프로그램을 찾는다(updateDeformPrograms).
        const float32 slotTag = static_cast<float32>( slot + 1u ); // float 로 정확한 정수(2^24 미만) - 비트 재해석은 비정규 수로 지워질 수 있다
        for ( const RHIVertex& vertex : mesh.getVertices() )
        {
            GPUMorphVertex restVertex = GPUMeshMorphPoolInternal::makeRestVertex( vertex );
            restVertex._normal._w     = slotTag;
            _listScratchRest.push_back( restVertex );
        }
    }

    void GPUMeshMorphPool::updateDeformPrograms( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        if ( _listPoolMorph.empty() )
        {
            _deformProgram.release( pDevice );
            _listDeformProgramGeneration.clear();
            return;
        }
        // 칸마다 번호만 본다(프로그램을 바꾸지 않는 프레임은 잠금도 없다). 하나라도 다르면 표와 프로그램을 통째로 다시 올린다 - 미리보기 몇 개 규모다.
        bool bSame = _listDeformProgramGeneration.size() == _listPoolMorph.size() && _deformProgram.isValid();
        // 나간 메시의 칸은 nullptr 이다 - 번호 대신 kUint64 를 둔다(그 칸의 표 값은 -1, 프로그램 없음).
        for ( size_t slot = 0; slot < _listPoolMorph.size() && bSame; ++slot )
        {
            const Mesh*  pMesh      = _listPoolMorph[slot];
            const uint64 generation = ( pMesh != nullptr ) ? pMesh->getGPUDeformProgramGeneration() : invalid_index::kUint64;
            bSame                   = _listDeformProgramGeneration[slot] == generation;
        }
        if ( bSame )
            return;

        _listDeformProgramGeneration.resize( _listPoolMorph.size() );
        _listScratchDeformProgram.assign( _listPoolMorph.size(), float4{ -1.0f, 0.0f, 0.0f, 0.0f } );
        for ( size_t slot = 0; slot < _listPoolMorph.size(); ++slot )
        {
            const Mesh* pMesh = _listPoolMorph[slot];
            if ( pMesh == nullptr )
            {
                _listDeformProgramGeneration[slot] = invalid_index::kUint64;
                continue;
            }
            _listDeformProgramGeneration[slot]             = pMesh->getGPUDeformProgramGeneration(); // 사본보다 먼저 읽는다 - 사이에 바뀌면 다음 프레임에 다시 올린다
            const shared_ptr<const vector<float4>> program = pMesh->getGPUDeformProgram();
            if ( program == nullptr || program->empty() )
                continue;
            _listScratchDeformProgram[slot]._x = static_cast<float32>( _listScratchDeformProgram.size() );
            _listScratchDeformProgram.insert( _listScratchDeformProgram.end(), program->begin(), program->end() );
        }
        GPUMeshMorphPoolInternal::uploadAll( pDevice, _deformProgram, _listScratchDeformProgram.data(), static_cast<uint32>( _listScratchDeformProgram.size() ) );
    }

    void GPUMeshMorphPool::uploadSkinPalettes( IRHIDevice* pDevice, const vector<GPUSkinPalette>& listPalette, const vector<float4>* pListRow,
                                               const vector<float32>* pListMorphWeight )
    {
        const bool bNothingToUploadPalette = pDevice == nullptr || _skinBoneCount == 0 || _skinPalette.isValid() == false;
        if ( bNothingToUploadPalette )
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
            const GPUSkinInstanceRow& row             = _listSkinRow[skinIndex];
            const bool                bNoMorphWeights = row._morphTargetCount == 0 || pFound == nullptr || pListMorphWeight == nullptr;
            if ( bNoMorphWeights )
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
        _deformProgram.release( pDevice );
        _listPoolMorph.clear();
        _listDeformProgramGeneration.clear();
        _listFreeMorphSlot.clear();
        _mapMorphEntry.clear();
        _morphAllocator.reset( 0 );
        _mapSkinBase.clear();
        _morphLiveVertexCount = 0;
        _listBuiltSkin.clear();
        _listBuiltSkinContentID.clear();
        _listSkinMesh.clear();
        _listSkinRow.clear();
        _listSourceDataID.clear();
        _listSourceBase.clear();
        _vertexCount           = 0;
        _skinVertexBase        = 0;
        _skinBoneCount         = 0;
        _skinSourceVertexCount = 0;
        _skinDeltaBase         = 0;
        _skinMorphWeightCount  = 0;
    }
} // namespace sw
