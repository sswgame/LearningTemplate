#include "pch.h"

#include "Engine/Renderer/Scene/GPUScene.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

namespace sw
{
    namespace
    {
        struct GPUSceneInternal
        {
            /**
             * @brief 배치마다 머리 원소의 프록시에 사본을 올리고(바뀐 것만), 그 상수 버퍼 인덱스와 텍스처 슬롯을 배치에 덧씁니다.
             * @details 머티리얼의 GPU 자원은 렌더 스레드만 만든다 — 게임 스레드는 불변 사본만 실어 보낸다(`MaterialRenderProxy`). 원소 업로드(g_SwMaterials)는
             *          사본의 바이트를 직접 읽으므로 원소마다 상수 버퍼를 만들지 않는다. 상수 버퍼는 배치가 고르는 머리 원소의 것뿐이다.
             */
            static void bindBatchMaterialResources( IRHIDevice* pDevice, const vector<GPUMeshBatch>& listBatch, vector<GPUMeshBatchBinding>& inoutListBinding,
                                                    const vector<GPUMaterialGroup>& listGroup )
            {
                SW_ASSERT( listBatch.size() == inoutListBinding.size() );
                for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
                {
                    const GPUMeshBatch& batch       = listBatch[batchIndex];
                    const bool          bHasElement = batch._materialGroup < listGroup.size() && batch._materialIndex < listGroup[batch._materialGroup]._listEntry.size();
                    if ( bHasElement == false )
                        continue;
                    const MaterialRenderBinding& binding = listGroup[batch._materialGroup]._listEntry[batch._materialIndex]._binding;
                    if ( binding.isValid() == false )
                        continue;
                    GPUMeshBatchBinding& gpu = inoutListBinding[batchIndex];
                    gpu._materialCb          = binding._proxy->sync( pDevice, binding._data );
                    // 슬롯 바인딩 백엔드(DX11 · GL)는 배치마다 텍스처 슬롯을 건다. 인스턴스가 덮어쓴 텍스처는 사본이 이미 얹었다.
                    for ( uint32 slot = 0; slot < shaderslot::kMaterialTextureCount; ++slot )
                    {
                        gpu._arrMaterialTexSRV[slot] = binding._data->_arrTextureSRV[slot];
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
        _snapshot = GPUSceneSnapshot{};
        resetBatchBindings();
        _indirectCommandCount = 0;
        _bBatchTablesDirty    = SW_TRUE;
        // 씬을 비우면 움직임 기록도 버린다 — 다음 씬의 첫 프레임은 카메라 움직임뿐이다.
        _motionHistory.reset();
        _bObjectMotion      = SW_FALSE;
        _bDistortionBatches = SW_FALSE;
    }

    void GPUScene::setVertexPoolEnabled( bool bEnabled )
    {
        const uint8 enabledFlag = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bVertexPoolEnabled == enabledFlag )
            return;
        _bVertexPoolEnabled = enabledFlag;
        _bBatchTablesDirty  = SW_TRUE; // baseVertex · startIndex 가 바뀐다. 표와 간접 인자를 다시 올린다
    }

    void GPUScene::assignMorphBases( const GPUMeshMorphPool& pool )
    {
        const vector<GPUMeshBatch>& listBatch = _snapshot.getAllBatches();
        for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
        {
            const GPUMeshBatch&  batch = listBatch[batchIndex];
            GPUMeshBatchBinding& gpu   = _listBatchBinding[batchIndex];
            // 구간 · LOD 배치도 메시 시작 그대로다 — 인덱스가 메시 안 번호라 셰이더의 로컬 번호가 곧 메시 안 번호다(binding.hlsli MorphPool::computeElement).
            const uint32 meshBase = pool.baseOf( batch._mesh.get() );
            if ( gpu._morphVertexBase != meshBase )
                _bBatchTablesDirty = SW_TRUE; // 표가 이 값을 든다. 바뀌면 다시 올린다
            gpu._morphVertexBase = meshBase;
            // 모프가 필요한데 풀에 없다(상한 초과 · 버퍼 실패, 풀이 오류를 남겼다) — 레스트 포즈로 대신 그리지 않고 이 배치를 거른다.
            // 스킨 메시는 결과가 칸마다라 인스턴스 단위로 거른다(칸 표에 없는 칸의 정점 셰이더가 NaN 위치로 버린다, binding.hlsli MorphPool::loadVertex).
            // VAT 메시는 모프 풀이 아니라 VAT 풀이 판정한다(assignVertexAnimationBases).
            const Mesh* pMesh        = batch._mesh.get();
            const bool  bNeedsDeform = pMesh != nullptr && pMesh->findVertexAnimation() == nullptr && pMesh->hasSkin() == false && pMesh->isGPUMorphEnabled();
            gpu._bDeformRefused      = ( bNeedsDeform && meshBase == GPUMeshMorphPool::kInvalidBase ) ? SW_TRUE : SW_FALSE;
        }
    }

    void GPUScene::assignVertexAnimationBases( const GPUVertexAnimationPool& pool )
    {
        const vector<GPUMeshBatch>& listBatch = _snapshot.getAllBatches();
        for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
        {
            const GPUMeshBatch&  batch = listBatch[batchIndex];
            GPUMeshBatchBinding& gpu   = _listBatchBinding[batchIndex];
            const uint32         base  = pool.baseOf( batch._mesh.get() );
            if ( gpu._vertexAnimationBase != base )
                _bBatchTablesDirty = SW_TRUE; // 표가 이 값을 든다. 바뀌면 다시 올린다
            gpu._vertexAnimationBase = base;
            // VAT 가 있는데 풀에 없다(상한 초과 · 버퍼 실패, 풀이 오류를 남겼다) — 바인드 포즈로 대신 그리지 않고 이 배치를 거른다.
            const Mesh*                pMesh       = batch._mesh.get();
            const MeshVertexAnimation* pAnimation  = ( pMesh != nullptr ) ? pMesh->findVertexAnimation() : nullptr;
            const bool                 bNeedsTable = pAnimation != nullptr && pAnimation->isEmpty() == false;
            if ( bNeedsTable && base == GPUVertexAnimationPool::kInvalidBase )
                gpu._bDeformRefused = SW_TRUE;
        }
    }

    bool GPUScene::upload( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GPUScene.upload" );
        if ( pDevice != nullptr )
            pDevice->assumeRenderThreadRole(); // 렌더 쪽 객체다 — 다른 스레드의 그리기는 렌더 명령으로 넘긴다(`RenderThread::executeAndWait`)

        const bool bNothingToUploadScene = pDevice == nullptr || _snapshot.getInstances().empty() || _snapshot.getAllBatches().empty();
        if ( bNothingToUploadScene )
            return false;

        // RT 에서만 하는 두 가지: 머티리얼 사본을 프록시의 상수 버퍼에 올리고, 메시를 GPU 에 올린다(정점 풀, 풀 밖이면 initRHI).
        {
            SW_PROFILE_SCOPE( "RT.GPUScene.bindMaterials" );
            GPUSceneInternal::bindBatchMaterialResources( pDevice, _snapshot.getAllBatches(), _listBatchBinding, _snapshot.getMaterialGroups() );
        }
        updateVertexPool( pDevice );
        // 머티리얼 데이터 구조버퍼(셰이더 타입별). 값이 프레임마다 바뀔 수 있어 스냅샷 dirty 와 무관하게 매번 본다(바이트가 같으면 올리지 않는다).
        uploadMaterialGroups( pDevice );
        // 불투명 · 투명 배치는 같은 목록의 구간 뷰라(GPUSceneSnapshot) 렌더 쪽 값(`_listBatchBinding`)도 한 벌이다.
        // 움직임 기록은 스냅샷이 그대로인 프레임에도 나아간다(멈춘 물체의 지난 행렬이 이번 것이 되어야 속도가 0 이다).
        updateMotionHistory( pDevice );

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
        {
            uploadInstanceAndVisibleBuffers( pDevice, instanceCount );
            refreshDistortionBatches();
        }

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
        // 정점 풀: 배치 메시 모두를 한 정점 버퍼 · 한 인덱스 버퍼에 모은다. 집합이 같으면 아무것도 하지 않고, 바뀌면 들어오고 나간 메시의 구간만 고친다(CPU 사본에서 올린다).
        // 풀에 든 배치는 풀 버퍼와 시작으로 그리고(같은 버퍼 → 멀티 드로우로 묶인다), 못 든 배치만 자기 정점 · 인덱스 버퍼(시작 0)를 만들어 그린다.
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
            // 끈 상태면 빈 목록으로 빌드해 풀이 비워진다. 배치는 아래에서 자기 정점 · 인덱스 버퍼(시작 0)로 돌아간다.
            for ( GPUMeshVertexPool& pool : _arrVertexPool ) // 각 풀이 자기 형식의 메시만 담는다
            {
                if ( pool.update( pDevice, _listScratchPoolMesh ) )
                    _bBatchTablesDirty = SW_TRUE;
            }
        }
        SW_PROFILE_SCOPE( "RT.GPUScene.uploadMeshes" );
        const vector<GPUMeshBatch>& listBatch = _snapshot.getAllBatches();
        for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
        {
            const GPUMeshBatch&                batch     = listBatch[batchIndex];
            GPUMeshBatchBinding&               gpu       = _listBatchBinding[batchIndex];
            const RHIMeshVertexFormat          format    = batch._mesh != nullptr ? batch._mesh->getVertexFormat() : RHIMeshVertexFormat::Packed;
            const GPUMeshVertexPool&           pool      = _arrVertexPool[static_cast<size_t>( format )];
            const GPUMeshVertexPool::Placement placement = pool.placementOf( batch._mesh.get() );
            if ( placement._baseVertex != GPUMeshVertexPool::kInvalidBase && pool.getVertexBuffer() != 0 )
            {
                gpu._vertexBuffer    = pool.getVertexBuffer();
                gpu._attributeOffset = pool.getAttributeOffset();
                gpu._indexBuffer     = pool.getIndexBuffer();
                gpu._baseVertex      = placement._baseVertex;
                gpu._firstIndex      = placement._firstIndex + batch._sectionFirstIndex;
                continue;
            }
            // 풀 밖(풀을 껐거나 못 든 메시) — 자기 정점 · 인덱스 버퍼로 그린다. 구간의 첫 인덱스부터.
            gpu._vertexBuffer = 0;
            gpu._indexBuffer  = 0;
            gpu._baseVertex   = 0;
            gpu._firstIndex   = batch._sectionFirstIndex;
            if ( batch._mesh == nullptr )
                continue;
            // 만드는 쪽은 하나다 — 게임 스레드가 넘기기 전에 만든 스냅샷이면(동적 메시 밖) 여기서 다시 만들지 않는다. 없으면 그 쪽이 실패했다(그쪽이 이미 알렸다).
            const bool bCreatedBeforeHandoff = _snapshot._bMeshesCreatedBeforeHandoff == SW_TRUE && batch._mesh->isDynamic() == false;
            if ( bCreatedBeforeHandoff )
            {
                if ( batch._mesh->isRHIValid() )
                {
                    gpu._vertexBuffer    = batch._mesh->getVertexBuffer();
                    gpu._attributeOffset = batch._mesh->getAttributeStreamOffset();
                    gpu._indexBuffer     = batch._mesh->getIndexBuffer();
                }
                else
                    SW_LOG_ERROR_ONCE_ID( "GPUScene.MeshNotCreatedBeforeHandoff", "A mesh in the snapshot has no vertex buffer although the upload queue owns its creation - it is not drawn" );
                continue;
            }
            if ( batch._mesh->initRHI( pDevice ) )
            {
                gpu._vertexBuffer    = batch._mesh->getVertexBuffer();
                gpu._attributeOffset = batch._mesh->getAttributeStreamOffset();
                gpu._indexBuffer     = batch._mesh->getIndexBuffer();
            }
        }
    }

