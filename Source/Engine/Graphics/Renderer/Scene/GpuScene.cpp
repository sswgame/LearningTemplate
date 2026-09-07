#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuScene.h"

#include "Core/Math/MathUtil.h"
#include "Core/Profile/FrameProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        struct GpuSceneInternal
        {
            /**
             * @brief 후보 [begin,end) 를 인스턴스 배열에 채웁니다. 범위는 서로 겹치지 않습니다.
             * @details 컨테이너가 아니라 **미리 뽑아 둔 생 포인터**를 받는다. sw::vector 의 비상수
             *          접근은 전부 "쓰기"로 집계되므로, 워커 N 개가 서로 다른 원소를 만져도
             *          DataRaceDetector 에는 "같은 컨테이너에 writer N 개"로 보인다.
             *          실제로 앱에 메시가 생기자마자 writer 17 개로 잡혀 SW_DEBUG_BREAK 이 돌았다 —
             *          씬에 메시가 없어서 이 병렬 경로가 한 번도 실행되지 않았을 뿐이었다.
             */
            template <typename TCandidate, typename TInstance>
            static void fillRangePtr( const TCandidate* pCandidate, TInstance* pInstance, uint32 begin, uint32 end )
            {
                for ( uint32 entryIndex = begin; entryIndex < end; ++entryIndex )
                {
                    const TCandidate& cand = pCandidate[entryIndex];
                    TInstance&        inst = pInstance[entryIndex];
                    inst._world            = cand._world;
                    inst._boundsCenter     = cand._boundsCenter;
                    inst._boundsRadius     = cand._boundsRadius;
                    inst._blendMode        = cand._blendMode;
                    inst._spinSeed         = cand._spinSeed;
                }
            }

            /** @brief 머티리얼의 텍스처 SRV 를 배치에 값으로 복사한다(렌더 스레드는 Material* 를 따라갈 수 없다). */
            static void fillMaterialTextureSrvs( GpuMeshBatch& batch, const Material* pMaterial )
            {
                if ( pMaterial == nullptr )
                    return;
                const vector<RHIDescriptorIndex>& listSrv = pMaterial->getMaterialTextureSrvs();
                const uint32                      count   = MathUtil::min( static_cast<uint32>( listSrv.size() ),
                                                                           shaderslot::kMaterialTextureCount );
                for ( uint32 texIndex = 0; texIndex < count; ++texIndex )
                    batch._arrMaterialTexSrv[texIndex] = listSrv[texIndex];
            }

            static void applyInstanceCbsVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._pMaterialInstance == nullptr )
                        continue;
                    if ( batch._pMaterialInstance->applyToGpu( pDevice ) )
                        batch._materialCb = batch._pMaterialInstance->getDescriptorIndex();
                }
            }

            static void uploadMeshesVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._pMesh != nullptr && batch._pMesh->upload( pDevice ) )
                        batch._vertexBuffer = batch._pMesh->getVertexBuffer();
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GpuScene::invalidateBuildCache()
    {
        _listBuiltCandidate.clear();
        _lastCameraPos              = float3{};
        _lastPrimitiveSetGeneration = 0;
        _bCpuDirty                  = 1;
    }

    void GpuScene::clear()
    {
        _listInstance.clear();
        _listOpaqueBatch.clear();
        _listTransparentBatch.clear();
        _listAllBatch.clear();
        _listMaterialGroup.clear();
        _indirectCommandCount = 0;
        _listScratchCandidate.clear();
        _listScratchRaw.clear();
        _listScratchOpaqueEntry.clear();
        _listScratchTransparentIdx.clear();
        invalidateBuildCache();
        _materialRetire.clear();
    }

    void GpuScene::syncMaterialPins()
    {
        _materialRetire.syncFromBatches( _listOpaqueBatch, _listTransparentBatch, _listMaterialGroup );
    }

    void GpuScene::setMergeBatchesAcrossMaterials( bool bMerge )
    {
        const uint8 value = bMerge ? 1 : 0;
        if ( _bMergeAcrossMaterials == value )
            return;
        _bMergeAcrossMaterials = value;
        // 합치기 여부가 배치 키를 바꾼다 = 원소 구성이 달라진다. 영속 인덱스를 그대로 두면 예전 기준의 자리가 남는다.
        resetMaterialRegistry();
        invalidateBuildCache();
    }

    Material* GpuScene::batchKeyMaterial( Material* pMaterial )
    {
        if ( _bMergeAcrossMaterials == 0 || pMaterial == nullptr )
            return pMaterial;
        auto it = _mapShaderRepresentative.find( pMaterial->getShaderPath() );
        if ( it != _mapShaderRepresentative.end() )
            return it->second;
        _mapShaderRepresentative.emplace( pMaterial->getShaderPath(), pMaterial );
        return pMaterial;
    }

    void GpuMaterialRetireQueue::clear()
    {
        _uniquePinned.clear();
        _listRetiring.clear();
    }

    bool GpuMaterialRetireQueue::isPinned( const MaterialInstance* pInstance ) const
    {
        if ( pInstance == nullptr )
            return false;
        if ( _uniquePinned.find( const_cast<MaterialInstance*>( pInstance ) ) != _uniquePinned.end() )
            return true;
        for ( const RetireEntry& retireEntry : _listRetiring )
        {
            if ( retireEntry._pInstance == pInstance )
                return true;
        }
        return false;
    }

    void GpuMaterialRetireQueue::syncFromBatches( const vector<GpuMeshBatch>& listOpaque, const vector<GpuMeshBatch>& listTransparent, const vector<GpuMaterialGroup>& listGroup )
    {
        unordered_set<MaterialInstance*> uniqueLive;
        auto                             collect = [&]( const vector<GpuMeshBatch>& listBatch )
        {
            for ( const GpuMeshBatch& batch : listBatch )
            {
                if ( batch._pMaterialInstance != nullptr )
                    uniqueLive.insert( batch._pMaterialInstance );
            }
        };
        collect( listOpaque );
        collect( listTransparent );
        for ( const GpuMaterialGroup& group : listGroup )
        {
            for ( const auto& entry : group._listEntry )
            {
                if ( entry.second != nullptr )
                    uniqueLive.insert( entry.second );
            }
        }

        for ( MaterialInstance* pInst : _uniquePinned )
        {
            if ( uniqueLive.find( pInst ) == uniqueLive.end() )
            {
                RetireEntry entry{};
                entry._pInstance  = pInst;
                entry._framesLeft = kRetireFrameDelay;
                _listRetiring.push_back( entry );
            }
        }

        _uniquePinned = std::move( uniqueLive );
    }

    void GpuMaterialRetireQueue::advanceFrame()
    {
        uint32 write{ 0 };
        for ( uint32 index = 0; index < _listRetiring.size(); ++index )
        {
            RetireEntry& entry = _listRetiring[index];
            if ( entry._framesLeft > 0 )
                --entry._framesLeft;
            if ( entry._framesLeft > 0 )
            {
                _listRetiring[write++] = entry;
            }
        }
        _listRetiring.resize( write );
    }

    void GpuMaterialRetireQueue::flushAfterGpu( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
            pDevice->waitIdle();
        clear();
    }

    bool GpuScene::hasSameBatchKeysAsBuilt() const
    {
        if ( _listBuiltCandidate.size() != _listScratchCandidate.size() )
            return false;
        // 후보 순서는 등록부 순서를 그대로 따른다. 등록/해제가 있었으면 집합 세대가 올라가 여기까지
        // 오지 않고, 가시성 변화는 개수를 바꾸므로 위 크기 비교에서 걸린다.
        for ( size_t index = 0; index < _listBuiltCandidate.size(); ++index )
        {
            if ( _listBuiltCandidate[index].hasSameBatchKey( _listScratchCandidate[index] ) == false )
                return false;
        }
        return true;
    }

    void GpuScene::rebuildPartitionTables()
    {
        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        _listScratchOpaqueEntry.clear();
        _listScratchOpaqueEntry.reserve( count );
        _listScratchTransparentIdx.clear();
        _listScratchTransparentIdx.reserve( count );
        _mapShaderRepresentative.clear();

        for ( uint32 instanceIndex = 0; instanceIndex < count; ++instanceIndex )
        {
            const DrawCandidate& cand = _listScratchCandidate[instanceIndex];
            if ( static_cast<RHIBlendMode>( cand._blendMode ) == RHIBlendMode::Transparent )
            {
                _listScratchTransparentIdx.push_back( instanceIndex );
                continue;
            }
            // 합치기가 켜져 있으면 같은 셰이더 타입은 머티리얼·인스턴스가 달라도 한 키다 — 파라미터는 원소 인덱스로 읽는다.
            SortKey key{ cand._pMesh, batchKeyMaterial( cand._pMaterial ), _bMergeAcrossMaterials != 0 ? nullptr : cand._pInstance };
            _listScratchOpaqueEntry.push_back( SortEntry{ key, instanceIndex } );
        }

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            std::sort( _listScratchOpaqueEntry.begin(), _listScratchOpaqueEntry.end(), []( const SortEntry& entryA, const SortEntry& entryB )
            {
                if ( entryA._key._pMesh != entryB._key._pMesh )
                    return entryA._key._pMesh < entryB._key._pMesh;
                if ( entryA._key._pMaterial != entryB._key._pMaterial )
                    return entryA._key._pMaterial < entryB._key._pMaterial;
                return entryA._key._pInstance < entryB._key._pInstance;
            } );
        }
    }

    void GpuScene::fillScratchRange( uint32 start, uint32 end )
    {
        // 포인터는 병렬 구간에 들어가기 전에 뽑아 둔다(prepareScratchPointers).
        GpuSceneInternal::fillRangePtr( _pScratchCandidateBase, _pScratchRawBase, start, end );
    }

    void GpuScene::buildFromScene( Scene* pScene, const float3& cameraPos,
                                   TaskManager* pTaskManager )
    {
        SW_PROFILE_SCOPE( "GT.GpuScene.build" );

        if ( pScene == nullptr )
        {
            clear();
            return;
        }

        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
        {
            clear();
            return;
        }

        // 여기서 움직인 프리미티브가 onWorldTransformUpdated 를 통해 스스로 더티를 찍는다.
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.flushTransforms" );
            pObjects->flushSceneTransforms();
        }

        const PrimitiveRegistry& primitives    = pObjects->getPrimitiveRegistry();
        const uint64             setGeneration = primitives.getSetGeneration();
        const bool               bHasCache     = _listBuiltCandidate.empty() == false;
        const bool               bSetSame      = bHasCache && ( setGeneration == _lastPrimitiveSetGeneration );
        const bool               bCamSame      = bHasCache && ( float3::getDistanceSquared( cameraPos, _lastCameraPos ) <= MathUtil::Epsilon );

        // 아무도 "바뀌었다"고 말하지 않았고 카메라도 그대로면 **수집 자체를 하지 않는다**.
        // 예전엔 이 판단을 하려고 매 프레임 모든 GameObject 의 모든 Component 를 castTo 로 훑어
        // 후보를 다 만든 다음에야 "그대로였네" 하고 버렸다. 씬이 커질수록, 화면이 정지해 있어도
        // 비용이 늘었다. 이제는 바꾼 쪽이 알려주므로 정지한 씬의 비용이 0 에 수렴한다.
        if ( bSetSame && bCamSame && primitives.hasDirty() == false )
            return;

        pObjects->getPrimitiveRegistry().clearDirty();

        const vector<MeshComponent*>& listPrimitive = primitives.getAll();
        _listScratchCandidate.clear();
        _listScratchCandidate.reserve( listPrimitive.size() );

        // 등록부에는 그릴 수 있는 것만 들어 있다 — 타입 검사가 없다.
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.collect" );
            for ( MeshComponent* pMeshComp : listPrimitive )
            {
                if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
                    continue;
                GameObject* pObj = pMeshComp->getOwner();
                if ( pObj == nullptr || pObj->isActiveInHierarchy() == false )
                    continue;
                Mesh* pMesh = pMeshComp->getRawMesh();
                if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                    continue;

                const float4x4 world = pMeshComp->getWorldMatrix();
                DrawCandidate  cand{};
                cand._world        = world;
                cand._boundsCenter = world.getTranslation();
                cand._boundsRadius = pMeshComp->getBoundsRadius();
                cand._blendMode    = static_cast<uint32>( pMeshComp->getBlendMode() );
                cand._spinSeed     = pMeshComp->getGpuSpinSeed();
                cand._pMesh        = pMesh;
                cand._pMaterial    = pMeshComp->getMaterial();
                if ( cand._pMaterial == nullptr )
                    cand._pMaterial = pScene->getMaterial(); // 머티리얼 없는 메시는 씬 기본 머티리얼로 (언리얼의 기본 머티리얼)
                cand._pInstance = pMeshComp->getRawMaterialInstance();
                _listScratchCandidate.push_back( cand );
            }
        }

        if ( _listScratchCandidate.empty() )
        {
            clear();
            return;
        }

        // 더티 신호가 왔다고 내용이 실제로 달라졌다는 뜻은 아니다(집합 세대는 활성 토글 같은 것에도
        // 올라간다). 여기서 한 번 더 확인해 헛된 재구축을 막는다.
        bool bContentSame = false;
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.compare" );
            bContentSame = bHasCache && _listBuiltCandidate == _listScratchCandidate && _listInstance.empty() == false;
        }

        if ( bContentSame && bCamSame )
        {
            _lastPrimitiveSetGeneration = setGeneration;
            return;
        }

        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        _listScratchRaw.resize( count );

        // 물체가 움직이기만 했으면 배치 구성은 그대로다 — 인스턴스 값만 새로 채우고, 나누기와
        // 정렬은 건너뛴다. 움직이는 씬에서 남아 있던 유일한 O(N log N) 이 이 정렬이었다.
        const bool bBatchKeysSame = bHasCache && bContentSame == false && hasSameBatchKeysAsBuilt();

        if ( bContentSame == false )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.fill" );
            // 병렬로 나눠 쓰기 전에 버퍼 주소를 한 번만 확정한다. 워커가 컨테이너를 직접 만지면
            // 서로 다른 원소를 써도 레이스 감지기가 "writer N 개"로 본다.
            _pScratchCandidateBase = _listScratchCandidate.data();
            _pScratchRawBase       = _listScratchRaw.data();

            if ( pTaskManager != nullptr && count >= 8 && pTaskManager->getWorkerCount() > 0 )
            {
                if ( _snapshotStage.isValid() == false )
                    _snapshotStage = pTaskManager->createAnonymousStage( "GpuSceneSnapshot" );

                TaskHandle handle = pTaskManager->emplaceParallelBlock(
                    0, count, SW_DELEGATE_METHOD( ParallelBlockDelegate, &GpuScene::fillScratchRange, this ) );
                _snapshotStage.addTask( handle );
                handle.submit();
                pTaskManager->waitStage( _snapshotStage );
            }
            else
                GpuSceneInternal::fillRangePtr( _pScratchCandidateBase, _pScratchRawBase, 0, count );

            if ( bBatchKeysSame == false )
            {
                SW_PROFILE_SCOPE( "GT.GpuScene.build.partition" );
                rebuildPartitionTables();
            }
        }

        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.batches" );
            sortTransparent( &cameraPos._x );

            _listInstance.clear();
            _listOpaqueBatch.clear();
            _listTransparentBatch.clear();
            _listAllBatch.clear();
            buildBatches();
            retireUnusedMaterialElements();
        }

        // scratch 를 기준 집합으로 넘기고 낡은 기준을 scratch 로 돌려받는다 — 복사 없이 두 버퍼를
        // 번갈아 쓰므로 프레임당 힙 할당이 생기지 않는다.
        _listBuiltCandidate.swap( _listScratchCandidate );
        _lastCameraPos              = cameraPos;
        _lastPrimitiveSetGeneration = setGeneration;
        _bCpuDirty                  = 1;
    }

    bool GpuScene::upload( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GpuScene.upload" );

        if ( pDevice == nullptr || _listInstance.empty() || _listAllBatch.empty() )
            return false;

        // RT-owned context: pack MaterialInstance overrides and upload meshes in a single pass.
        GpuSceneInternal::applyInstanceCbsVal( pDevice, _listAllBatch );
        GpuSceneInternal::uploadMeshesVal( pDevice, _listAllBatch );
        // 머티리얼 데이터 구조버퍼(셰이더 타입별) — 값이 프레임마다 바뀔 수 있어 스냅샷 dirty 와 무관하게 매번 올린다.
        uploadMaterialGroups( pDevice );

        const size_t opaqueCount = _listOpaqueBatch.size();
        for ( size_t batchIndex = 0; batchIndex < opaqueCount; ++batchIndex )
        {
            _listOpaqueBatch[batchIndex]._materialCb   = _listAllBatch[batchIndex]._materialCb;
            _listOpaqueBatch[batchIndex]._vertexBuffer = _listAllBatch[batchIndex]._vertexBuffer;
        }
        for ( size_t batchIndex = 0; batchIndex < _listTransparentBatch.size(); ++batchIndex )
        {
            _listTransparentBatch[batchIndex]._materialCb   = _listAllBatch[opaqueCount + batchIndex]._materialCb;
            _listTransparentBatch[batchIndex]._vertexBuffer = _listAllBatch[opaqueCount + batchIndex]._vertexBuffer;
        }

        // CPU 스냅샷이 그대로면 인스턴스 버퍼 재업로드를 생략한다. **간접 인자는 예외다** —
        // 컬링 컴퓨트가 개수를 InterlockedAdd 로 만드는 동안에는 매 프레임 0 으로 되돌려 놓아야 한다.
        // 여기서 같이 건너뛰었더니 정적 씬에서 개수가 N, 2N, 3N ... 으로 끝없이 자랐다(드로우 비용이
        // 계속 늘고, 이번 프레임에 쓰지 않은 가시 목록 자리를 읽는다). 움직이는 벤치와 한 프레임만
        // 그리는 테스트가 둘 다 이걸 가리고 있었다.
        if ( _bCpuDirty == 0 && _instanceBuffer != 0 && getIndirectArgsBuffer() != 0 )
        {
            if ( _bGpuFillsIndirectCounts != 0 )
                refreshIndirectCounts( pDevice );
            return true;
        }

        const uint32 instanceCount = static_cast<uint32>( _listInstance.size() );
        const uint32 argsCount     = static_cast<uint32>( _listAllBatch.size() );

        if ( _instanceBuffer == 0 || _instanceCapacity < instanceCount )
        {
            if ( _instanceBuffer != 0 )
            {
                if ( _instanceSrv != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessResource( _instanceSrv );
                if ( _instanceUav != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessUAV( _instanceUav );
                pDevice->getResource()->destroyBuffer( _instanceBuffer );
                _instanceBuffer = 0;
                _instanceSrv    = kInvalidDescriptorIndex;
                _instanceUav    = kInvalidDescriptorIndex;
            }
            RHIBufferDesc desc{};
            desc._elementSize  = static_cast<uint32>( sizeof( GpuInstance ) );
            desc._elementCount = instanceCount;
            desc._sizeBytes    = desc._elementSize * desc._elementCount;
            // UnorderedAccess 를 함께 요구한다 — instanceanim 컴퓨트가 월드 행렬을 고쳐 쓴다. 못 만드는
            // 백엔드/드라이버면 아래에서 SRV 전용으로 한 번 더 시도해 그리기는 그대로 살린다.
            desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            desc._pInitialData = _listInstance.data();
            _instanceBuffer    = pDevice->getResource()->createBuffer( desc );
            if ( _instanceBuffer == 0 )
            {
                desc._usage     = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
                _instanceBuffer = pDevice->getResource()->createBuffer( desc );
            }
            if ( _instanceBuffer == 0 )
            {
                _instanceBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
                if ( _instanceBuffer != 0 )
                    pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstance.data(), desc._sizeBytes );
            }
            if ( _instanceBuffer != 0 )
            {
                _instanceSrv      = pDevice->getResource()->registerBindlessResource( _instanceBuffer );
                _instanceUav      = pDevice->getResource()->registerBindlessUAV( _instanceBuffer );
                _instanceCapacity = instanceCount;
            }
        }
        else
        {
            pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstance.data(),
                                                            instanceCount * static_cast<uint32>( sizeof( GpuInstance ) ) );
        }

        // 가시 인스턴스 ID 버퍼 — 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해
        // 넣고(언리얼 FInstanceCullingContext 의 InstanceIdBuffer), 정점 셰이더가 그 순서로 읽는다.
        // **뷰마다 하나씩**이다 — 목록은 절두체에 종속이라 메인 카메라로 거른 것을 그림자가 쓰면 안 된다.
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( GpuCullView::Count ); ++viewIndex )
        {
            GpuCullViewResources& view = _arrCullView[viewIndex];
            if ( view._visibleInstanceBuffer != 0 && view._visibleCapacity >= instanceCount )
                continue;

            if ( view._visibleInstanceBuffer != 0 )
            {
                if ( view._visibleInstanceSrv != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessResource( view._visibleInstanceSrv );
                if ( view._visibleInstanceUav != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessUAV( view._visibleInstanceUav );
                pDevice->getResource()->destroyBuffer( view._visibleInstanceBuffer );
                view._visibleInstanceBuffer = 0;
                view._visibleInstanceSrv    = kInvalidDescriptorIndex;
                view._visibleInstanceUav    = kInvalidDescriptorIndex;
            }
            if ( instanceCount == 0 )
                continue;

            RHIBufferDesc desc{};
            desc._elementSize           = static_cast<uint32>( sizeof( uint32 ) );
            desc._elementCount          = instanceCount;
            desc._sizeBytes             = desc._elementSize * desc._elementCount;
            desc._usage                 = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            view._visibleInstanceBuffer = pDevice->getResource()->createBuffer( desc );
            if ( view._visibleInstanceBuffer != 0 )
            {
                view._visibleInstanceSrv = pDevice->getResource()->registerBindlessResource( view._visibleInstanceBuffer );
                view._visibleInstanceUav = pDevice->getResource()->registerBindlessUAV( view._visibleInstanceBuffer );
                view._visibleCapacity    = instanceCount;
            }
        }

        // 배치 구간 — 컬링 컴퓨트가 "이 배치의 인스턴스는 어디서 시작하나"를 읽는다.
        _listScratchBatchInfo.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            _listScratchBatchInfo[argIndex]._instanceBase   = _listAllBatch[argIndex]._instanceBase;
            _listScratchBatchInfo[argIndex]._instanceCount  = _listAllBatch[argIndex]._instanceCount;
            _listScratchBatchInfo[argIndex]._bPreserveOrder = ( _listAllBatch[argIndex]._blendMode == RHIBlendMode::Transparent ) ? 1u : 0u;
        }
        if ( _batchInfoBuffer == 0 || _batchInfoCapacity < argsCount )
        {
            if ( _batchInfoBuffer != 0 )
            {
                if ( _batchInfoSrv != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessResource( _batchInfoSrv );
                pDevice->getResource()->destroyBuffer( _batchInfoBuffer );
                _batchInfoBuffer = 0;
                _batchInfoSrv    = kInvalidDescriptorIndex;
            }
            if ( argsCount > 0 )
            {
                RHIBufferDesc desc{};
                desc._elementSize  = static_cast<uint32>( sizeof( GpuBatchInfo ) );
                desc._elementCount = argsCount;
                desc._sizeBytes    = desc._elementSize * desc._elementCount;
                desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
                desc._pInitialData = _listScratchBatchInfo.data();
                _batchInfoBuffer   = pDevice->getResource()->createBuffer( desc );
                if ( _batchInfoBuffer != 0 )
                {
                    _batchInfoSrv      = pDevice->getResource()->registerBindlessResource( _batchInfoBuffer );
                    _batchInfoCapacity = argsCount;
                }
            }
        }
        else if ( argsCount > 0 )
        {
            pDevice->getResource()->updateStructuredBuffer( _batchInfoBuffer, _listScratchBatchInfo.data(),
                                                            argsCount * static_cast<uint32>( sizeof( GpuBatchInfo ) ) );
        }

        // 컴퓨트가 개수를 만들려면 **가시 목록과 배치 구간이 둘 다** 있어야 한다. 하나라도 없으면 개수를
        // 0 으로 올리면 안 된다 — 컬링이 못 도는데 개수가 0 이면 그 프레임은 아무것도 안 그려진다.
        // 그래서 "원한다"(_bWantGpuIndirectCounts)와 "실제로 된다"(_bGpuFillsIndirectCounts)를 나눠 둔다.
        _bGpuFillsIndirectCounts =
            ( _bWantGpuIndirectCounts != 0 && _batchInfoBuffer != 0 && hasAllCullViewBuffers() ) ? 1u : 0u;

        _listScratchIndirectCmd.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            _listScratchIndirectCmd[argIndex]._vertexCount = _listAllBatch[argIndex]._vertexCount;
            // 컬링 컴퓨트가 개수를 만드는 배치는 **0 에서 시작**해야 한다 — InterlockedAdd 로 보이는 것만 센다.
            // 순서를 지켜야 하는 배치(투명)와 컬링이 아예 없을 때는 CPU 가 센 개수를 그대로 쓴다.
            const bool bGpuCounts                                  = ( _bGpuFillsIndirectCounts != 0 ) &&
                                                                     ( _listAllBatch[argIndex]._blendMode != RHIBlendMode::Transparent );
            _listScratchIndirectCmd[argIndex]._instanceCount       = bGpuCounts ? 0u : _listAllBatch[argIndex]._instanceCount;
            _listScratchIndirectCmd[argIndex]._startVertexLocation = 0;
            // **0 이어야 한다.** 배치의 인스턴스 시작 오프셋은 셰이더가 루트 상수(g_InstanceBase)로 더한다.
            // 여기에도 넣으면 Vulkan 에서만 두 번 더해진다 — DX 의 SV_InstanceID 는 StartInstanceLocation 을
            // 포함하지 않지만 SPIR-V 의 InstanceIndex 는 firstInstance 를 **포함**하기 때문이다(GL 은 빌드 때
            // InstanceId 로 바꿔 구우므로 DX 와 같다). 그래서 배치가 둘 이상일 때 Vulkan 만 엉뚱한 인스턴스를
            // 읽어 큐브가 겹쳐 그려졌다 — 벤치가 메시를 하나만 쓰던 동안(instanceBase 가 늘 0) 드러나지 않았다.
            _listScratchIndirectCmd[argIndex]._startInstanceLocation = 0;
        }

        // 간접 인자도 **뷰마다** 하나다 — 뷰별로 개수가 다르게 나오기 때문이다.
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( GpuCullView::Count ); ++viewIndex )
        {
            GpuCullViewResources& view = _arrCullView[viewIndex];
            if ( view._indirectArgsBuffer == 0 || view._argsCapacity < argsCount )
            {
                if ( view._indirectArgsBuffer != 0 )
                {
                    if ( view._indirectArgsUav != kInvalidDescriptorIndex )
                        pDevice->getResource()->unregisterBindlessUAV( view._indirectArgsUav );
                    pDevice->getResource()->destroyBuffer( view._indirectArgsBuffer );
                    view._indirectArgsBuffer = 0;
                    view._indirectArgsUav    = kInvalidDescriptorIndex;
                }
                if ( argsCount == 0 )
                    continue;

                RHIBufferDesc desc{};
                desc._elementSize        = static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) );
                desc._elementCount       = argsCount;
                desc._sizeBytes          = desc._elementSize * desc._elementCount;
                desc._usage              = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
                desc._pInitialData       = _listScratchIndirectCmd.data();
                view._indirectArgsBuffer = pDevice->getResource()->createBuffer( desc );
                if ( view._indirectArgsBuffer == 0 )
                {
                    view._indirectArgsBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
                    if ( view._indirectArgsBuffer != 0 )
                        pDevice->getResource()->updateStructuredBuffer( view._indirectArgsBuffer, _listScratchIndirectCmd.data(), desc._sizeBytes );
                }
                if ( view._indirectArgsBuffer != 0 )
                {
                    view._indirectArgsUav = pDevice->getResource()->registerBindlessUAV( view._indirectArgsBuffer );
                    view._argsCapacity    = argsCount;
                }
            }
            else if ( argsCount > 0 )
            {
                pDevice->getResource()->updateStructuredBuffer( view._indirectArgsBuffer, _listScratchIndirectCmd.data(),
                                                                argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
            }
        }

        _indirectCommandCount = argsCount;
        _bCpuDirty            = 0;

        return _instanceBuffer != 0 && getIndirectArgsBuffer() != 0;
    }

    bool GpuScene::hasAllCullViewBuffers() const
    {
        for ( const GpuCullViewResources& view : _arrCullView )
        {
            if ( view._indirectArgsBuffer == 0 || view._visibleInstanceBuffer == 0 )
                return false;
        }
        return true;
    }

    void GpuScene::refreshIndirectCounts( IRHIDevice* pDevice )
    {
        const uint32 argsCount = static_cast<uint32>( _listAllBatch.size() );
        if ( pDevice == nullptr || argsCount == 0 || _listScratchIndirectCmd.size() < argsCount )
            return;

        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            const bool bGpuCounts                            = ( _listAllBatch[argIndex]._blendMode != RHIBlendMode::Transparent );
            _listScratchIndirectCmd[argIndex]._instanceCount = bGpuCounts ? 0u : _listAllBatch[argIndex]._instanceCount;
        }
        for ( GpuCullViewResources& view : _arrCullView )
        {
            if ( view._indirectArgsBuffer == 0 )
                continue;
            pDevice->getResource()->updateStructuredBuffer( view._indirectArgsBuffer, _listScratchIndirectCmd.data(),
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
        _bCpuDirty = 1;
    }

    void GpuScene::releaseGpu( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        for ( auto& pair : _mapMaterialGpu )
        {
            if ( pair.second._srv != kInvalidDescriptorIndex )
                pDevice->getResource()->unregisterBindlessResource( pair.second._srv );
            if ( pair.second._buffer != 0 )
                pDevice->getResource()->destroyBuffer( pair.second._buffer );
        }
        _mapMaterialGpu.clear();
        for ( GpuMeshBatch& batch : _listAllBatch )
        {
            batch._materialBuffer = 0;
            batch._materialSrv    = kInvalidDescriptorIndex;
        }
        if ( _instanceSrv != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessResource( _instanceSrv );
        if ( _instanceUav != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessUAV( _instanceUav );
        for ( GpuCullViewResources& view : _arrCullView )
        {
            if ( view._visibleInstanceSrv != kInvalidDescriptorIndex )
                pDevice->getResource()->unregisterBindlessResource( view._visibleInstanceSrv );
            if ( view._visibleInstanceUav != kInvalidDescriptorIndex )
                pDevice->getResource()->unregisterBindlessUAV( view._visibleInstanceUav );
            if ( view._indirectArgsUav != kInvalidDescriptorIndex )
                pDevice->getResource()->unregisterBindlessUAV( view._indirectArgsUav );
            if ( view._visibleInstanceBuffer != 0 )
                pDevice->getResource()->destroyBuffer( view._visibleInstanceBuffer );
            if ( view._indirectArgsBuffer != 0 )
                pDevice->getResource()->destroyBuffer( view._indirectArgsBuffer );
            view = GpuCullViewResources{};
        }
        if ( _batchInfoSrv != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessResource( _batchInfoSrv );
        if ( _instanceBuffer != 0 )
            pDevice->getResource()->destroyBuffer( _instanceBuffer );
        if ( _batchInfoBuffer != 0 )
            pDevice->getResource()->destroyBuffer( _batchInfoBuffer );
        _instanceBuffer       = 0;
        _instanceSrv          = kInvalidDescriptorIndex;
        _instanceUav          = kInvalidDescriptorIndex;
        _batchInfoBuffer      = 0;
        _batchInfoSrv         = kInvalidDescriptorIndex;
        _batchInfoCapacity    = 0;
        _instanceCapacity     = 0;
        _indirectCommandCount = 0;
        _spinInstanceCount    = 0;
        _bCpuDirty            = 1;
        _materialRetire.flushAfterGpu( pDevice );
    }

    void GpuScene::exportCpuSnapshot( GpuScene& outSnapshot )
    {
        outSnapshot._spinInstanceCount    = _spinInstanceCount;
        outSnapshot._listInstance         = _listInstance;
        outSnapshot._listOpaqueBatch      = _listOpaqueBatch;
        outSnapshot._listTransparentBatch = _listTransparentBatch;
        outSnapshot._listAllBatch         = _listAllBatch;
        outSnapshot._listMaterialGroup    = _listMaterialGroup;
        outSnapshot._indirectCommandCount = _indirectCommandCount;
        outSnapshot._bCpuDirty            = _bCpuDirty;
        _bCpuDirty                        = 0;
    }

    void GpuScene::adoptCpuSnapshot( GpuScene&& snapshot )
    {
        _listInstance         = std::move( snapshot._listInstance );
        _listOpaqueBatch      = std::move( snapshot._listOpaqueBatch );
        _listTransparentBatch = std::move( snapshot._listTransparentBatch );
        _listAllBatch         = std::move( snapshot._listAllBatch );
        _listMaterialGroup    = std::move( snapshot._listMaterialGroup );
        _indirectCommandCount = snapshot._indirectCommandCount;
        _spinInstanceCount    = snapshot._spinInstanceCount;
        _bCpuDirty            = snapshot._bCpuDirty;
    }

    void GpuScene::sortTransparent( const float32* pCameraPos )
    {
        if ( pCameraPos == nullptr || _listScratchTransparentIdx.size() <= 1 )
            return;
        const float3 camPos{ pCameraPos[0], pCameraPos[1], pCameraPos[2] };
        std::sort( _listScratchTransparentIdx.begin(), _listScratchTransparentIdx.end(), [&]( uint32 idxA, uint32 idxB )
        { return float3::getDistanceSquared( _listScratchRaw[idxA]._boundsCenter, camPos ) > float3::getDistanceSquared( _listScratchRaw[idxB]._boundsCenter, camPos ); } );
    }

    void GpuScene::buildBatches()
    {
        _listInstance.reserve( _listScratchCandidate.size() );
        _spinInstanceCount = 0;

        // **머티리얼 원소 인덱스는 프레임을 넘어 유지된다** (언리얼 GPUScene 의 영속 PrimitiveID 와 같은 자리).
        // 예전엔 여기서 그룹을 통째로 지우고 인스턴스마다 다시 부여했다 — 인스턴스 N 개와 머티리얼 M 종에
        // O(N·M) 이었고, 무엇보다 같은 머티리얼의 인덱스가 프레임마다 달라져 "바뀐 것만 올린다" 를 할 수 없었다.
        // 이제 처음 본 (머티리얼, 인스턴스) 쌍에만 자리를 주고, 안 쓰이면 아래 retireUnusedMaterialElements 가
        // 지연 회수한다. 자리를 옮기지 않으므로 인덱스는 안정적이다.
        ++_buildCounter;
        for ( GpuMaterialGroup& group : _listMaterialGroup )
            group._bHasLast = 0;

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchOpaqueEntry.size(); ++entryIndex )
            {
                if ( entryIndex == _listScratchOpaqueEntry.size() || ( _listScratchOpaqueEntry[entryIndex]._key == _listScratchOpaqueEntry[batchStart]._key ) == false )
                {
                    const SortKey& key = _listScratchOpaqueEntry[batchStart]._key;
                    GpuMeshBatch   batch{};
                    batch._pMesh             = key._pMesh;
                    batch._vertexCount       = batch._pMesh->getVertexCount();
                    batch._instanceBase      = static_cast<uint32>( _listInstance.size() );
                    batch._instanceCount     = entryIndex - batchStart;
                    batch._blendMode         = RHIBlendMode::Opaque;
                    batch._pMaterialInstance = key._pInstance;
                    batch._pMaterial         = key._pMaterial;
                    if ( key._pInstance != nullptr )
                        batch._materialCb = key._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = key._pMaterial ? key._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;
                    // 텍스처 슬롯은 인스턴스가 아니라 부모 머티리얼이 소유한다(인스턴스는 CB 값만 덮어쓴다).
                    GpuSceneInternal::fillMaterialTextureSrvs( batch, key._pMaterial );
                    batch._materialGroup = materialGroupFor( key._pMaterial );
                    batch._materialIndex = 0;

                    // 인스턴스마다 자기 (머티리얼, 인스턴스) 원소를 받는다 — 합치기가 켜져 있으면 한 배치에 여러 머티리얼이 산다.
                    const uint32 batchIndex = static_cast<uint32>( _listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        const uint32         srcIdx = _listScratchOpaqueEntry[batchEntryIndex]._srcIdx;
                        const DrawCandidate& cand   = _listScratchCandidate[srcIdx];
                        GpuInstance          inst   = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex        = batchIndex;
                        inst._materialIndex         = assignMaterialElement( cand._pMaterial, cand._pInstance, batch._materialGroup );
                        if ( batchEntryIndex == batchStart )
                            batch._materialIndex = inst._materialIndex;
                        if ( inst._spinSeed != 0 )
                            ++_spinInstanceCount;
                        _listInstance.push_back( inst );
                    }
                    _listOpaqueBatch.push_back( batch );
                    _listAllBatch.push_back( batch );

                    batchStart = entryIndex;
                }
            }
        }

        // back-to-front 정렬된 transparent 인덱스에서 연속 동일 mesh/mat/instance 만 머지.
        if ( _listScratchTransparentIdx.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchTransparentIdx.size(); ++entryIndex )
            {
                const bool bEnd = entryIndex == _listScratchTransparentIdx.size();
                bool       bKeyChange{ false };
                if ( bEnd == false )
                {
                    const DrawCandidate& a = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    const DrawCandidate& b = _listScratchCandidate[_listScratchTransparentIdx[entryIndex]];
                    bKeyChange             = ( a._pMesh != b._pMesh ) || ( batchKeyMaterial( a._pMaterial ) != batchKeyMaterial( b._pMaterial ) ) ||
                                             ( _bMergeAcrossMaterials == 0 && a._pInstance != b._pInstance );
                }
                if ( bEnd || bKeyChange )
                {
                    const DrawCandidate& cand = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    GpuMeshBatch         batch{};
                    batch._pMesh             = cand._pMesh;
                    batch._vertexCount       = batch._pMesh->getVertexCount();
                    batch._instanceBase      = static_cast<uint32>( _listInstance.size() );
                    batch._instanceCount     = entryIndex - batchStart;
                    batch._blendMode         = RHIBlendMode::Transparent;
                    batch._pMaterialInstance = cand._pInstance;
                    batch._pMaterial         = cand._pMaterial;
                    if ( cand._pInstance != nullptr )
                        batch._materialCb = cand._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = cand._pMaterial ? cand._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;
                    GpuSceneInternal::fillMaterialTextureSrvs( batch, cand._pMaterial );
                    batch._materialGroup = materialGroupFor( cand._pMaterial );
                    batch._materialIndex = 0;

                    const uint32 batchIndex = static_cast<uint32>( _listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        const uint32         srcIdx    = _listScratchTransparentIdx[batchEntryIndex];
                        const DrawCandidate& entryCand = _listScratchCandidate[srcIdx];
                        GpuInstance          inst      = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex           = batchIndex;
                        inst._materialIndex            = assignMaterialElement( entryCand._pMaterial, entryCand._pInstance, batch._materialGroup );
                        if ( batchEntryIndex == batchStart )
                            batch._materialIndex = inst._materialIndex;
                        if ( inst._spinSeed != 0 )
                            ++_spinInstanceCount;
                        _listInstance.push_back( inst );
                    }
                    _listTransparentBatch.push_back( batch );
                    _listAllBatch.push_back( batch );
                    batchStart = entryIndex;
                }
            }
        }

        syncMaterialPins();
    }

    void GpuScene::retireUnusedMaterialElements()
    {
        // 이번 빌드에서 안 쓰인 원소는 바로 지우지 않는다 — 아직 GPU 가 읽는 중인 프레임이 있을 수 있다.
        // GpuMaterialRetireQueue 와 같은 지연 기준을 쓴다.
        if ( _buildCounter <= GpuMaterialRetireQueue::kRetireFrameDelay )
            return;
        const uint64 staleBefore = _buildCounter - GpuMaterialRetireQueue::kRetireFrameDelay;

        for ( GpuMaterialGroup& group : _listMaterialGroup )
        {
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index].first == nullptr )
                    continue;
                if ( group._listEntryLastSeenBuild[index] >= staleBefore )
                    continue;

                const GpuMaterialElementKey key{ group._listEntry[index].first, group._listEntry[index].second };
                group._mapEntryToIndex.erase( key );
                // 자리는 비워 두고 프리리스트로 돌린다. 뒤 원소를 당겨오면 그들의 인덱스가 바뀌어
                // 이미 인스턴스에 적힌 materialIndex 가 엉뚱한 머티리얼을 가리킨다.
                group._listEntry[index] = pair<Material*, MaterialInstance*>{ nullptr, nullptr };
                group._listFreeEntry.push_back( index );
                group._bHasLast = 0;
            }
        }
    }

    void GpuScene::resetMaterialRegistry()
    {
        _listMaterialGroup.clear();
        _mapShaderPathToGroup.clear();
    }

    uint32 GpuScene::materialGroupFor( const Material* pMaterial )
    {
        if ( pMaterial == nullptr )
            return kInvalidMaterialGroup;
        const string& shaderPath = pMaterial->getShaderPath();
        const auto    it         = _mapShaderPathToGroup.find( shaderPath );
        if ( it != _mapShaderPathToGroup.end() )
            return it->second;

        GpuMaterialGroup group{};
        group._shaderPath = shaderPath;
        _listMaterialGroup.push_back( std::move( group ) );
        const uint32 groupIndex = static_cast<uint32>( _listMaterialGroup.size() - 1 );
        _mapShaderPathToGroup.emplace( shaderPath, groupIndex );
        return groupIndex;
    }

    uint32 GpuScene::assignMaterialElement( Material* pMaterial, MaterialInstance* pInstance, uint32 groupIndex )
    {
        if ( pMaterial == nullptr || groupIndex >= _listMaterialGroup.size() )
            return 0;
        GpuMaterialGroup&           group = _listMaterialGroup[groupIndex];
        const GpuMaterialElementKey key{ pMaterial, pInstance };
        // 배치 안의 인스턴스는 같은 원소를 연속으로 묻는다 — 포인터 비교 한 번으로 끝낸다.
        if ( group._bHasLast != 0 && group._lastKey == key )
            return group._lastIndex;

        const auto it = group._mapEntryToIndex.find( key );
        uint32     elementIndex{ 0 };
        if ( it != group._mapEntryToIndex.end() )
        {
            elementIndex = it->second;
        }
        else if ( group._listFreeEntry.empty() == false )
        {
            // 회수된 자리를 재사용한다 — 새 자리를 늘리면 버퍼가 단조 증가한다.
            elementIndex = group._listFreeEntry.back();
            group._listFreeEntry.pop_back();
            group._listEntry[elementIndex] = pair<Material*, MaterialInstance*>{ pMaterial, pInstance };
            group._mapEntryToIndex.emplace( key, elementIndex );
        }
        else
        {
            group._listEntry.push_back( pair<Material*, MaterialInstance*>{ pMaterial, pInstance } );
            group._listEntryLastSeenBuild.push_back( 0 );
            elementIndex = static_cast<uint32>( group._listEntry.size() - 1 );
            group._mapEntryToIndex.emplace( key, elementIndex );
        }
        group._listEntryLastSeenBuild[elementIndex] = _buildCounter;
        group._lastKey                              = key;
        group._lastIndex                            = elementIndex;
        group._bHasLast                             = 1;
        return elementIndex;
    }

    void GpuScene::uploadMaterialGroups( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GpuScene.uploadMaterials" );
        if ( pDevice == nullptr )
            return;

        for ( const GpuMaterialGroup& group : _listMaterialGroup )
        {
            if ( group._listEntry.empty() )
                continue;

            // 원소 stride = 셰이더 리플렉션이 준 구조버퍼 원소 크기(Material::getElementStride, 백엔드마다 다르다).
            // 같은 셰이더의 머티리얼은 같은 stride 를 가진다 — 다르면 가장 큰 것을 쓴다.
            uint32 stride{ 0 };
            for ( const auto& entry : group._listEntry )
            {
                Material* pMaterial = entry.first;
                if ( pMaterial == nullptr )
                    continue;
                // 레이아웃 정본은 셰이더 — 이 백엔드의 리플렉션으로 오프셋·stride 를 맞춘 뒤 바이트를 읽는다(백엔드마다 한 번).
                pMaterial->ensureShaderLayout( pDevice );
                uint32 entryStride = pMaterial->getElementStride();
                if ( entryStride == 0 )
                    entryStride = static_cast<uint32>( pMaterial->getBuffer().size() );
                stride = MathUtil::max( stride, entryStride );
            }
            // stride 는 셰이더가 선언한 원소 크기 그대로다 — DX11 은 SRV 의 구조 stride 가 셰이더 선언(24 등)과 다르면 디버그 레이어가
            // 오류로 잡고, 네 백엔드가 DX 패킹(-fvk-use-dx-layout)으로 같은 stride 를 쓴다. 16 정렬로 키우면 DX11 이 "32 vs 24" 를 낸다.
            if ( stride == 0 )
                continue;

            const uint32 elementCount = static_cast<uint32>( group._listEntry.size() );
            const uint32 needBytes    = stride * elementCount;
            _listMaterialScratch.assign( needBytes, 0 );
            for ( uint32 element = 0; element < elementCount; ++element )
            {
                const Material*         pMaterial = group._listEntry[element].first;
                const MaterialInstance* pInstance = group._listEntry[element].second;
                if ( pMaterial == nullptr )
                    continue;
                const vector<uint8>& bytes = ( pInstance != nullptr && pInstance->getBuffer().empty() == false ) ? pInstance->getBuffer() : pMaterial->getBuffer();
                const uint32         copy  = MathUtil::min( stride, static_cast<uint32>( bytes.size() ) );
                if ( copy > 0 )
                    Memory::copy( _listMaterialScratch.data() + static_cast<size_t>( element ) * stride, bytes.data(), copy );
            }

            GpuMaterialGpu& gpu       = _mapMaterialGpu[group._shaderPath];
            const bool      bRecreate = ( gpu._buffer == 0 || gpu._capacityBytes < needBytes || gpu._stride != stride );
            if ( bRecreate )
            {
                if ( gpu._buffer != 0 )
                {
                    if ( gpu._srv != kInvalidDescriptorIndex )
                        pDevice->getResource()->unregisterBindlessResource( gpu._srv );
                    pDevice->getResource()->destroyBuffer( gpu._buffer );
                    gpu = GpuMaterialGpu{};
                }
                // 여유를 두어 머티리얼이 하나 늘 때마다 다시 만들지 않는다.
                const uint32  capacityElements = MathUtil::max( elementCount * 2u, 16u );
                RHIBufferDesc desc{};
                desc._elementSize  = stride;
                desc._elementCount = capacityElements;
                desc._sizeBytes    = stride * capacityElements;
                desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
                desc._pInitialData = nullptr;
                gpu._buffer        = pDevice->getResource()->createBuffer( desc );
                if ( gpu._buffer == 0 )
                    gpu._buffer = pDevice->getResource()->createStructuredBuffer( stride, capacityElements );
                if ( gpu._buffer == 0 )
                {
                    SW_LOG_ERROR( "머티리얼 데이터 버퍼 생성 실패 (%#, stride %#, %# 원소).", group._shaderPath.c_str(), stride, elementCount );
                    continue;
                }
                gpu._srv           = pDevice->getResource()->registerBindlessResource( gpu._buffer );
                gpu._capacityBytes = desc._sizeBytes;
                gpu._stride        = stride;
            }
            // 값이 지난 업로드와 같으면 올리지 않는다 — 머티리얼 수에 비례하던 프레임당 업로드가 바뀐 그룹만으로 준다.
            const bool bChanged = bRecreate || gpu._lastBytes.size() != needBytes ||
                                  Memory::compare( gpu._lastBytes.data(), _listMaterialScratch.data(), needBytes ) != 0;
            if ( bChanged )
            {
                pDevice->getResource()->updateStructuredBuffer( gpu._buffer, _listMaterialScratch.data(), needBytes );
                gpu._lastBytes.assign( _listMaterialScratch.begin(), _listMaterialScratch.begin() + needBytes );
            }
        }

        auto applyToBatches = [this]( vector<GpuMeshBatch>& listBatch )
        {
            for ( GpuMeshBatch& batch : listBatch )
            {
                batch._materialBuffer = 0;
                batch._materialSrv    = kInvalidDescriptorIndex;
                batch._materialCount  = 0;
                if ( batch._materialGroup == kInvalidMaterialGroup || batch._materialGroup >= _listMaterialGroup.size() )
                    continue;
                auto it = _mapMaterialGpu.find( _listMaterialGroup[batch._materialGroup]._shaderPath );
                if ( it == _mapMaterialGpu.end() )
                    continue;
                batch._materialBuffer = it->second._buffer;
                batch._materialSrv    = it->second._srv;
                batch._materialCount  = static_cast<uint32>( _listMaterialGroup[batch._materialGroup]._listEntry.size() );
            }
        };
        applyToBatches( _listAllBatch );
        applyToBatches( _listOpaqueBatch );
        applyToBatches( _listTransparentBatch );
    }
} // namespace sw
