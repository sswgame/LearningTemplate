#include "pch.h"

#include "Engine/Renderer/Scene/GPUScene.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

namespace sw
{
    SW_LOG_CALLER( "GPUScene" );

    namespace
    {
        struct GPUSceneInternal
        {
            static void applyInstanceCbsVal( IRHIDevice* pDevice, vector_reference<GPUMeshBatch> listBatch )
            {
                for ( GPUMeshBatch& batch : listBatch )
                {
                    if ( batch._materialInstance == nullptr )
                        continue;
                    if ( batch._materialInstance->updateRHI( pDevice ) == false )
                        continue;
                    batch._materialCb = batch._materialInstance->getDescriptorIndex();
                    // 슬롯 바인딩 백엔드(DX11 · GL)는 배치마다 텍스처 슬롯을 건다. 인스턴스가 덮어쓴 텍스처를 얹는다(렌더 스레드가 빌린 것이라 여기서).
                    batch._materialInstance->collectTextureSlotSRVs( batch._arrMaterialTexSRV, shaderslot::kMaterialTextureCount );
                }
            }

            /**
             * @brief 머티리얼 원소 표의 인스턴스를 올립니다(합친 배치의 인스턴스).
             * @details 네이티브 bindless 에서는 배치를 머티리얼끼리 합치고 배치에 인스턴스를 싣지 않는다 — 인스턴스는 원소 표(`_listEntry`)에만 있다.
             *          주의: 여기를 돌지 않으면 그 인스턴스들의 바이트가 만들어지지 않아 원소 업로드가 부모 바이트로 폴백하고,
             *          **DX12 · Vulkan 에서 불투명 인스턴스의 오버라이드가 통째로 사라진다**. updateRHI 는 바뀐 것이 없으면 곧바로 돌아온다.
             */
            static void applyElementInstancesVal( IRHIDevice* pDevice, vector<GPUMaterialGroup>& listGroup )
            {
                for ( GPUMaterialGroup& group : listGroup )
                {
                    for ( GPUMaterialElement& element : group._listEntry )
                    {
                        if ( element._instance != nullptr )
                            (void)element._instance->updateRHI( pDevice );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GPUScene::clear()
    {
        // 스냅샷이 든 소유(메시 · 머티리얼 · 인스턴스)를 여기서 놓는다. 디바이스보다 먼저 불러야 한다(Graphics/README "소유와 수명" 4).
        _snapshot             = GPUSceneSnapshot{};
        _indirectCommandCount = 0;
        _bBatchTablesDirty    = SW_TRUE;
    }

    void GPUScene::setVertexPoolEnabled( bool bEnabled )
    {
        const uint8 enabledFlag = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bVertexPoolEnabled == enabledFlag )
            return;
        _bVertexPoolEnabled = enabledFlag;
        _bBatchTablesDirty  = SW_TRUE; // startVertex 가 바뀐다. 표와 간접 인자를 다시 올린다
    }

    void GPUScene::assignMorphBases( const GPUMeshMorphPool& pool )
    {
        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            // 구간 배치는 메시 시작에 구간 첫 정점을 더한다 — 셰이더는 배치 안 번호(로컬)를 더해 원소를 찾는다(binding.hlsli MorphPool::computeElement).
            const uint32 meshBase = pool.baseOf( batch._mesh.get() );
            const uint32 base     = ( meshBase != GPUMeshMorphPool::kInvalidBase ) ? meshBase + batch._sectionFirstVertex : meshBase;
            if ( batch._morphVertexBase != base )
                _bBatchTablesDirty = SW_TRUE; // 표가 이 값을 든다. 바뀌면 다시 올린다
            batch._morphVertexBase = base;
            // 모프 · 스킨이 필요한데 풀에 없다(상한 초과 · 버퍼 실패, 풀이 오류를 남겼다) — 레스트 포즈로 대신 그리지 않고 이 배치를 거른다.
            // VAT 메시는 모프 풀이 아니라 VAT 풀이 판정한다(assignVertexAnimationBases).
            const Mesh* pMesh        = batch._mesh.get();
            const bool  bNeedsDeform = pMesh != nullptr && pMesh->findVertexAnimation() == nullptr && ( pMesh->hasSkin() || pMesh->isGPUMorphEnabled() );
            batch._bDeformRefused    = ( bNeedsDeform && meshBase == GPUMeshMorphPool::kInvalidBase ) ? SW_TRUE : SW_FALSE;
        }
    }

    void GPUScene::assignVertexAnimationBases( const GPUVertexAnimationPool& pool )
    {
        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            const uint32 base = pool.baseOf( batch._mesh.get() );
            if ( batch._vertexAnimationBase != base )
                _bBatchTablesDirty = SW_TRUE; // 표가 이 값을 든다. 바뀌면 다시 올린다
            batch._vertexAnimationBase = base;
            // VAT 가 있는데 풀에 없다(상한 초과 · 버퍼 실패, 풀이 오류를 남겼다) — 바인드 포즈로 대신 그리지 않고 이 배치를 거른다.
            const Mesh*                pMesh       = batch._mesh.get();
            const MeshVertexAnimation* pAnimation  = ( pMesh != nullptr ) ? pMesh->findVertexAnimation() : nullptr;
            const bool                 bNeedsTable = pAnimation != nullptr && pAnimation->isEmpty() == false;
            if ( bNeedsTable && base == GPUVertexAnimationPool::kInvalidBase )
                batch._bDeformRefused = SW_TRUE;
        }
    }