    void GPUScene::uploadInstanceAndVisibleBuffers( IRHIDevice* pDevice, uint32 instanceCount )
    {
        // UnorderedAccess 를 함께 요구한다. instance_animation 컴퓨트가 월드 행렬을 고쳐 쓴다. 못 만드는
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
                const bool bFullInstanceUpload = bRecreated || _snapshot._bAllInstancesDirty != SW_FALSE || _snapshot._listDirtyInstanceRange.empty();
                if ( bFullInstanceUpload )
                    _instances.upload( pDevice, _snapshot.getInstances().data(), instanceCount * static_cast<uint32>( sizeof( GPUInstance ) ) );
                else
                    uploadDirtyInstanceRanges( pDevice, instanceCount );
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
                SW_LOG_ERROR_ONCE_ID( "GPUScene.FailedCreateVisibleInstance", "Failed to create the visible instance buffer (%# slots) of a cull view", visibleSlotCount );
        }
        // 가시 목록 항목은 아래 24비트가 인스턴스 번호다(다음 3비트는 LOD 단계, 위쪽 5비트는 크로스페이드 코드 — instance_data.hlsli `kVisibleEntryIDMask`).
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
                SW_LOG_ERROR_ONCE_ID( "GPUScene.FailedCreateLODState", "Failed to create the LOD state buffer (%# instances) of a cull view - its LOD levels switch without a crossfade", stateCount );
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

