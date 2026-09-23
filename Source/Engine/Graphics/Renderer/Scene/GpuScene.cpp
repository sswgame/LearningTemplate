#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuScene.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuMeshMorphPool.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "GpuScene" );

    namespace
    {
        struct GpuSceneInternal
        {
            static void applyInstanceCbsVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._materialInstance == nullptr )
                        continue;
                    if ( batch._materialInstance->updateRhi( pDevice ) )
                        batch._materialCb = batch._materialInstance->getDescriptorIndex();
                }
            }

            static void uploadMeshesVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._mesh != nullptr && batch._mesh->initRhi( pDevice ) )
                        batch._vertexBuffer = batch._mesh->getVertexBuffer();
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GpuScene::clear()
    {
        // 스냅샷이 든 소유(메시 · 머티리얼 · 인스턴스)를 여기서 놓는다. 디바이스보다 먼저 불러야 한다(Graphics/README "소유와 수명" 4).
        _snapshot             = GpuSceneSnapshot{};
        _indirectCommandCount = 0;
        _bBatchTablesDirty    = SW_TRUE;
    }

    void GpuScene::setVertexPoolEnabled( bool bEnabled )
    {
        const uint8 value = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bVertexPoolEnabled == value )
            return;
        _bVertexPoolEnabled = value;
        _bBatchTablesDirty  = SW_TRUE; // startVertex 가 바뀐다. 표와 간접 인자를 다시 올린다
    }

    void GpuScene::assignMorphBases( const GpuMeshMorphPool& pool )
    {
        // 세 목록이 같은 배치를 각자 복사해 들고 있다. 하나만 채우면 그 목록으로 나가는 드로우만 모프된다.
        auto assign = [this, &pool]( vector<GpuMeshBatch>& listBatch )
        {
            for ( GpuMeshBatch& batch : listBatch )
            {
                const uint32 base = pool.baseOf( batch._mesh.get() );
                if ( batch._morphVertexBase != base )
                    _bBatchTablesDirty = SW_TRUE; // 표가 이 값을 든다. 바뀌면 다시 올린다
                batch._morphVertexBase = base;
            }
        };
        assign( _snapshot._listOpaqueBatch );
        assign( _snapshot._listTransparentBatch );
        assign( _snapshot._listAllBatch );
    }

    bool GpuScene::upload( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GpuScene.upload" );

        if ( pDevice == nullptr || _snapshot.getInstances().empty() || _snapshot._listAllBatch.empty() )
            return false;

        // RT 에서만 하는 두 가지: 머티리얼 인스턴스의 오버라이드를 CB 에 반영하고(updateRhi), 메시를 GPU 에 올린다(initRhi).
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.applyInstanceCbs" );
            GpuSceneInternal::applyInstanceCbsVal( pDevice, _snapshot._listAllBatch );
        }
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.uploadMeshes" );
            GpuSceneInternal::uploadMeshesVal( pDevice, _snapshot._listAllBatch );
        }
        // 정점 풀: 배치 메시 모두를 한 정점 버퍼에 모은다. 집합이 같으면 아무것도 하지 않는다. 풀에 든 배치는 풀 버퍼와 시작
        // 오프셋으로 그리고(같은 버퍼 → 멀티 드로우로 묶인다), 못 든 배치는 자기 정점 버퍼(오프셋 0)로 그린다.
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.vertexPool" );
            _listScratchPoolMesh.clear();
            if ( _bVertexPoolEnabled != SW_FALSE )
            {
                _listScratchPoolMesh.reserve( _snapshot._listAllBatch.size() );
                for ( const GpuMeshBatch& batch : _snapshot._listAllBatch )
                    _listScratchPoolMesh.push_back( batch._mesh.get() );
            }
            // 끈 상태면 빈 목록으로 빌드해 풀이 비워진다. 배치는 아래에서 자기 정점 버퍼(오프셋 0)로 돌아간다.
            if ( _vertexPool.build( pDevice, _listScratchPoolMesh ) )
                _bBatchTablesDirty = SW_TRUE;
            for ( GpuMeshBatch& batch : _snapshot._listAllBatch )
            {
                const uint32 base = _vertexPool.baseOf( batch._mesh.get() );
                if ( base != GpuMeshVertexPool::kInvalidBase && _vertexPool.getVertexBuffer() != 0 )
                {
                    batch._vertexBuffer = _vertexPool.getVertexBuffer();
                    batch._firstVertex  = base;
                }
                else
                    batch._firstVertex = 0;
            }
        }
        // 머티리얼 데이터 구조버퍼(셰이더 타입별). 값이 프레임마다 바뀔 수 있어 스냅샷 dirty 와 무관하게 매번 본다(바이트가 같으면 올리지 않는다).
        uploadMaterialGroups( pDevice );

        const size_t opaqueCount = _snapshot._listOpaqueBatch.size();
        for ( size_t batchIndex = 0; batchIndex < opaqueCount; ++batchIndex )
        {
            _snapshot._listOpaqueBatch[batchIndex]._materialCb   = _snapshot._listAllBatch[batchIndex]._materialCb;
            _snapshot._listOpaqueBatch[batchIndex]._vertexBuffer = _snapshot._listAllBatch[batchIndex]._vertexBuffer;
            _snapshot._listOpaqueBatch[batchIndex]._firstVertex  = _snapshot._listAllBatch[batchIndex]._firstVertex;
        }
        for ( size_t batchIndex = 0; batchIndex < _snapshot._listTransparentBatch.size(); ++batchIndex )
        {
            _snapshot._listTransparentBatch[batchIndex]._materialCb   = _snapshot._listAllBatch[opaqueCount + batchIndex]._materialCb;
            _snapshot._listTransparentBatch[batchIndex]._vertexBuffer = _snapshot._listAllBatch[opaqueCount + batchIndex]._vertexBuffer;
            _snapshot._listTransparentBatch[batchIndex]._firstVertex  = _snapshot._listAllBatch[opaqueCount + batchIndex]._firstVertex;
        }

        // CPU 스냅샷이 그대로면 인스턴스 버퍼 재업로드를 생략한다. **간접 인자는 예외다.**
        // 컬링 컴퓨트가 개수를 InterlockedAdd 로 만드는 동안에는 매 프레임 0 으로 되돌려 놓아야 한다.
        // 여기서 같이 건너뛰었더니 정적 씬에서 개수가 N, 2N, 3N ... 으로 끝없이 자랐다(드로우 비용이
        // 계속 늘고, 이번 프레임에 쓰지 않은 가시 목록 자리를 읽는다). 움직이는 벤치와 한 프레임만
        // 그리는 테스트가 둘 다 이것을 가리고 있었다.
        // 배치 표 · 간접 인자는 스냅샷이 그대로여도 풀 오프셋이 바뀌면(_bBatchTablesDirty) 다시 올린다.
        const bool bCpuDirty = _snapshot._bCpuDirty != SW_FALSE || _instances._buffer == 0 || getIndirectArgsBuffer() == 0;
        if ( bCpuDirty == false && _bBatchTablesDirty == SW_FALSE )
        {
            if ( _bGpuFillsIndirectCounts != SW_FALSE )
                refreshIndirectCounts( pDevice );
            return true;
        }

        const uint32 instanceCount = static_cast<uint32>( _snapshot.getInstances().size() );
        const uint32 argsCount     = static_cast<uint32>( _snapshot._listAllBatch.size() );

        if ( bCpuDirty )
        {
            // UnorderedAccess 를 함께 요구한다. instanceanim 컴퓨트가 월드 행렬을 고쳐 쓴다. 못 만드는
            // 백엔드 · 드라이버면 슬롯이 SRV 전용으로 한 번 더 시도해 그리기는 그대로 살린다.
            constexpr RHIBufferUsage kInstanceUsage =
                RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            {
                SW_PROFILE_SCOPE( "RT.GpuScene.instanceBuffer" );
                // 버퍼가 새로 만들어졌는지 봐야 한다. 새 버퍼에는 아직 아무것도 없으므로 구간만
                // 올리면 나머지가 쓰레기다. 핸들이 바뀌었으면 다시 만들어진 것이다.
                const RHIBufferHandle bufferBefore = _instances._buffer;
                if ( _instances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( GpuInstance ) ), instanceCount, kInstanceUsage, true, true,
                                                _snapshot.getInstances().data() ) )
                {
                    constexpr uint32   kStride    = static_cast<uint32>( sizeof( GpuInstance ) );
                    const GpuInstance* pSource    = _snapshot.getInstances().data();
                    const bool         bRecreated = ( _instances._buffer != bufferBefore );

                    if ( bRecreated || _snapshot._bAllInstancesDirty != SW_FALSE || _snapshot._listDirtyInstanceRun.empty() )
                    {
                        _instances.upload( pDevice, pSource, instanceCount * kStride );
                    }
                    else
                    {
                        // **바뀐 구간만, 그리고 한 번의 호출로 올린다.** 구간마다 따로 부르면 백엔드가
                        // 스테이징 확보와 큐 제출을 그만큼 되풀이한다(DX12 에서 호출당 ~3.3 us).
                        _listScratchCopyRegion.clear();
                        _listScratchCopyRegion.reserve( _snapshot._listDirtyInstanceRun.size() );
                        for ( const GpuInstanceRun& run : _snapshot._listDirtyInstanceRun )
                        {
                            if ( run._count == 0 || run._start >= instanceCount )
                                continue;
                            const uint32 count = MathUtil::min( run._count, instanceCount - run._start );
                            _listScratchCopyRegion.push_back( RHIBufferCopyRegion{ run._start * kStride, run._start * kStride, count * kStride } );
                        }
                        if ( _listScratchCopyRegion.empty() == false )
                        {
                            pDevice->getResource()->updateStructuredBufferRegions( _instances._buffer, pSource,
                                                                                   _listScratchCopyRegion.data(),
                                                                                   static_cast<uint32>( _listScratchCopyRegion.size() ) );
                        }
                    }
                }
            }

            // 가시 인스턴스 ID 버퍼. 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해
            // 넣고(언리얼 FInstanceCullingContext 의 InstanceIdBuffer), 정점 셰이더가 그 순서로 읽는다.
            // **뷰마다 하나씩**이다. 목록은 절두체에 종속이라 메인 카메라로 거른 것을 그림자가 쓰면 안 된다.
            constexpr RHIBufferUsage kVisibleUsage =
                RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            SW_PROFILE_SCOPE( "RT.GpuScene.visibleBuffers" );
            for ( GpuCullViewResources& view : _arrCullView )
            {
                view._visibleInstances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), instanceCount, kVisibleUsage, true,
                                                       true, nullptr );
            }
        }

        SW_PROFILE_SCOPE( "RT.GpuScene.batchTables" );

        // 배치 표: 컬링 컴퓨트는 "이 배치의 인스턴스는 어디서 시작하나" 를, 정점 셰이더는 모프 · 정점 풀 오프셋을 읽는다.
        _listScratchBatchInfo.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            const GpuMeshBatch& infoBatch                    = _snapshot._listAllBatch[argIndex];
            _listScratchBatchInfo[argIndex]._instanceBase    = infoBatch._instanceBase;
            _listScratchBatchInfo[argIndex]._instanceCount   = infoBatch._instanceCount;
            _listScratchBatchInfo[argIndex]._morphVertexBase = infoBatch._morphVertexBase;
            _listScratchBatchInfo[argIndex]._firstVertex     = infoBatch._firstVertex;
            // 투명은 압축한 뒤 GPU 가 깊이순으로 다시 정렬한다. 한 워크그룹에 안 담기는 큰 배치만
            // 압축을 포기하고 CPU 가 정렬해 둔 순서를 그대로 쓴다.
            GpuBatchSortMode sortMode = GpuBatchSortMode::None;
            if ( infoBatch._blendMode == RHIBlendMode::Transparent )
                sortMode = ( infoBatch._instanceCount <= kGpuSortMaxElements ) ? GpuBatchSortMode::DepthGpu : GpuBatchSortMode::Preserve;
            _listScratchBatchInfo[argIndex]._sortMode = static_cast<uint32>( sortMode );
        }
        constexpr RHIBufferUsage kBatchInfoUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        if ( _batchInfo.ensureCapacity( pDevice, static_cast<uint32>( sizeof( GpuBatchInfo ) ), argsCount, kBatchInfoUsage, true, false,
                                        _listScratchBatchInfo.data() ) )
            _batchInfo.upload( pDevice, _listScratchBatchInfo.data(), argsCount * static_cast<uint32>( sizeof( GpuBatchInfo ) ) );

        // 컴퓨트가 개수를 만들려면 **가시 목록과 배치 구간이 둘 다** 있어야 한다. 하나라도 없으면 개수를
        // 0 으로 올리면 안 된다. 컬링이 못 도는데 개수가 0 이면 그 프레임은 아무것도 안 그려진다.
        // 그래서 "원한다"(_bWantGpuIndirectCounts)와 "실제로 된다"(_bGpuFillsIndirectCounts)를 나눠 둔다.
        _bGpuFillsIndirectCounts =
            ( _bWantGpuIndirectCounts != SW_FALSE && _batchInfo._buffer != 0 && argsCount > 0 && hasAllVisibleBuffers() ) ? 1u : 0u;

        _listScratchIndirectCmd.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            RHIDrawIndirectCommand& cmd = _listScratchIndirectCmd[argIndex];
            cmd._vertexCount            = _snapshot._listAllBatch[argIndex]._vertexCount;
            // 컬링 컴퓨트가 개수를 만드는 배치는 **0 에서 시작**해야 한다. InterlockedAdd 로 보이는 것만 센다.
            // 압축을 포기한 배치(Preserve)와 컬링이 아예 없을 때는 CPU 가 센 개수를 그대로 쓴다.
            const bool bPreserve  = static_cast<GpuBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GpuBatchSortMode::Preserve;
            const bool bGpuCounts = ( _bGpuFillsIndirectCounts != SW_FALSE ) && ( bPreserve == false );
            cmd._instanceCount    = bGpuCounts ? 0u : _snapshot._listAllBatch[argIndex]._instanceCount;
            // 정점 풀 안의 시작. 입력 어셈블러가 그 구간을 읽는다(SV_VertexID 가 이 값을 포함하는지는 API 마다 다르다: binding.hlsli).
            cmd._startVertexLocation = _snapshot._listAllBatch[argIndex]._firstVertex;
            // 배치의 인스턴스 시작. 인스턴스 슬롯 스트림(슬롯 1)의 원소를 그만큼 건너뛴다. 셰이더는 SV_InstanceID 를 쓰지 않으므로
            // "SPIR-V InstanceIndex 는 포함하고 DX 는 안 한다" 는 차이에 더 이상 기대지 않는다. 입력 어셈블러의 인스턴스 스텝
            // 스트림은 네 API 모두 startInstance 부터 읽는다.
            cmd._startInstanceLocation = _snapshot._listAllBatch[argIndex]._instanceBase;
        }

        // 인스턴스 슬롯 스트림: 항등 배열이다. 인스턴스 수만큼 커지면 다시 만든다.
        if ( _instanceSlotStreamCapacity < instanceCount || _instanceSlotStream == 0 )
        {
            if ( _instanceSlotStream != 0 )
                pDevice->getResource()->destroyBuffer( _instanceSlotStream );
            const uint32   capacity = MathUtil::max( instanceCount, 256u );
            vector<uint32> listSlot( capacity );
            for ( uint32 slotIndex = 0; slotIndex < capacity; ++slotIndex )
                listSlot[slotIndex] = slotIndex;
            _instanceSlotStream         = pDevice->getResource()->createVertexBuffer( listSlot.data(), capacity * static_cast<uint32>( sizeof( uint32 ) ) );
            _instanceSlotStreamCapacity = ( _instanceSlotStream != 0 ) ? capacity : 0;
            if ( _instanceSlotStream == 0 )
                SW_LOG_ERROR( "인스턴스 슬롯 스트림을 만들지 못했습니다(%# 인스턴스) — 씬 드로우가 인스턴스를 찾지 못합니다.", capacity );
        }

        // 간접 인자도 **뷰마다** 하나다. 뷰별로 개수가 다르게 나오기 때문이다.
        constexpr RHIBufferUsage kArgsUsage =
            RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
        for ( GpuCullViewResources& view : _arrCullView )
        {
            if ( view._indirectArgs.ensureCapacity( pDevice, static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), argsCount,
                                                    kArgsUsage, false, true, _listScratchIndirectCmd.data() ) )
            {
                view._indirectArgs.upload( pDevice, _listScratchIndirectCmd.data(),
                                           argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
            }
        }

        // 인자 버퍼를 하나라도 못 만들었으면 컴퓨트가 개수를 만들 수 없다. 개수 0 짜리 인자로 그리면
        // 그 뷰는 빈 화면이 된다. 플래그를 내리고 CPU 개수로 되돌려 올린다.
        if ( _bGpuFillsIndirectCounts != SW_FALSE && hasAllCullViewBuffers() == false )
        {
            _bGpuFillsIndirectCounts = SW_FALSE;
            refreshIndirectCounts( pDevice );
        }

        _indirectCommandCount = argsCount;
        _snapshot._bCpuDirty  = SW_FALSE;
        _bBatchTablesDirty    = SW_FALSE;

        return _instances._buffer != 0 && getIndirectArgsBuffer() != 0;
    }

    bool GpuScene::hasAllVisibleBuffers() const
    {
        for ( const GpuCullViewResources& view : _arrCullView )
        {
            if ( view._visibleInstances._buffer == 0 )
                return false;
        }
        return true;
    }

    bool GpuScene::hasAllCullViewBuffers() const
    {
        for ( const GpuCullViewResources& view : _arrCullView )
        {
            if ( view._indirectArgs._buffer == 0 || view._visibleInstances._buffer == 0 )
                return false;
        }
        return true;
    }

    void GpuScene::refreshIndirectCounts( IRHIDevice* pDevice )
    {
        const uint32 argsCount = static_cast<uint32>( _snapshot._listAllBatch.size() );
        if ( pDevice == nullptr || argsCount == 0 || _listScratchIndirectCmd.size() < argsCount )
            return;

        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            // 압축을 포기한 배치(Preserve)만 CPU 개수를 그대로 두고, 나머지는 컴퓨트가 0 부터 센다.
            const bool bPreserve = ( argIndex < _listScratchBatchInfo.size() ) &&
                                   ( static_cast<GpuBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GpuBatchSortMode::Preserve );
            _listScratchIndirectCmd[argIndex]._instanceCount = bPreserve ? _snapshot._listAllBatch[argIndex]._instanceCount : 0u;
        }
        for ( GpuCullViewResources& view : _arrCullView )
        {
            if ( view._indirectArgs._buffer == 0 )
                continue;
            pDevice->getResource()->updateStructuredBuffer( view._indirectArgs._buffer, _listScratchIndirectCmd.data(),
                                                            argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }
    }

    void GpuScene::setIndirectCountsFilledByGpu( bool bByGpu )
    {
        const uint8 value = bByGpu ? 1u : 0u;
        if ( _bWantGpuIndirectCounts == value )
            return;
        _bWantGpuIndirectCounts = value;
        // 간접 인자의 내용이 달라지므로 다음 upload 가 반드시 다시 올려야 한다.
        _snapshot._bCpuDirty = SW_TRUE;
    }

    void GpuScene::releaseGpu( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        for ( auto& pair : _mapMaterialGpu )
            pair.second._slot.release( pDevice );
        _mapMaterialGpu.clear();
        for ( GpuMeshBatch& batch : _snapshot._listAllBatch )
        {
            batch._materialBuffer = 0;
            batch._materialSrv    = kInvalidDescriptorIndex;
        }
        // 슬롯이 뷰 등록 해제와 버퍼 파괴를 순서까지 맞춰 처리한다. 예전에는 여기서 손으로 스무 줄을
        // 늘어놓았고, 뷰 하나를 빠뜨려도 컴파일은 통과했다.
        _instances.release( pDevice );
        _batchInfo.release( pDevice );
        _vertexPool.release( pDevice );
        if ( _instanceSlotStream != 0 && pDevice != nullptr && pDevice->getResource() != nullptr )
            pDevice->getResource()->destroyBuffer( _instanceSlotStream );
        _instanceSlotStream         = 0;
        _instanceSlotStreamCapacity = 0;
        if ( _instanceSlotStream != 0 && pDevice != nullptr && pDevice->getResource() != nullptr )
            pDevice->getResource()->destroyBuffer( _instanceSlotStream );
        _instanceSlotStream         = 0;
        _instanceSlotStreamCapacity = 0;
        for ( GpuMeshBatch& batch : _snapshot._listAllBatch )
        {
            batch._vertexBuffer = 0; // 풀이 사라졌다. 다음 upload 가 다시 정한다
            batch._firstVertex  = 0;
        }
        for ( GpuCullViewResources& view : _arrCullView )
        {
            view._visibleInstances.release( pDevice );
            view._indirectArgs.release( pDevice );
        }
        _bBatchTablesDirty           = SW_TRUE;
        _indirectCommandCount        = 0;
        _snapshot._spinInstanceCount = 0;
        _snapshot._bCpuDirty         = SW_TRUE;
    }

    void GpuScene::adoptCpuSnapshot( GpuSceneSnapshot& snapshot )
    {
        // 바꿔치기다. 지난 스냅샷의 저장소가 패킷 자리로 돌아가 다음 프레임에 재사용된다(GpuScene.h 참고).
        std::swap( _snapshot, snapshot );
    }

    void GpuScene::uploadMaterialGroups( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GpuScene.uploadMaterials" );
        if ( pDevice == nullptr )
            return;

        for ( const GpuMaterialGroup& group : _snapshot._listMaterialGroup )
        {
            if ( group._listEntry.empty() )
                continue;

            // 원소 stride 는 셰이더 리플렉션이 준 구조버퍼 원소 크기다(Material::getElementStride, 백엔드의 리플렉션 값).
            // 같은 셰이더의 머티리얼은 같은 stride 를 가진다. 다르면 가장 큰 것을 쓴다.
            uint32 stride{ 0 };
            for ( const auto& entry : group._listEntry )
            {
                Material* pMaterial = entry._material.get();
                if ( pMaterial == nullptr )
                    continue;
                // 레이아웃의 기준은 셰이더다. 이 백엔드의 리플렉션으로 오프셋 · stride 를 맞춘 뒤 바이트를 읽는다(백엔드마다 한 번).
                pMaterial->ensureShaderLayout( pDevice );
                uint32 entryStride = pMaterial->getElementStride();
                if ( entryStride == 0 )
                    entryStride = static_cast<uint32>( pMaterial->getBuffer().size() );
                stride = MathUtil::max( stride, entryStride );
            }
            // stride 는 셰이더가 선언한 원소 크기 그대로다. DX11 은 SRV 의 구조 stride 가 셰이더 선언(24 등)과 다르면 디버그 레이어가
            // 오류로 잡고, 네 백엔드가 DX 패킹(-fvk-use-dx-layout)으로 같은 stride 를 쓴다. 16 정렬로 키우면 DX11 이 "32 vs 24" 를 낸다.
            if ( stride == 0 )
                continue;

            const uint32 elementCount = static_cast<uint32>( group._listEntry.size() );
            const uint32 needBytes    = stride * elementCount;
            _listMaterialScratch.assign( needBytes, 0 );
            for ( uint32 element = 0; element < elementCount; ++element )
            {
                const Material*         pMaterial = group._listEntry[element]._material.get();
                const MaterialInstance* pInstance = group._listEntry[element]._instance.get();
                if ( pMaterial == nullptr )
                    continue;
                const vector<uint8>& bytes = ( pInstance != nullptr && pInstance->getBuffer().empty() == false ) ? pInstance->getBuffer() : pMaterial->getBuffer();
                const uint32         copy  = MathUtil::min( stride, static_cast<uint32>( bytes.size() ) );
                if ( copy > 0 )
                    Memory::copy( _listMaterialScratch.data() + static_cast<size_t>( element ) * stride, bytes.data(), copy );
            }

            GpuMaterialGpu& gpu = _mapMaterialGpu[group._shaderPath];
            // 여유를 두어 머티리얼이 하나 늘 때마다 다시 만들지 않는다. stride 가 달라지면 슬롯이 알아서
            // 다시 만든다. 구조버퍼의 stride 는 뷰에 박혀 있어 셰이더 선언과 달라지면 안 된다.
            const uint32 capacityElements = MathUtil::max( elementCount * 2u, 16u );
            const bool   bRecreate        = ( gpu._slot._buffer == 0 ) || ( gpu._slot._elementSize != stride ) ||
                                   ( gpu._slot._capacityElements < capacityElements );
            if ( gpu._slot.ensureCapacity( pDevice, stride, capacityElements,
                                           RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
            {
                SW_LOG_ERROR( "머티리얼 데이터 버퍼 생성 실패 (%#, stride %#, %# 원소).", group._shaderPath.c_str(), stride, elementCount );
                continue;
            }
            // 값이 지난 업로드와 같으면 올리지 않는다. 머티리얼 수에 비례하던 프레임당 업로드가 바뀐 그룹만큼으로 준다.
            const bool bChanged = bRecreate || gpu._lastBytes.size() != needBytes ||
                                  Memory::compare( gpu._lastBytes.data(), _listMaterialScratch.data(), needBytes ) != 0;
            if ( bChanged )
            {
                gpu._slot.upload( pDevice, _listMaterialScratch.data(), needBytes );
                gpu._lastBytes.assign( _listMaterialScratch.begin(), _listMaterialScratch.begin() + needBytes );
            }
        }

        // **그룹당 한 번 풀어 두고, 배치는 인덱스로 집는다.**
        // 예전에는 배치마다 셰이더 **경로 문자열**로 해시 조회를 했다. 그룹은 한둘인데 배치는 수백이라
        // 같은 답을 배치 수만큼 다시 구한 셈이다(Release 실측 프레임당 86us, RT 렌더 시간의 12%).
        // 그룹 수만큼만 조회해 표로 만들어 두면 배치 루프는 저장 몇 번으로 끝난다. 표는 멤버라 프레임마다 힙을 만지지 않는다.
        const uint32           groupCount   = static_cast<uint32>( _snapshot._listMaterialGroup.size() );
        vector<ResolvedGroup>& listResolved = _listResolvedGroupScratch;
        listResolved.assign( groupCount, ResolvedGroup{} );
        for ( uint32 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const GpuMaterialGroup& group = _snapshot._listMaterialGroup[groupIndex];
            const auto              it    = _mapMaterialGpu.find( group._shaderPath );
            if ( it == _mapMaterialGpu.end() )
                continue;
            listResolved[groupIndex]._buffer       = it->second._slot._buffer;
            listResolved[groupIndex]._srv          = it->second._slot._srv;
            listResolved[groupIndex]._elementCount = static_cast<uint32>( group._listEntry.size() );
        }

        auto applyToBatches = [&listResolved, groupCount]( vector<GpuMeshBatch>& listBatch )
        {
            for ( GpuMeshBatch& batch : listBatch )
            {
                batch._materialBuffer = 0;
                batch._materialSrv    = kInvalidDescriptorIndex;
                batch._materialCount  = 0;
                if ( batch._materialGroup == kInvalidMaterialGroup || batch._materialGroup >= groupCount )
                    continue;
                const ResolvedGroup& resolved = listResolved[batch._materialGroup];
                batch._materialBuffer         = resolved._buffer;
                batch._materialSrv            = resolved._srv;
                batch._materialCount          = resolved._elementCount;
            }
        };
        applyToBatches( _snapshot._listAllBatch );
        applyToBatches( _snapshot._listOpaqueBatch );
        applyToBatches( _snapshot._listTransparentBatch );
    }
} // namespace sw