    bool GPUScene::upload( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GPUScene.upload" );

        const bool bNothingToUploadScene = pDevice == nullptr || _snapshot.getInstances().empty() || _snapshot.getAllBatches().empty();
        if ( bNothingToUploadScene )
            return false;

        // RT 에서만 하는 두 가지: 머티리얼 인스턴스의 오버라이드를 CB 에 반영하고(updateRHI), 메시를 GPU 에 올린다(정점 풀, 풀 밖이면 initRHI).
        {
            SW_PROFILE_SCOPE( "RT.GPUScene.applyInstanceCbs" );
            GPUSceneInternal::applyInstanceCbsVal( pDevice, _snapshot.getMutableBatches() );
            GPUSceneInternal::applyElementInstancesVal( pDevice, _snapshot._listMaterialGroup );
        }
        updateVertexPool( pDevice );
        // 머티리얼 데이터 구조버퍼(셰이더 타입별). 값이 프레임마다 바뀔 수 있어 스냅샷 dirty 와 무관하게 매번 본다(바이트가 같으면 올리지 않는다).
        uploadMaterialGroups( pDevice );
        // 불투명 · 투명 배치는 같은 목록의 구간 뷰라(GPUSceneSnapshot) 위에서 덧쓴 GPU 필드를 따로 옮겨 적을 사본이 없다.

        // CPU 스냅샷이 그대로면 인스턴스 버퍼 재업로드를 생략한다. **간접 인자는 예외다.**
        // 컬링 컴퓨트가 개수를 InterlockedAdd 로 만드는 동안에는 매 프레임 0 으로 되돌려 놓아야 한다.
        // 주의: 여기서 같이 건너뛰면 정적 씬에서 개수가 N, 2N, 3N ... 으로 끝없이 자란다(드로우 비용이
        // 계속 늘고, 이번 프레임에 쓰지 않은 가시 목록 자리를 읽는다). 움직이는 벤치와 한 프레임만
        // 그리는 테스트로는 드러나지 않는다.
        // 배치 표 · 간접 인자는 스냅샷이 그대로여도 풀 오프셋이 바뀌면(_bBatchTablesDirty) 다시 올린다.
        const bool bCPUDirty = _snapshot._bCPUDirty != SW_FALSE || _instances._buffer == 0 || getIndirectArgsBuffer() == 0;
        if ( bCPUDirty == false && _bBatchTablesDirty == SW_FALSE )
        {
            if ( _bGPUFillsIndirectCounts != SW_FALSE )
                refreshIndirectCounts( pDevice );
            return true;
        }

        const uint32 instanceCount = static_cast<uint32>( _snapshot.getInstances().size() );
        const uint32 argsCount     = static_cast<uint32>( _snapshot.getAllBatches().size() );
        if ( bCPUDirty )
            uploadInstanceAndVisibleBuffers( pDevice, instanceCount );

        SW_PROFILE_SCOPE( "RT.GPUScene.batchTables" );
        uploadBatchTable( pDevice, argsCount );
        // 컴퓨트가 개수를 만들려면 **가시 목록과 배치 구간이 둘 다** 있어야 한다. 하나라도 없으면 개수를
        // 0 으로 올리면 안 된다. 컬링이 못 도는데 개수가 0 이면 그 프레임은 아무것도 안 그려진다.
        // 그래서 "요청했다"(_bGPUIndirectCountsRequested)와 "실제로 된다"(_bGPUFillsIndirectCounts)를 나눠 둔다.
        _bGPUFillsIndirectCounts =
            ( _bGPUIndirectCountsRequested != SW_FALSE && _batchInfo._buffer != 0 && argsCount > 0 && hasAllVisibleBuffers() ) ? 1u : 0u;
        populateIndirectCommands( argsCount );
        ensureInstanceSlotStream( pDevice, _snapshot.getVisibleSlotCount() );
        uploadIndirectArgs( pDevice, argsCount );

        // 인자 버퍼를 하나라도 못 만들었으면 컴퓨트가 개수를 만들 수 없다. 개수 0 짜리 인자로 그리면
        // 그 뷰는 빈 화면이 된다. 플래그를 내리고 CPU 개수로 되돌려 올린다.
        if ( _bGPUFillsIndirectCounts != SW_FALSE && hasAllCullViewBuffers() == false )
        {
            _bGPUFillsIndirectCounts = SW_FALSE;
            refreshIndirectCounts( pDevice );
        }

        _indirectCommandCount = argsCount;
        _snapshot._bCPUDirty  = SW_FALSE;
        _bBatchTablesDirty    = SW_FALSE;

        return _instances._buffer != 0 && getIndirectArgsBuffer() != 0;
    }