    void GPUScene::refreshDistortionBatches()
    {
        const string distortionDefine{ kMaterialDistortionDefine };
        _bDistortionBatches = SW_FALSE;
        for ( const GPUMeshBatch& batch : _snapshot.getAllBatches() )
        {
            const GPUShaderPermutation* pPermutation = batch._shaderPermutation == kInvalidShaderPermutation ? nullptr : findShaderPermutation( batch._shaderPermutation );
            if ( pPermutation != nullptr && VectorUtil::contains( pPermutation->_listDefine, distortionDefine ) )
            {
                _bDistortionBatches = SW_TRUE;
                return;
            }
        }
    }

    void GPUScene::updateMotionHistory( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GPUScene.motionHistory" );
        // shortcut: 움직이는 프레임마다 인스턴스 수만큼 열쇠를 찾고 버퍼 전체(인스턴스당 64 바이트)를 올린다 — 인스턴스가 수만을 넘어 이 구간이 측정에 드러나면 바뀐 구간만 올린다.
        _bObjectMotion                           = _motionHistory.update( _snapshot.getInstances(), _listScratchPrevWorld ) ? SW_TRUE : SW_FALSE;
        constexpr RHIBufferUsage kPrevWorldUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             count           = static_cast<uint32>( _listScratchPrevWorld.size() );
        const RHIBufferHandle    bufferBefore    = _prevInstanceWorlds._buffer;
        if ( _prevInstanceWorlds.ensureCapacity( pDevice, static_cast<uint32>( sizeof( float4x4 ) ), count, kPrevWorldUsage, true, false, _listScratchPrevWorld.data() ) == false )
        {
            if ( count > 0 )
                SW_LOG_ERROR_ONCE_ID( "GPUScene.FailedCreatePrevInstanceWorld", "Failed to create the previous instance world buffer (%# instances) - objects get camera-only velocity", count );
            _listUploadedPrevWorld.clear();
            return;
        }
        const bool bSameAsUploaded = _prevInstanceWorlds._buffer == bufferBefore && _listUploadedPrevWorld.size() == _listScratchPrevWorld.size() &&
                                     Memory::compare( _listUploadedPrevWorld.data(), _listScratchPrevWorld.data(), _listScratchPrevWorld.size() * sizeof( float4x4 ) ) == 0;
        if ( bSameAsUploaded )
            return;
        _prevInstanceWorlds.upload( pDevice, _listScratchPrevWorld.data(), count * static_cast<uint32>( sizeof( float4x4 ) ) );
        _listUploadedPrevWorld.swap( _listScratchPrevWorld );
    }