    void GPUScene::updateVertexPool( IRHIDevice* pDevice )
    {
        // 정점 풀: 배치 메시 모두를 한 정점 버퍼에 모은다. 집합이 같으면 아무것도 하지 않고, 바뀌면 들어오고 나간 메시의 구간만 고친다(CPU 정점에서 올린다).
        // 풀에 든 배치는 풀 버퍼와 시작 오프셋으로 그리고(같은 버퍼 → 멀티 드로우로 묶인다), 못 든 배치만 자기 정점 버퍼(오프셋 0)를 만들어 그린다.
        // 동적 메시(프레임마다 새 메시 — 천)도 풀에 든다 — 풀은 바뀐 메시를 "나감 + 들어옴" 으로 보고 그 구간만 올리고, 구간 반납 지연이 앞 프레임이 읽는
        // 위치를 지킨다. 그래서 프레임마다의 정점 버퍼 생성 · 반납이 없다(게임 스레드도 동적 메시를 업로드 큐에 넣지 않는다).
        // 이 구간의 시간이 C-43 의 전/후 측정값이다.
        {
            SW_PROFILE_SCOPE( "RT.GPUScene.vertexPool" );
            _listScratchPoolMesh.clear();
            if ( _bVertexPoolEnabled != SW_FALSE )
            {
                _listScratchPoolMesh.reserve( _snapshot.getAllBatches().size() );
                for ( const GPUMeshBatch& batch : _snapshot.getAllBatches() )
                {
                    _listScratchPoolMesh.push_back( batch._mesh.get() );
                }
            }
            // 끈 상태면 빈 목록으로 빌드해 풀이 비워진다. 배치는 아래에서 자기 정점 버퍼(오프셋 0)로 돌아간다.
            if ( _vertexPool.update( pDevice, _listScratchPoolMesh ) )
                _bBatchTablesDirty = SW_TRUE;
        }
        SW_PROFILE_SCOPE( "RT.GPUScene.uploadMeshes" );
        const RHIBufferHandle poolBuffer = _vertexPool.getVertexBuffer();
        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            const uint32 base = _vertexPool.baseOf( batch._mesh.get() );
            if ( base != GPUMeshVertexPool::kInvalidBase && poolBuffer != 0 )
            {
                batch._vertexBuffer = poolBuffer;
                batch._firstVertex  = base + batch._sectionFirstVertex;
                continue;
            }
            // 풀 밖(풀을 껐거나 못 든 메시) — 자기 정점 버퍼로 그린다. 구간의 첫 정점부터.
            batch._firstVertex = batch._sectionFirstVertex;
            if ( batch._mesh != nullptr && batch._mesh->initRHI( pDevice ) )
                batch._vertexBuffer = batch._mesh->getVertexBuffer();
        }
    }

    void GPUScene::uploadInstanceAndVisibleBuffers( IRHIDevice* pDevice, uint32 instanceCount )
    {
        // UnorderedAccess 를 함께 요구한다. instanceanimation 컴퓨트가 월드 행렬을 고쳐 쓴다. 못 만드는
        // 백엔드 · 드라이버면 슬롯이 SRV 전용으로 한 번 더 시도해 그리기는 그대로 살린다.
        constexpr RHIBufferUsage kInstanceUsage =
            RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        {
            SW_PROFILE_SCOPE( "RT.GPUScene.instanceBuffer" );
            // 버퍼가 새로 만들어졌는지 봐야 한다. 새 버퍼에는 아직 아무것도 없으므로 구간만
            // 올리면 나머지가 쓰레기다. 핸들이 바뀌었으면 다시 만들어진 것이다.
            const RHIBufferHandle bufferBefore = _instances._buffer;
            if ( _instances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( GPUInstance ) ), instanceCount, kInstanceUsage, true, true,
                                            _snapshot.getInstances().data() ) )
            {
                const bool bRecreated          = ( _instances._buffer != bufferBefore );
                const bool bFullInstanceUpload = bRecreated || _snapshot._bAllInstancesDirty != SW_FALSE || _snapshot._listDirtyInstanceRun.empty();
                if ( bFullInstanceUpload )
                    _instances.upload( pDevice, _snapshot.getInstances().data(), instanceCount * static_cast<uint32>( sizeof( GPUInstance ) ) );
                else
                    uploadDirtyInstanceRuns( pDevice, instanceCount );
            }
        }

        // 가시 인스턴스 ID 버퍼. 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해
        // 넣고(언리얼 FInstanceCullingContext 의 InstanceIDBuffer), 정점 셰이더가 그 순서로 읽는다.
        // **뷰마다 하나씩**이다. 목록은 절두체에 종속이라 메인 카메라로 거른 것을 그림자가 쓰면 안 된다.
        constexpr RHIBufferUsage kVisibleUsage =
            RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        // 가시 목록은 인스턴스 수가 아니라 가시 구간의 합만큼이다(LOD 그룹의 낮은 단계 배치가 구간을 더 가진다).
        const uint32 visibleSlotCount = _snapshot.getVisibleSlotCount();
        SW_PROFILE_SCOPE( "RT.GPUScene.visibleBuffers" );
        for ( GPUCullViewResources& view : _listCullView )
        {
            // 인스턴스가 0 이면 버퍼를 놓는 것이라 false 가 맞다.
            const bool bVisibleBuffer = view._visibleInstances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), visibleSlotCount, kVisibleUsage, true, true, nullptr );
            if ( bVisibleBuffer == false && visibleSlotCount > 0 )
                SW_LOG_ERROR_ONCE( "Failed to create the visible instance buffer (%# slots) of a cull view", visibleSlotCount );
        }
        // 가시 목록 항목은 아래 24비트가 인스턴스 번호다(다음 3비트는 LOD 단계, 위쪽 5비트는 크로스페이드 코드 — instancedata.hlsli `kVisibleEntryIDMask`).
        SW_ASSERT( instanceCount <= 0x00FFFFFFu );
        ensureLODStateBuffers( pDevice, instanceCount );
    }

    void GPUScene::ensureLODStateBuffers( IRHIDevice* pDevice, uint32 instanceCount )
    {
        // 전환 상태는 LOD 그룹이 있을 때만 필요하다. 없으면 놓아 인스턴스 수만큼의 메모리를 뷰마다 들고 있지 않는다.
        const uint32             stateCount     = _snapshot.hasLODGroups() ? instanceCount : 0u;
        constexpr RHIBufferUsage kLODStateUsage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess;
        for ( GPUCullViewResources& view : _listCullView )
        {
            const RHIBufferHandle bufferBefore = view._lodState._buffer;
            const bool            bEnsured     = view._lodState.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), stateCount, kLODStateUsage, false, true, nullptr );
            if ( bEnsured == false && stateCount > 0 )
                SW_LOG_ERROR_ONCE( "Failed to create the LOD state buffer (%# instances) of a cull view - its LOD levels switch without a crossfade", stateCount );
            // 새 버퍼의 내용은 정해져 있지 않다 — 다음 컬링이 비운다.
            if ( view._lodState._buffer != bufferBefore )
                view._lodStateLayoutGeneration = 0xFFFFFFFFu;
        }
    }

    bool GPUScene::takeLODStateReset( uint32 cullViewIndex )
    {
        if ( cullViewIndex >= _listCullView.size() )
            return false;
        GPUCullViewResources& view = _listCullView[cullViewIndex];
        if ( view._lodStateLayoutGeneration == _snapshot._instanceLayoutGeneration )
            return false;
        view._lodStateLayoutGeneration = _snapshot._instanceLayoutGeneration;
        return true;
    }

    void GPUScene::uploadDirtyInstanceRuns( IRHIDevice* pDevice, uint32 instanceCount )
    {
        // **바뀐 구간만, 그리고 한 번의 호출로 올린다.** 구간마다 따로 부르면 백엔드가
        // 스테이징 확보와 큐 제출을 그만큼 되풀이한다(DX12 에서 호출당 ~3.3 us).
        constexpr uint32 kStride = static_cast<uint32>( sizeof( GPUInstance ) );
        _listScratchCopyRegion.clear();
        _listScratchCopyRegion.reserve( _snapshot._listDirtyInstanceRun.size() );
        for ( const GPUInstanceRun& run : _snapshot._listDirtyInstanceRun )
        {
            if ( run._count == 0 || run._start >= instanceCount )
                continue;
            const uint32 count = MathUtil::min( run._count, instanceCount - run._start );
            _listScratchCopyRegion.push_back( RHIBufferCopyRegion{ run._start * kStride, run._start * kStride, count * kStride } );
        }
        if ( _listScratchCopyRegion.empty() == false )
        {
            pDevice->getResourceFactory()->updateStructuredBufferRegions( _instances._buffer, _snapshot.getInstances().data(),
                                                                          _listScratchCopyRegion.data(),
                                                                          static_cast<uint32>( _listScratchCopyRegion.size() ) );
        }
    }

    void GPUScene::uploadBatchTable( IRHIDevice* pDevice, uint32 argsCount )
    {
        // 배치 표: 컬링 컴퓨트는 "이 배치의 인스턴스는 어디서 시작하나" 를, 정점 셰이더는 모프 · 정점 풀 오프셋을 읽는다.
        _listScratchBatchInfo.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            const GPUMeshBatch& infoBatch                        = _snapshot.getAllBatches()[argIndex];
            _listScratchBatchInfo[argIndex]._instanceBase        = infoBatch._instanceBase;
            _listScratchBatchInfo[argIndex]._instanceCount       = infoBatch._instanceCount;
            _listScratchBatchInfo[argIndex]._visibleBase         = infoBatch._visibleBase;
            _listScratchBatchInfo[argIndex]._lodLevelCount       = infoBatch._lodLevelCount;
            _listScratchBatchInfo[argIndex]._lodScreenSize       = infoBatch._lodScreenSize;
            _listScratchBatchInfo[argIndex]._morphVertexBase     = infoBatch._morphVertexBase;
            _listScratchBatchInfo[argIndex]._firstVertex         = infoBatch._firstVertex;
            _listScratchBatchInfo[argIndex]._vertexAnimationBase = infoBatch._vertexAnimationBase;
            _listScratchBatchInfo[argIndex]._sectionVertexOffset = infoBatch._sectionFirstVertex;
            // 투명은 압축한 뒤 GPU 가 깊이순으로 다시 정렬한다. 한 워크그룹에 안 담기는 큰 배치만
            // 압축을 포기하고 CPU 가 정렬해 둔 순서를 그대로 쓴다.
            GPUBatchSortMode sortMode = GPUBatchSortMode::None;
            if ( infoBatch._blendMode == RHIBlendMode::Transparent )
                sortMode = ( infoBatch._instanceCount <= kGPUSortMaxElements ) ? GPUBatchSortMode::DepthGPU : GPUBatchSortMode::Preserve;
            _listScratchBatchInfo[argIndex]._sortMode = static_cast<uint32>( sortMode );
        }
        constexpr RHIBufferUsage kBatchInfoUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        if ( _batchInfo.ensureCapacity( pDevice, static_cast<uint32>( sizeof( GPUBatchInfo ) ), argsCount, kBatchInfoUsage, true, false,
                                        _listScratchBatchInfo.data() ) )
            _batchInfo.upload( pDevice, _listScratchBatchInfo.data(), argsCount * static_cast<uint32>( sizeof( GPUBatchInfo ) ) );
    }

    uint32 GPUScene::computeStartInstance( const GPUMeshBatch& batch ) const
    {
        return ( _bGPUFillsIndirectCounts != SW_FALSE ) ? batch._visibleBase : batch._instanceBase;
    }

    void GPUScene::populateIndirectCommands( uint32 argsCount )
    {
        _listScratchIndirectCmd.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            RHIDrawIndirectCommand& cmd = _listScratchIndirectCmd[argIndex];
            cmd._vertexCount            = _snapshot.getAllBatches()[argIndex]._vertexCount;
            // 컬링 컴퓨트가 개수를 만드는 배치는 **0 에서 시작**해야 한다. InterlockedAdd 로 보이는 것만 센다.
            // 압축을 포기한 배치(Preserve)와 컬링이 아예 없을 때는 CPU 가 센 개수를 그대로 쓴다.
            const bool bPreserve  = static_cast<GPUBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GPUBatchSortMode::Preserve;
            const bool bGPUCounts = ( _bGPUFillsIndirectCounts != SW_FALSE ) && ( bPreserve == false );
            cmd._instanceCount    = bGPUCounts ? 0u : _snapshot.getAllBatches()[argIndex]._instanceCount;
            // 정점 풀 안의 시작. 입력 어셈블러가 그 구간을 읽는다(SV_VertexID 가 이 값을 포함하는지는 API 마다 다르다: binding.hlsli).
            cmd._startVertexLocation = _snapshot.getAllBatches()[argIndex]._firstVertex;
            // 배치의 가시 구간 시작. 인스턴스 슬롯 스트림(슬롯 1)의 원소를 그만큼 건너뛴다. 셰이더는 SV_InstanceID 를 쓰지 않으므로
            // "SPIR-V InstanceIndex 는 포함하고 DX 는 안 한다" 는 차이에 기대지 않는다. 입력 어셈블러의 인스턴스 스텝
            // 스트림은 네 API 모두 startInstance 부터 읽는다.
            // 컬링이 도는 프레임은 슬롯 스트림이 가시 목록을 읽으므로 시작은 가시 구간의 시작이고, 컬링이 없으면 슬롯이 곧 인스턴스 번호라 인스턴스 구간의 시작이다.
            cmd._startInstanceLocation = computeStartInstance( _snapshot.getAllBatches()[argIndex] );
        }
    }

    void GPUScene::ensureInstanceSlotStream( IRHIDevice* pDevice, uint32 slotCount )
    {
        // 인스턴스 슬롯 스트림: 항등 배열이다. 가시 칸 수(`GPUSceneSnapshot::getVisibleSlotCount`)만큼 커지면 다시 만든다.
        if ( _instanceSlotStreamCapacity >= slotCount && _instanceSlotStream != 0 )
            return;
        if ( _instanceSlotStream != 0 )
            pDevice->getResourceFactory()->destroyBuffer( _instanceSlotStream );
        const uint32   capacity = MathUtil::max( slotCount, 256u );
        vector<uint32> listSlot( capacity );
        for ( uint32 slotIndex = 0; slotIndex < capacity; ++slotIndex )
        {
            listSlot[slotIndex] = slotIndex;
        }
        _instanceSlotStream         = pDevice->getResourceFactory()->createVertexBuffer( listSlot.data(), capacity * static_cast<uint32>( sizeof( uint32 ) ) );
        _instanceSlotStreamCapacity = ( _instanceSlotStream != 0 ) ? capacity : 0;
        if ( _instanceSlotStream == 0 )
            SW_LOG_ERROR( "Failed to create the instance slot stream (%# instances) - scene draws cannot find their instances.", capacity );
    }

    void GPUScene::uploadIndirectArgs( IRHIDevice* pDevice, uint32 argsCount )
    {
        // 간접 인자도 **뷰마다** 하나다. 뷰별로 개수가 다르게 나오기 때문이다.
        constexpr RHIBufferUsage kArgsUsage =
            RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
        for ( GPUCullViewResources& view : _listCullView )
        {
            if ( view._indirectArgs.ensureCapacity( pDevice, static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), argsCount,
                                                    kArgsUsage, false, true, _listScratchIndirectCmd.data() ) )
            {
                view._indirectArgs.upload( pDevice, _listScratchIndirectCmd.data(),
                                           argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
            }
        }
    }

    bool GPUScene::hasAllVisibleBuffers() const
    {
        for ( const GPUCullViewResources& view : _listCullView )
        {
            if ( view._visibleInstances._buffer == 0 )
                return false;
        }
        return true;
    }

    bool GPUScene::hasAllCullViewBuffers() const
    {
        for ( const GPUCullViewResources& view : _listCullView )
        {
            if ( view._indirectArgs._buffer == 0 || view._visibleInstances._buffer == 0 )
                return false;
        }
        return true;
    }

    void GPUScene::refreshIndirectCounts( IRHIDevice* pDevice )
    {
        const uint32 argsCount      = static_cast<uint32>( _snapshot.getAllBatches().size() );
        const bool   bNothingToFill = pDevice == nullptr || argsCount == 0 || _listScratchIndirectCmd.size() < argsCount;
        if ( bNothingToFill )
            return;

        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            // 압축을 포기한 배치(Preserve)만 CPU 개수를 그대로 두고, 나머지는 컴퓨트가 0 부터 센다.
            const bool bPreserve = ( argIndex < _listScratchBatchInfo.size() ) &&
                                   ( static_cast<GPUBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GPUBatchSortMode::Preserve );
            _listScratchIndirectCmd[argIndex]._instanceCount = bPreserve ? _snapshot.getAllBatches()[argIndex]._instanceCount : 0u;
            // 컬링 여부가 바뀌었을 수 있으므로 시작도 다시 정한다(populateIndirectCommands 와 같은 규칙).
            _listScratchIndirectCmd[argIndex]._startInstanceLocation = computeStartInstance( _snapshot.getAllBatches()[argIndex] );
        }
        for ( GPUCullViewResources& view : _listCullView )
        {
            if ( view._indirectArgs._buffer == 0 )
                continue;
            pDevice->getResourceFactory()->updateStructuredBuffer( view._indirectArgs._buffer, _listScratchIndirectCmd.data(),
                                                                   argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }
    }

    void GPUScene::setCullViewCount( IRHIDevice* pDevice, uint32 count )
    {
        const size_t wanted = MathUtil::max( count, static_cast<uint32>( RenderViewType::Count ) );
        if ( wanted == _listCullView.size() )
            return;
        for ( size_t index = wanted; index < _listCullView.size(); ++index )
        {
            _listCullView[index]._visibleInstances.release( pDevice );
            _listCullView[index]._lodState.release( pDevice );
            _listCullView[index]._indirectArgs.release( pDevice );
        }
        _listCullView.resize( wanted );
        // 새 칸의 버퍼는 다음 업로드가 만든다 — 스냅샷이 그대로여도 다시 올리게 한다.
        _snapshot._bCPUDirty = SW_TRUE;
    }

    void GPUScene::setIndirectCountsFilledByGPU( bool bByGPU )
    {
        const uint8 value = bByGPU ? 1u : 0u;
        if ( _bGPUIndirectCountsRequested == value )
            return;
        _bGPUIndirectCountsRequested = value;
        // 간접 인자의 내용이 달라지므로 다음 upload 가 반드시 다시 올려야 한다.
        _snapshot._bCPUDirty = SW_TRUE;
    }

    void GPUScene::releaseGPU( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        for ( auto& pair : _mapMaterialGroupBuffer )
        {
            pair.second._slot.release( pDevice );
        }
        _mapMaterialGroupBuffer.clear();
        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            batch._materialBuffer = 0;
            batch._materialSRV    = kInvalidDescriptorIndex;
        }
        // 슬롯이 뷰 등록 해제와 버퍼 파괴를 순서까지 맞춰 처리한다. 손으로 풀어 쓰면 뷰 하나를 빠뜨려도 컴파일은 통과한다.
        _instances.release( pDevice );
        _batchInfo.release( pDevice );
        _vertexPool.release( pDevice );
        const bool bReleasableSlotStream = _instanceSlotStream != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr;
        if ( bReleasableSlotStream )
            pDevice->getResourceFactory()->destroyBuffer( _instanceSlotStream );
        _instanceSlotStream         = 0;
        _instanceSlotStreamCapacity = 0;
        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            batch._vertexBuffer = 0; // 풀을 놓았다. 다음 upload 가 다시 정한다
            batch._firstVertex  = 0;
        }
        for ( GPUCullViewResources& view : _listCullView )
        {
            view._visibleInstances.release( pDevice );
            view._lodState.release( pDevice );
            view._indirectArgs.release( pDevice );
        }
        _bBatchTablesDirty           = SW_TRUE;
        _indirectCommandCount        = 0;
        _snapshot._spinInstanceCount = 0;
        _snapshot._bCPUDirty         = SW_TRUE;
    }

    const GPUViewTransparentOrder* GPUScene::findViewTransparentOrder( uint64 viewID ) const
    {
        for ( const GPUViewTransparentOrder& order : _snapshot._listViewTransparentOrder )
        {
            if ( order._viewID == viewID )
                return &order;
        }
        return nullptr;
    }

    void GPUScene::adoptCPUSnapshot( GPUSceneSnapshot& snapshot )
    {
        // 바꿔치기다. 지난 스냅샷의 저장소가 패킷 자리로 돌아가 다음 프레임에 재사용된다(GPUScene.h 참고).
        std::swap( _snapshot, snapshot );
    }

    uint32 GPUScene::computeMaterialGroupStride( IRHIDevice* pDevice, const GPUMaterialGroup& group )
    {
        // 원소 stride 는 셰이더 리플렉션이 준 구조버퍼 원소 크기다(Material::getElementStride, 백엔드의 리플렉션 값).
        // 같은 셰이더의 머티리얼은 같은 stride 를 가진다. 다르면 가장 큰 것을 쓴다.
        uint32 stride{ 0 };
        for ( const auto& entry : group._listEntry )
        {
            Material* pMaterial = entry._material.get();
            if ( pMaterial == nullptr )
                continue;
            // 레이아웃의 기준은 셰이더다. 이 백엔드의 리플렉션으로 오프셋 · stride 를 맞춘 뒤 바이트를 읽는다(맞춰져 있으면 그냥 지나간다 —
            // 인스턴스가 있는 머티리얼은 위 applyInstanceCbs 의 updateRHI 가 이미 맞췄다).
            // 실패(오류는 이미 남았다)하면 바이트가 비어 stride 에 끼지 않고 원소는 복사되지 않는다. 레거시 MaterialCB 는 stride 0 으로 false 지만 바이트가 있다.
            if ( pMaterial->ensureShaderLayout( pDevice ) == false && pMaterial->getBuffer().empty() )
                continue;
            uint32 entryStride = pMaterial->getElementStride();
            if ( entryStride == 0 )
                entryStride = static_cast<uint32>( pMaterial->getBuffer().size() );
            stride = MathUtil::max( stride, entryStride );
        }
        return stride;
    }

    void GPUScene::uploadMaterialGroup( IRHIDevice* pDevice, const GPUMaterialGroup& group )
    {
        // stride 는 셰이더가 선언한 원소 크기 그대로다. DX11 은 SRV 의 구조 stride 가 셰이더 선언(24 등)과 다르면 디버그 레이어가
        // 오류로 잡고, 네 백엔드가 DX 패킹(-fvk-use-dx-layout)으로 같은 stride 를 쓴다. 16 정렬로 키우면 DX11 이 "32 vs 24" 를 낸다.
        const uint32 stride = computeMaterialGroupStride( pDevice, group );
        if ( stride == 0 )
            return;

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

        GPUMaterialGroupBuffer& gpu = _mapMaterialGroupBuffer[group._shaderPath];
        // 모자랄 때만 두 배로 키운다. 주의: "원소 수 × 2" 를 **매번** 요구하면 16 개를 넘은 뒤로는 머티리얼이 하나 늘 때마다 다시
        // 만든다(용량 34 < 요구 36 → 36, 다음엔 38 …). stride 가 달라지면 슬롯이 알아서 다시 만든다. 구조버퍼의 stride 는 뷰에 박혀 있어
        // 셰이더 선언과 달라지면 안 된다.
        const bool   bFits            = ( gpu._slot._buffer != 0 ) && ( gpu._slot._elementSize == stride ) && ( gpu._slot._capacityElements >= elementCount );
        const uint32 capacityElements = bFits ? gpu._slot._capacityElements : MathUtil::max( elementCount * 2u, 16u );
        const bool   bRecreate        = ( bFits == false );
        if ( gpu._slot.ensureCapacity( pDevice, stride, capacityElements, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Failed to create the material data buffer (%#, stride %#, %# elements).", group._shaderPath.c_str(), stride, elementCount );
            return;
        }
        // 값이 지난 업로드와 같으면 올리지 않는다. 머티리얼 수에 비례하던 프레임당 업로드가 바뀐 그룹만큼으로 준다.
        const bool bChanged = bRecreate || gpu._lastBytes.size() != needBytes || Memory::compare( gpu._lastBytes.data(), _listMaterialScratch.data(), needBytes ) != 0;
        if ( bChanged )
        {
            gpu._slot.upload( pDevice, _listMaterialScratch.data(), needBytes );
            gpu._lastBytes.assign( _listMaterialScratch.begin(), _listMaterialScratch.begin() + needBytes );
        }
    }

    void GPUScene::bindBatchMaterials()
    {
        // **그룹당 한 번 풀어 두고, 배치는 인덱스로 집는다.**
        // 배치마다 셰이더 **경로 문자열**로 해시 조회를 하면 그룹은 한둘인데 배치는 수백이라 같은 답을 배치 수만큼
        // 다시 구한다(Release 실측 프레임당 86us, RT 렌더 시간의 12%).
        // 그룹 수만큼만 조회해 표로 만들어 두면 배치 루프는 저장 몇 번으로 끝난다. 표는 멤버라 프레임마다 힙을 만지지 않는다.
        const uint32           groupCount   = static_cast<uint32>( _snapshot._listMaterialGroup.size() );
        vector<ResolvedGroup>& listResolved = _listResolvedGroupScratch;
        listResolved.assign( groupCount, ResolvedGroup{} );
        for ( uint32 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const GPUMaterialGroup& group = _snapshot._listMaterialGroup[groupIndex];
            const auto              it    = _mapMaterialGroupBuffer.find( group._shaderPath );
            if ( it == _mapMaterialGroupBuffer.end() )
                continue;
            listResolved[groupIndex]._buffer       = it->second._slot._buffer;
            listResolved[groupIndex]._srv          = it->second._slot._srv;
            listResolved[groupIndex]._elementCount = static_cast<uint32>( group._listEntry.size() );
        }

        for ( GPUMeshBatch& batch : _snapshot.getMutableBatches() )
        {
            batch._materialBuffer = 0;
            batch._materialSRV    = kInvalidDescriptorIndex;
            batch._materialCount  = 0;
            if ( batch._materialGroup == kInvalidMaterialGroup || batch._materialGroup >= groupCount )
                continue;
            const ResolvedGroup& resolved = listResolved[batch._materialGroup];
            batch._materialBuffer         = resolved._buffer;
            batch._materialSRV            = resolved._srv;
            batch._materialCount          = resolved._elementCount;
        }
    }

    void GPUScene::uploadMaterialGroups( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GPUScene.uploadMaterials" );
        if ( pDevice == nullptr )
            return;

        for ( const GPUMaterialGroup& group : _snapshot._listMaterialGroup )
        {
            if ( group._listEntry.empty() )
                continue;
            uploadMaterialGroup( pDevice, group );
        }
        bindBatchMaterials();
    }
} // namespace sw