    void GPUScene::uploadDirtyInstanceRanges( IRHIDevice* pDevice, uint32 instanceCount )
    {
        // **바뀐 구간만, 그리고 한 번의 호출로 올린다.** 구간마다 따로 부르면 백엔드가
        // 스테이징 확보와 큐 제출을 그만큼 되풀이한다(DX12 에서 호출당 ~3.3 us).
        constexpr uint32 kStride = static_cast<uint32>( sizeof( GPUInstance ) );
        _listScratchCopyRegion.clear();
        _listScratchCopyRegion.reserve( _snapshot._listDirtyInstanceRange.size() );
        for ( const GPUInstanceDirtyRange& run : _snapshot._listDirtyInstanceRange )
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
            const GPUMeshBatch&        infoBatch                 = _snapshot.getAllBatches()[argIndex];
            const GPUMeshBatchBinding& infoBinding               = _listBatchBinding[argIndex];
            _listScratchBatchInfo[argIndex]._instanceBase        = infoBatch._instanceBase;
            _listScratchBatchInfo[argIndex]._instanceCount       = infoBatch._instanceCount;
            _listScratchBatchInfo[argIndex]._visibleBase         = infoBatch._visibleBase;
            _listScratchBatchInfo[argIndex]._lodLevelCount       = infoBatch._lodLevelCount;
            _listScratchBatchInfo[argIndex]._lodScreenSize       = infoBatch._lodScreenSize;
            _listScratchBatchInfo[argIndex]._morphVertexBase     = infoBinding._morphVertexBase;
            _listScratchBatchInfo[argIndex]._baseVertex          = infoBinding._baseVertex;
            _listScratchBatchInfo[argIndex]._vertexAnimationBase = infoBinding._vertexAnimationBase;
            const Mesh* pBatchMesh                               = infoBatch._mesh.get();
            const bool  bHasBounds                               = pBatchMesh != nullptr && pBatchMesh->getVertexCount() > 0;
            _listScratchBatchInfo[argIndex]._localBoundsCenter   = bHasBounds ? pBatchMesh->getLocalBoundsCenter() : float3{};
            _listScratchBatchInfo[argIndex]._localBoundsRadius   = bHasBounds ? pBatchMesh->getBoundingRadius() : 0.0f;
            _listScratchBatchInfo[argIndex]._localBoundsExtent   = bHasBounds ? ( pBatchMesh->getLocalBoundsMax() - pBatchMesh->getLocalBoundsMin() ) * 0.5f : float3{};
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
            RHIDrawIndexedIndirectCommand& cmd = _listScratchIndirectCmd[argIndex];
            cmd._indexCountPerInstance         = _snapshot.getAllBatches()[argIndex]._indexCount;
            // 컬링 컴퓨트가 개수를 만드는 배치는 **0 에서 시작**해야 한다. InterlockedAdd 로 보이는 것만 센다.
            // 압축을 포기한 배치(Preserve)와 컬링이 아예 없을 때는 CPU 가 센 개수를 그대로 쓴다.
            const bool bPreserve  = static_cast<GPUBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GPUBatchSortMode::Preserve;
            const bool bGPUCounts = ( _bGPUFillsIndirectCounts != SW_FALSE ) && ( bPreserve == false );
            cmd._instanceCount    = bGPUCounts ? 0u : _snapshot.getAllBatches()[argIndex]._instanceCount;
            // 인덱스 풀 안의 시작과 메시의 정점 풀 시작. 인덱스는 메시 안 번호다(SV_VertexID 가 baseVertex 를 포함하는지는 API 마다 다르다: binding.hlsli).
            cmd._startIndexLocation = _listBatchBinding[argIndex]._firstIndex;
            cmd._baseVertexLocation = static_cast<int32>( _listBatchBinding[argIndex]._baseVertex );
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
            SW_LOG_ERROR_ID( "GPUScene.FailedCreateInstanceSlot", "Failed to create the instance slot stream (%# instances) - scene draws cannot find their instances.", capacity );
    }

    void GPUScene::uploadIndirectArgs( IRHIDevice* pDevice, uint32 argsCount )
    {
        // 간접 인자도 **뷰마다** 하나다. 뷰별로 개수가 다르게 나오기 때문이다.
        constexpr RHIBufferUsage kArgsUsage =
            RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
        for ( GPUCullViewResources& view : _listCullView )
        {
            if ( view._indirectArgs.ensureCapacity( pDevice, static_cast<uint32>( sizeof( RHIDrawIndexedIndirectCommand ) ), argsCount,
                                                    kArgsUsage, false, true, _listScratchIndirectCmd.data() ) )
            {
                view._indirectArgs.upload( pDevice, _listScratchIndirectCmd.data(),
                                           argsCount * static_cast<uint32>( sizeof( RHIDrawIndexedIndirectCommand ) ) );
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
                                                                   argsCount * static_cast<uint32>( sizeof( RHIDrawIndexedIndirectCommand ) ) );
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
        for ( GPUMeshBatchBinding& gpu : _listBatchBinding )
        {
            gpu._materialBuffer = 0;
            gpu._materialSRV    = kInvalidDescriptorIndex;
        }
        // 슬롯이 뷰 등록 해제와 버퍼 파괴를 순서까지 맞춰 처리한다. 손으로 풀어 쓰면 뷰 하나를 빠뜨려도 컴파일은 통과한다.
        _instances.release( pDevice );
        _batchInfo.release( pDevice );
        _prevInstanceWorlds.release( pDevice );
        _listUploadedPrevWorld.clear(); // 버퍼를 다시 만들면 내용을 다시 올린다
        for ( GPUMeshVertexPool& pool : _arrVertexPool )
        {
            pool.release( pDevice );
        }
        const bool bReleasableSlotStream = _instanceSlotStream != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr;
        if ( bReleasableSlotStream )
            pDevice->getResourceFactory()->destroyBuffer( _instanceSlotStream );
        _instanceSlotStream         = 0;
        _instanceSlotStreamCapacity = 0;
        for ( GPUMeshBatchBinding& gpu : _listBatchBinding )
        {
            gpu._vertexBuffer = 0; // 풀을 놓았다. 다음 upload 가 다시 정한다
            gpu._indexBuffer  = 0;
            gpu._baseVertex   = 0;
            gpu._firstIndex   = 0;
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

    void GPUScene::applyCPUSnapshot( GPUSceneSnapshot& snapshot )
    {
        // 바꿔치기다. 지난 스냅샷의 저장소가 패킷 자리로 돌아가 다음 프레임에 재사용된다(GPUScene.h 참고).
        std::swap( _snapshot, snapshot );
        resetBatchBindings();
    }

    void GPUScene::resetBatchBindings()
    {
        // 렌더 쪽 값은 받은 배치마다 기본값에서 다시 채운다 — 예전에 프레임마다 새 배치 사본이 기본값으로 오던 것과 같은 수명이다(배치 목록이 같아도).
        _listBatchBinding.assign( _snapshot.getAllBatches().size(), GPUMeshBatchBinding{} );
    }

    uint32 GPUScene::computeMaterialGroupStride( const GPUMaterialGroup& group )
    {
        // 원소 stride 는 셰이더 리플렉션이 준 구조버퍼 원소 크기다(게임 스레드가 맞춘 `Material::getElementStride` 를 사본이 싣는다).
        // 같은 셰이더의 머티리얼은 같은 stride 를 가진다. 다르면 가장 큰 것을 쓴다.
        uint32 stride{ 0 };
        for ( const GPUMaterialElement& entry : group._listEntry )
        {
            // 레이아웃 오류(오류는 게임 스레드가 남겼다)면 바이트가 비어 stride 에 끼지 않고 원소는 복사되지 않는다. 레거시 MaterialCB 는 stride 0 이지만 바이트가 있다.
            const MaterialRenderData* pData = entry._binding._data.get();
            if ( pData == nullptr || pData->_bytes.empty() )
                continue;
            const uint32 entryStride = ( pData->_elementStride != 0 ) ? pData->_elementStride : static_cast<uint32>( pData->_bytes.size() );
            stride                   = MathUtil::max( stride, entryStride );
        }
        return stride;
    }

    void GPUScene::uploadMaterialGroup( IRHIDevice* pDevice, const GPUMaterialGroup& group )
    {
        // stride 는 셰이더가 선언한 원소 크기 그대로다. DX11 은 SRV 의 구조 stride 가 셰이더 선언(24 등)과 다르면 디버그 레이어가
        // 오류로 잡고, 네 백엔드가 DX 패킹(-fvk-use-dx-layout)으로 같은 stride 를 쓴다. 16 정렬로 키우면 DX11 이 "32 vs 24" 를 낸다.
        const uint32 stride = computeMaterialGroupStride( group );
        if ( stride == 0 )
            return;

        const uint32 elementCount = static_cast<uint32>( group._listEntry.size() );
        const uint32 needBytes    = stride * elementCount;
        _listMaterialScratch.assign( needBytes, 0 );
        for ( uint32 element = 0; element < elementCount; ++element )
        {
            const MaterialRenderData* pData = group._listEntry[element]._binding._data.get();
            if ( pData == nullptr )
                continue;
            const vector<uint8>& bytes = pData->_bytes;
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
            SW_LOG_ERROR_ID( "GPUScene.FailedCreateMaterialData", "Failed to create the material data buffer (%#, stride %#, %# elements).", group._shaderPath.c_str(), stride, elementCount );
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
        const vector<GPUMaterialGroup>& listGroup    = _snapshot.getMaterialGroups();
        const uint32                    groupCount   = static_cast<uint32>( listGroup.size() );
        vector<ResolvedGroup>&          listResolved = _listResolvedGroupScratch;
        listResolved.assign( groupCount, ResolvedGroup{} );
        for ( uint32 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const GPUMaterialGroup& group = listGroup[groupIndex];
            const auto              it    = _mapMaterialGroupBuffer.find( group._shaderPath );
            if ( it == _mapMaterialGroupBuffer.end() )
                continue;
            listResolved[groupIndex]._buffer       = it->second._slot._buffer;
            listResolved[groupIndex]._srv          = it->second._slot._srv;
            listResolved[groupIndex]._elementCount = static_cast<uint32>( group._listEntry.size() );
        }

        const vector<GPUMeshBatch>& listBatch = _snapshot.getAllBatches();
        for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
        {
            const GPUMeshBatch&  batch = listBatch[batchIndex];
            GPUMeshBatchBinding& gpu   = _listBatchBinding[batchIndex];
            gpu._materialBuffer        = 0;
            gpu._materialSRV           = kInvalidDescriptorIndex;
            gpu._materialCount         = 0;
            if ( batch._materialGroup == kInvalidMaterialGroup || batch._materialGroup >= groupCount )
                continue;
            const ResolvedGroup& resolved = listResolved[batch._materialGroup];
            gpu._materialBuffer           = resolved._buffer;
            gpu._materialSRV              = resolved._srv;
            gpu._materialCount            = resolved._elementCount;
        }
    }

    void GPUScene::uploadMaterialGroups( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GPUScene.uploadMaterials" );
        if ( pDevice == nullptr )
            return;

        for ( const GPUMaterialGroup& group : _snapshot.getMaterialGroups() )
        {
            if ( group._listEntry.empty() )
                continue;
            uploadMaterialGroup( pDevice, group );
        }
        bindBatchMaterials();
    }
} // namespace sw
