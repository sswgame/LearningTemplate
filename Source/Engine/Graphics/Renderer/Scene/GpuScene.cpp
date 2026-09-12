#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuScene.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

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

            /**
             * @brief 원시 포인터로 받은 머티리얼의 소유를 빌립니다.
             * @details Material 은 create() 로만 태어나므로(패스키 생성자) 언제나 shared_ptr 이 소유하고 있다 —
             *          shared_from_this 가 실패하는 경우는 타입상 없다. 예전에는 값으로 만든 머티리얼을 위해
             *          "빌릴 수 없으면 그리지 않는다" 분기가 있었다.
             */
            static shared_ptr<Material> shareMaterial( Material* pMaterial )
            {
                return ( pMaterial != nullptr ) ? pMaterial->shared_from_this() : nullptr;
            }

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
    void GpuScene::invalidateBuildCache()
    {
        _listBuiltCandidate.clear();
        _lastCameraPos              = float3{};
        _lastPrimitiveSetGeneration = 0;
        _snapshot._bCpuDirty        = SW_TRUE;
    }

    void GpuScene::clear()
    {
        _snapshot._listInstance.clear();
        _snapshot._listOpaqueBatch.clear();
        _snapshot._listTransparentBatch.clear();
        _snapshot._listAllBatch.clear();
        // 머티리얼 등록부는 "그룹 목록(스냅샷)" 과 "셰이더 경로→인덱스 맵" 이 한 몸이다. 목록만 지우면 맵이 옛 인덱스를
        // 돌려주고 materialGroupFor 가 범위 밖이라 조용히 건너뛴다 — 배치에 머티리얼 버퍼가 안 실려 폴백(0)으로
        // 그려지고, 투명 머티리얼은 알파 0 이라 화면에서 사라진다(백엔드 교체 뒤 유리 큐브가 없어지던 원인).
        resetMaterialRegistry();
        _indirectCommandCount = 0;
        _listScratchCandidate.clear();
        _listScratchRaw.clear();
        _listScratchOpaqueEntry.clear();
        _listScratchTransparentIdx.clear();
        invalidateBuildCache();
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

    uint64 GpuScene::permutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance )
    {
        if ( pMaterial == nullptr )
            return 0;
        // 인스턴스가 있으면 그 해시를 쓴다 — 키워드 오버라이드가 퍼뮤테이션을 바꾸고, 그 해시는 부모 것을 이미 포함한다.
        const uint64 defineHash = ( pInstance != nullptr ) ? pInstance->getPermutationHash() : pMaterial->getPermutationHash();
        uint64       hash       = pMaterial->getShaderPathHash();
        hash ^= defineHash + 0x9e3779b97f4a7c15ull + ( hash << 6 ) + ( hash >> 2 );
        return hash;
    }

    uint32 GpuScene::shaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance )
    {
        if ( pMaterial == nullptr )
            return kInvalidShaderPermutation;
        const uint64 hash = permutationHashFor( pMaterial, pInstance );
        const auto   it   = _mapPermutationToIndex.find( hash );
        if ( it != _mapPermutationToIndex.end() )
            return it->second;

        GpuShaderPermutation permutation{};
        permutation._shaderPath = pMaterial->getShaderPath();
        permutation._listDefine = ( pInstance != nullptr ) ? pInstance->getCachedShaderDefines() : pMaterial->getCachedShaderDefines();
        permutation._hash       = hash;
        _snapshot._listShaderPermutation.push_back( std::move( permutation ) );

        const uint32 index = static_cast<uint32>( _snapshot._listShaderPermutation.size() - 1 );
        _mapPermutationToIndex.emplace( hash, index );
        return index;
    }

    Material* GpuScene::batchKeyMaterial( Material* pMaterial, const MaterialInstance* pInstance )
    {
        if ( _bMergeAcrossMaterials == SW_FALSE || pMaterial == nullptr )
            return pMaterial;
        // 대표는 **퍼뮤테이션 단위**다. 예전엔 셰이더 경로만 봐서, 같은 .hlsl 을 쓰지만 정적 스위치가 다른
        // 머티리얼이 한 배치로 접혔다 — 배치는 PSO 하나로 그리므로 한쪽 퍼뮤테이션이 통째로 버려졌다.
        const uint64 hash = permutationHashFor( pMaterial, pInstance );
        auto         it   = _mapShaderRepresentative.find( hash );
        if ( it != _mapShaderRepresentative.end() )
            return it->second;
        _mapShaderRepresentative.emplace( hash, pMaterial );
        return pMaterial;
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
            SortKey key{ cand._mesh.get(), batchKeyMaterial( cand._material.get(), cand._instance.get() ), _bMergeAcrossMaterials != SW_FALSE ? nullptr : cand._instance.get() };
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

    void GpuScene::buildFromScene( Scene* pScene, const float3& cameraPos )
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
                cand._spinSeed     = pMeshComp->getGpuSpinSeed();
                cand._mesh         = pMeshComp->getMesh(); // 소유를 싣는다 — RT 가 upload() 에서 역참조한다
                // 스냅샷은 소유를 함께 싣는다 — 렌더 스레드가 패킷을 다 쓸 때까지 머티리얼·인스턴스가 살아야 한다.
                cand._instance = pMeshComp->getMaterialInstance();
                cand._material = GpuSceneInternal::shareMaterial( pMeshComp->getMaterial() );
                if ( cand._material == nullptr )
                    cand._material = GpuSceneInternal::shareMaterial( pScene->getMaterial() ); // 머티리얼 없는 메시는 씬 기본 머티리얼로 (언리얼의 기본 머티리얼)

                // **블렌드 모드는 머티리얼의 성질이다** — 언리얼도 블렌드 모드가 머티리얼 에셋에 있고,
                // 그 값이 셰이더 퍼뮤테이션(불투명/반투명)을 가른다. 메시가 뒤집을 수 있게 두면 불투명으로
                // 컴파일된 머티리얼을 블렌딩으로 그리는 어긋난 상태가 만들어진다.
                //
                // 인스턴스만 붙은 메시는 **인스턴스의 부모 머티리얼**이 정본이다(인스턴스는 값만 덮어쓰고
                // 블렌드 모드는 갖지 않는다). 둘 다 없을 때만 컴포넌트 값을 쓴다 — 머티리얼이 없는
                // 디버그·픽스처 메시가 그 경우다.
                // (예전에는 인스턴스를 이 판단 **뒤에** 채워서 이 폴백이 한 번도 걸리지 않았다.)
                const Material* pBlendSource = cand._material.get();
                if ( pBlendSource == nullptr && cand._instance != nullptr )
                    pBlendSource = cand._instance->getParent();
                cand._blendMode = ( pBlendSource != nullptr ) ? static_cast<uint32>( pBlendSource->getBlendMode() )
                                                              : static_cast<uint32>( pMeshComp->getBlendMode() );
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
            bContentSame = bHasCache && _listBuiltCandidate == _listScratchCandidate && _snapshot._listInstance.empty() == false;
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
            // 이 스레드에서 그대로 채운다. 워커로 나누면 **모든 크기에서 느려진다** — 원소당 일이
            // 필드 몇 개 복사뿐이라 디스패치와 대기가 일보다 비싸다(GpuScene.h buildFromScene 주석의 숫자).
            GpuSceneInternal::fillRangePtr( _listScratchCandidate.data(), _listScratchRaw.data(), 0, count );

            if ( bBatchKeysSame == false )
            {
                SW_PROFILE_SCOPE( "GT.GpuScene.build.partition" );
                rebuildPartitionTables();
            }
        }

        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.batches" );
            sortTransparent( cameraPos );

            // 배치 구성이 그대로면 **다시 나누지 않는다** — 인스턴스 값만 제자리에서 갱신한다.
            // 예전엔 물체가 하나만 움직여도 인스턴스·배치 목록을 통째로 비우고 다시 만들었다(측정에서
            // 이 블록이 GpuScene 빌드의 절반이 넘었다). 언리얼 GPUScene 도 프리미티브가 움직였다고
            // 자료구조를 다시 만들지는 않는다.
            const bool bRefreshed = bBatchKeysSame && refreshInstancesInPlace();
            if ( bRefreshed == false )
            {
                _snapshot._listInstance.clear();
                _snapshot._listOpaqueBatch.clear();
                _snapshot._listTransparentBatch.clear();
                _snapshot._listAllBatch.clear();
                buildBatches();
                _listBuiltTransparentIdx = _listScratchTransparentIdx;
            }
            retireUnusedMaterialElements();
        }

        // scratch 를 기준 집합으로 넘기고 낡은 기준을 scratch 로 돌려받는다 — 복사 없이 두 버퍼를
        // 번갈아 쓰므로 프레임당 힙 할당이 생기지 않는다.
        _listBuiltCandidate.swap( _listScratchCandidate );
        _lastCameraPos              = cameraPos;
        _lastPrimitiveSetGeneration = setGeneration;
        _snapshot._bCpuDirty        = SW_TRUE;
    }

    void GpuScene::requestGpuUploads( GpuUploadQueue& queue ) const
    {
        // 배치가 메시의 소유를 들고 있으므로(스냅샷 소유 규칙) 큐에 넘겨도 워커가 도는 동안 사라지지 않는다.
        for ( const GpuMeshBatch& batch : _snapshot._listAllBatch )
            queue.requestMesh( batch._mesh );
    }

    bool GpuScene::upload( IRHIDevice* pDevice )
    {
        SW_PROFILE_SCOPE( "RT.GpuScene.upload" );

        if ( pDevice == nullptr || _snapshot._listInstance.empty() || _snapshot._listAllBatch.empty() )
            return false;

        // RT-owned context: pack MaterialInstance overrides and upload meshes in a single pass.
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.applyInstanceCbs" );
            GpuSceneInternal::applyInstanceCbsVal( pDevice, _snapshot._listAllBatch );
        }
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.uploadMeshes" );
            GpuSceneInternal::uploadMeshesVal( pDevice, _snapshot._listAllBatch );
        }
        // 머티리얼 데이터 구조버퍼(셰이더 타입별) — 값이 프레임마다 바뀔 수 있어 스냅샷 dirty 와 무관하게 매번 올린다.
        uploadMaterialGroups( pDevice );

        const size_t opaqueCount = _snapshot._listOpaqueBatch.size();
        for ( size_t batchIndex = 0; batchIndex < opaqueCount; ++batchIndex )
        {
            _snapshot._listOpaqueBatch[batchIndex]._materialCb   = _snapshot._listAllBatch[batchIndex]._materialCb;
            _snapshot._listOpaqueBatch[batchIndex]._vertexBuffer = _snapshot._listAllBatch[batchIndex]._vertexBuffer;
        }
        for ( size_t batchIndex = 0; batchIndex < _snapshot._listTransparentBatch.size(); ++batchIndex )
        {
            _snapshot._listTransparentBatch[batchIndex]._materialCb   = _snapshot._listAllBatch[opaqueCount + batchIndex]._materialCb;
            _snapshot._listTransparentBatch[batchIndex]._vertexBuffer = _snapshot._listAllBatch[opaqueCount + batchIndex]._vertexBuffer;
        }

        // CPU 스냅샷이 그대로면 인스턴스 버퍼 재업로드를 생략한다. **간접 인자는 예외다** —
        // 컬링 컴퓨트가 개수를 InterlockedAdd 로 만드는 동안에는 매 프레임 0 으로 되돌려 놓아야 한다.
        // 여기서 같이 건너뛰었더니 정적 씬에서 개수가 N, 2N, 3N ... 으로 끝없이 자랐다(드로우 비용이
        // 계속 늘고, 이번 프레임에 쓰지 않은 가시 목록 자리를 읽는다). 움직이는 벤치와 한 프레임만
        // 그리는 테스트가 둘 다 이걸 가리고 있었다.
        if ( _snapshot._bCpuDirty == SW_FALSE && _instances._buffer != 0 && getIndirectArgsBuffer() != 0 )
        {
            if ( _bGpuFillsIndirectCounts != SW_FALSE )
                refreshIndirectCounts( pDevice );
            return true;
        }

        const uint32 instanceCount = static_cast<uint32>( _snapshot._listInstance.size() );
        const uint32 argsCount     = static_cast<uint32>( _snapshot._listAllBatch.size() );

        // UnorderedAccess 를 함께 요구한다 — instanceanim 컴퓨트가 월드 행렬을 고쳐 쓴다. 못 만드는
        // 백엔드/드라이버면 슬롯이 SRV 전용으로 한 번 더 시도해 그리기는 그대로 살린다.
        constexpr RHIBufferUsage kInstanceUsage =
            RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        {
            SW_PROFILE_SCOPE( "RT.GpuScene.instanceBuffer" );
            if ( _instances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( GpuInstance ) ), instanceCount, kInstanceUsage, true, true,
                                            _snapshot._listInstance.data() ) )
                _instances.upload( pDevice, _snapshot._listInstance.data(), instanceCount * static_cast<uint32>( sizeof( GpuInstance ) ) );
        }

        // 가시 인스턴스 ID 버퍼 — 컬링 컴퓨트가 살아남은 인스턴스의 **원본 인덱스**를 배치 구간에 압축해
        // 넣고(언리얼 FInstanceCullingContext 의 InstanceIdBuffer), 정점 셰이더가 그 순서로 읽는다.
        // **뷰마다 하나씩**이다 — 목록은 절두체에 종속이라 메인 카메라로 거른 것을 그림자가 쓰면 안 된다.
        constexpr RHIBufferUsage kVisibleUsage =
            RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        for ( GpuCullViewResources& view : _arrCullView )
        {
            view._visibleInstances.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), instanceCount, kVisibleUsage, true,
                                                   true, nullptr );
        }

        // 배치 구간 — 컬링 컴퓨트가 "이 배치의 인스턴스는 어디서 시작하나"를 읽는다.
        _listScratchBatchInfo.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            const GpuMeshBatch& infoBatch                  = _snapshot._listAllBatch[argIndex];
            _listScratchBatchInfo[argIndex]._instanceBase  = infoBatch._instanceBase;
            _listScratchBatchInfo[argIndex]._instanceCount = infoBatch._instanceCount;
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
        // 0 으로 올리면 안 된다 — 컬링이 못 도는데 개수가 0 이면 그 프레임은 아무것도 안 그려진다.
        // 그래서 "원한다"(_bWantGpuIndirectCounts)와 "실제로 된다"(_bGpuFillsIndirectCounts)를 나눠 둔다.
        _bGpuFillsIndirectCounts =
            ( _bWantGpuIndirectCounts != SW_FALSE && _batchInfo._buffer != 0 && argsCount > 0 && hasAllVisibleBuffers() ) ? 1u : 0u;

        _listScratchIndirectCmd.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            _listScratchIndirectCmd[argIndex]._vertexCount = _snapshot._listAllBatch[argIndex]._vertexCount;
            // 컬링 컴퓨트가 개수를 만드는 배치는 **0 에서 시작**해야 한다 — InterlockedAdd 로 보이는 것만 센다.
            // 압축을 포기한 배치(Preserve)와 컬링이 아예 없을 때는 CPU 가 센 개수를 그대로 쓴다.
            const bool bPreserve                                   = static_cast<GpuBatchSortMode>( _listScratchBatchInfo[argIndex]._sortMode ) == GpuBatchSortMode::Preserve;
            const bool bGpuCounts                                  = ( _bGpuFillsIndirectCounts != SW_FALSE ) && ( bPreserve == false );
            _listScratchIndirectCmd[argIndex]._instanceCount       = bGpuCounts ? 0u : _snapshot._listAllBatch[argIndex]._instanceCount;
            _listScratchIndirectCmd[argIndex]._startVertexLocation = 0;
            // **0 이어야 한다.** 배치의 인스턴스 시작 오프셋은 셰이더가 루트 상수(g_InstanceBase)로 더한다.
            // 여기에도 넣으면 Vulkan 에서만 두 번 더해진다 — DX 의 SV_InstanceID 는 StartInstanceLocation 을
            // 포함하지 않지만 SPIR-V 의 InstanceIndex 는 firstInstance 를 **포함**하기 때문이다(GL 은 빌드 때
            // InstanceId 로 바꿔 구우므로 DX 와 같다). 그래서 배치가 둘 이상일 때 Vulkan 만 엉뚱한 인스턴스를
            // 읽어 큐브가 겹쳐 그려졌다 — 벤치가 메시를 하나만 쓰던 동안(instanceBase 가 늘 0) 드러나지 않았다.
            _listScratchIndirectCmd[argIndex]._startInstanceLocation = 0;
        }

        // 간접 인자도 **뷰마다** 하나다 — 뷰별로 개수가 다르게 나오기 때문이다.
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

        // 인자 버퍼를 하나라도 못 만들었으면 컴퓨트가 개수를 만들 수 없다 — 개수 0 짜리 인자로 그리면
        // 그 뷰는 빈 화면이 된다. 플래그를 내리고 CPU 개수로 되돌려 올린다.
        if ( _bGpuFillsIndirectCounts != SW_FALSE && hasAllCullViewBuffers() == false )
        {
            _bGpuFillsIndirectCounts = SW_FALSE;
            refreshIndirectCounts( pDevice );
        }

        _indirectCommandCount = argsCount;
        _snapshot._bCpuDirty  = SW_FALSE;

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
        // 슬롯이 뷰 등록 해제와 버퍼 파괴를 순서까지 맞춰 처리한다 — 예전엔 여기서 손으로 스무 줄을
        // 늘어놓았고, 뷰 하나를 빠뜨려도 컴파일은 통과했다.
        _instances.release( pDevice );
        _batchInfo.release( pDevice );
        for ( GpuCullViewResources& view : _arrCullView )
        {
            view._visibleInstances.release( pDevice );
            view._indirectArgs.release( pDevice );
        }
        _indirectCommandCount        = 0;
        _snapshot._spinInstanceCount = 0;
        _snapshot._bCpuDirty         = SW_TRUE;
    }

    void GpuScene::exportCpuSnapshot( GpuSceneSnapshot& outSnapshot )
    {
        // 옮겨지는 것은 GpuSceneSnapshot 이 든 것 **전부이고 그것뿐**이다. 복사다 — 퍼뮤테이션 표는 GT 가
        // 계속 늘려 가는 정본이라 빼앗아 가면 다음 프레임의 인덱스가 0 부터 다시 매겨진다.
        outSnapshot          = _snapshot;
        _snapshot._bCpuDirty = SW_FALSE;
    }

    void GpuScene::adoptCpuSnapshot( GpuSceneSnapshot&& snapshot )
    {
        _snapshot = std::move( snapshot );
    }

    void GpuScene::sortTransparent( const float3& cameraPos )
    {
        if ( _listScratchTransparentIdx.size() <= 1 )
            return;
        std::sort( _listScratchTransparentIdx.begin(), _listScratchTransparentIdx.end(), [&]( uint32 idxA, uint32 idxB )
        { return float3::getDistanceSquared( _listScratchRaw[idxA]._boundsCenter, cameraPos ) > float3::getDistanceSquared( _listScratchRaw[idxB]._boundsCenter, cameraPos ); } );
    }

    bool GpuScene::refreshInstancesInPlace()
    {
        // 매핑이 인스턴스 수와 맞아야 한다. 한 번이라도 전체 빌드를 안 했으면 못 쓴다.
        if ( _snapshot._listInstance.empty() || _listInstanceSrcIndex.size() != _snapshot._listInstance.size() )
            return false;
        // 투명은 카메라 거리로 매 프레임 다시 정렬한다. 그 순서가 바뀌면 어느 인스턴스가 어느 자리에
        // 앉는지가 달라지므로 매핑을 그대로 쓸 수 없다.
        if ( _listScratchTransparentIdx != _listBuiltTransparentIdx )
            return false;

        const uint32 rawCount        = static_cast<uint32>( _listScratchRaw.size() );
        _snapshot._spinInstanceCount = 0;
        for ( size_t slot = 0; slot < _snapshot._listInstance.size(); ++slot )
        {
            const uint32 srcIndex = _listInstanceSrcIndex[slot];
            if ( srcIndex >= rawCount )
                return false;

            // 배치 구성이 같으므로 _meshBatchIndex 와 _materialIndex 는 그대로다 — 바뀐 것은
            // 트랜스폼과 바운드, 그리고 회전 시드뿐이다.
            GpuInstance&       inst = _snapshot._listInstance[slot];
            const GpuInstance& raw  = _listScratchRaw[srcIndex];
            inst._world             = raw._world;
            inst._boundsCenter      = raw._boundsCenter;
            inst._boundsRadius      = raw._boundsRadius;
            inst._blendMode         = raw._blendMode;
            inst._spinSeed          = raw._spinSeed;
            if ( inst._spinSeed != 0 )
                ++_snapshot._spinInstanceCount;
        }

        // 배치 구성은 그대로지만 **회수 시계는 돌아야 한다**. 안 그러면 물체가 움직이기만 하는 씬에서
        // 시계가 멈춰, 안 쓰이게 된 머티리얼 원소가 영원히 회수되지 않는다(자리가 조금씩 샌다).
        // 지금 인스턴스가 가리키는 원소는 전부 살아 있으므로 이번 빌드 번호로 도장을 찍어 둔다.
        ++_buildCounter;
        for ( const GpuInstance& inst : _snapshot._listInstance )
        {
            if ( inst._meshBatchIndex >= _snapshot._listAllBatch.size() )
                continue;
            const uint32 groupIndex = _snapshot._listAllBatch[inst._meshBatchIndex]._materialGroup;
            if ( groupIndex >= _snapshot._listMaterialGroup.size() )
                continue;
            GpuMaterialGroup& group = _snapshot._listMaterialGroup[groupIndex];
            if ( inst._materialIndex < group._listEntryLastSeenBuild.size() )
                group._listEntryLastSeenBuild[inst._materialIndex] = _buildCounter;
        }
        return true;
    }

    void GpuScene::buildBatches()
    {
        _snapshot._listInstance.reserve( _listScratchCandidate.size() );
        _listInstanceSrcIndex.clear();
        _listInstanceSrcIndex.reserve( _listScratchCandidate.size() );
        _snapshot._spinInstanceCount = 0;

        // **머티리얼 원소 인덱스는 프레임을 넘어 유지된다** (언리얼 GPUScene 의 영속 PrimitiveID 와 같은 자리).
        // 예전엔 여기서 그룹을 통째로 지우고 인스턴스마다 다시 부여했다 — 인스턴스 N 개와 머티리얼 M 종에
        // O(N·M) 이었고, 무엇보다 같은 머티리얼의 인덱스가 프레임마다 달라져 "바뀐 것만 올린다" 를 할 수 없었다.
        // 이제 처음 본 (머티리얼, 인스턴스) 쌍에만 자리를 주고, 안 쓰이면 아래 retireUnusedMaterialElements 가
        // 지연 회수한다. 자리를 옮기지 않으므로 인덱스는 안정적이다.
        ++_buildCounter;
        for ( GpuMaterialGroup& group : _snapshot._listMaterialGroup )
            group._bHasLast = SW_FALSE;

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchOpaqueEntry.size(); ++entryIndex )
            {
                if ( entryIndex == _listScratchOpaqueEntry.size() || ( _listScratchOpaqueEntry[entryIndex]._key == _listScratchOpaqueEntry[batchStart]._key ) == false )
                {
                    const SortKey& key = _listScratchOpaqueEntry[batchStart]._key;
                    GpuMeshBatch   batch{};
                    batch._mesh          = _listScratchCandidate[_listScratchOpaqueEntry[batchStart]._srcIdx]._mesh; // 키는 정체성, 소유는 배치 머리 후보의 것
                    batch._vertexCount   = batch._mesh->getVertexCount();
                    batch._instanceBase  = static_cast<uint32>( _snapshot._listInstance.size() );
                    batch._instanceCount = entryIndex - batchStart;
                    batch._blendMode     = RHIBlendMode::Opaque;
                    // 키의 포인터는 정체성이고 소유는 배치 머리 후보의 것을 빌린다. 합치기가 켜지면 키의 인스턴스는
                    // nullptr 이라 배치에는 인스턴스를 싣지 않는다 — 원소 표(_listEntry)가 인스턴스마다 소유를 든다.
                    {
                        const DrawCandidate& headCand = _listScratchCandidate[_listScratchOpaqueEntry[batchStart]._srcIdx];
                        batch._material               = GpuSceneInternal::shareMaterial( key._pMaterial );
                        batch._materialInstance       = ( key._pInstance != nullptr ) ? headCand._instance : nullptr;
                    }
                    if ( key._pInstance != nullptr )
                        batch._materialCb = key._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = key._pMaterial ? key._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;
                    // 텍스처 슬롯은 인스턴스가 아니라 부모 머티리얼이 소유한다(인스턴스는 CB 값만 덮어쓴다).
                    GpuSceneInternal::fillMaterialTextureSrvs( batch, key._pMaterial );
                    batch._materialGroup     = materialGroupFor( key._pMaterial );
                    batch._shaderPermutation = shaderPermutationFor( key._pMaterial, key._pInstance );
                    batch._materialIndex     = 0;

                    // 인스턴스마다 자기 (머티리얼, 인스턴스) 원소를 받는다 — 합치기가 켜져 있으면 한 배치에 여러 머티리얼이 산다.
                    const uint32 batchIndex = static_cast<uint32>( _snapshot._listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        const uint32         srcIdx = _listScratchOpaqueEntry[batchEntryIndex]._srcIdx;
                        const DrawCandidate& cand   = _listScratchCandidate[srcIdx];
                        GpuInstance          inst   = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex        = batchIndex;
                        inst._materialIndex         = assignMaterialElement( cand._material, cand._instance, batch._materialGroup );
                        if ( batchEntryIndex == batchStart )
                            batch._materialIndex = inst._materialIndex;
                        if ( inst._spinSeed != 0 )
                            ++_snapshot._spinInstanceCount;
                        _listInstanceSrcIndex.push_back( srcIdx );
                        _snapshot._listInstance.push_back( inst );
                    }
                    _snapshot._listOpaqueBatch.push_back( batch );
                    _snapshot._listAllBatch.push_back( batch );

                    batchStart = entryIndex;
                }
            }
        }

        // back-to-front 정렬된 transparent 인덱스에서 연속 동일 mesh/mat/instance 만 머지.
        if ( _listScratchTransparentIdx.empty() == false )
        {
            uint32 batchStart{ 0 };

            // 배치 헤드의 키는 배치가 닫힐 때만 바뀐다. 예전엔 반복마다 헤드와 현재 것을 **둘 다**
            // batchKeyMaterial 로 다시 구했다 — 배치 하나가 N 개면 헤드 키를 N 번 다시 만든 셈이다.
            const DrawCandidate* pBatchHead    = &_listScratchCandidate[_listScratchTransparentIdx[0]];
            Material*            pBatchHeadKey = batchKeyMaterial( pBatchHead->_material.get(), pBatchHead->_instance.get() );

            for ( uint32 entryIndex = 1; entryIndex <= _listScratchTransparentIdx.size(); ++entryIndex )
            {
                const bool bEnd = entryIndex == _listScratchTransparentIdx.size();
                bool       bKeyChange{ false };
                if ( bEnd == false )
                {
                    const DrawCandidate& current = _listScratchCandidate[_listScratchTransparentIdx[entryIndex]];
                    bKeyChange                   = ( pBatchHead->_mesh != current._mesh ) ||
                                 ( pBatchHeadKey != batchKeyMaterial( current._material.get(), current._instance.get() ) ) ||
                                 ( _bMergeAcrossMaterials == SW_FALSE && pBatchHead->_instance != current._instance );
                }
                if ( bEnd || bKeyChange )
                {
                    const DrawCandidate& cand = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    GpuMeshBatch         batch{};
                    batch._mesh             = cand._mesh;
                    batch._vertexCount      = batch._mesh->getVertexCount();
                    batch._instanceBase     = static_cast<uint32>( _snapshot._listInstance.size() );
                    batch._instanceCount    = entryIndex - batchStart;
                    batch._blendMode        = RHIBlendMode::Transparent;
                    batch._materialInstance = cand._instance;
                    batch._material         = cand._material;
                    if ( cand._instance.get() != nullptr )
                        batch._materialCb = cand._instance.get()->getDescriptorIndex();
                    else
                        batch._materialCb = cand._material.get() ? cand._material.get()->getDescriptorIndex() : kInvalidDescriptorIndex;
                    GpuSceneInternal::fillMaterialTextureSrvs( batch, cand._material.get() );
                    batch._materialGroup     = materialGroupFor( cand._material.get() );
                    batch._shaderPermutation = shaderPermutationFor( cand._material.get(), cand._instance.get() );
                    batch._materialIndex     = 0;

                    const uint32 batchIndex = static_cast<uint32>( _snapshot._listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        const uint32         srcIdx    = _listScratchTransparentIdx[batchEntryIndex];
                        const DrawCandidate& entryCand = _listScratchCandidate[srcIdx];
                        GpuInstance          inst      = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex           = batchIndex;
                        inst._materialIndex            = assignMaterialElement( entryCand._material, entryCand._instance, batch._materialGroup );
                        if ( batchEntryIndex == batchStart )
                            batch._materialIndex = inst._materialIndex;
                        if ( inst._spinSeed != 0 )
                            ++_snapshot._spinInstanceCount;
                        _listInstanceSrcIndex.push_back( srcIdx );
                        _snapshot._listInstance.push_back( inst );
                    }
                    _snapshot._listTransparentBatch.push_back( batch );
                    _snapshot._listAllBatch.push_back( batch );
                    batchStart = entryIndex;
                    if ( bEnd == false )
                    {
                        pBatchHead    = &_listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                        pBatchHeadKey = batchKeyMaterial( pBatchHead->_material.get(), pBatchHead->_instance.get() );
                    }
                }
            }
        }
    }

    void GpuScene::retireUnusedMaterialElements()
    {
        // 이번 빌드에서 안 쓰인 원소는 바로 지우지 않는다 — 아직 GPU 가 읽는 중인 프레임이 있을 수 있다.
        // 패킷 링 깊이(constant::kRenderFrameQueueDepth)와 같은 지연 기준을 쓴다 — 큐잉된 패킷이 아직 원소를 읽을 수 있다.
        if ( _buildCounter <= constant::kRenderFrameQueueDepth )
            return;
        const uint64 staleBefore = _buildCounter - constant::kRenderFrameQueueDepth;

        for ( GpuMaterialGroup& group : _snapshot._listMaterialGroup )
        {
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index]._material == nullptr )
                    continue;
                if ( group._listEntryLastSeenBuild[index] >= staleBefore )
                    continue;

                const GpuMaterialElementKey key{ group._listEntry[index]._material.get(), group._listEntry[index]._instance.get() };
                group._mapEntryToIndex.erase( key );
                // 자리는 비워 두고 프리리스트로 돌린다. 뒤 원소를 당겨오면 그들의 인덱스가 바뀌어
                // 이미 인스턴스에 적힌 materialIndex 가 엉뚱한 머티리얼을 가리킨다.
                group._listEntry[index] = GpuMaterialElement{};
                group._listFreeEntry.push_back( index );
                group._bHasLast = SW_FALSE;
            }
        }
    }

    void GpuScene::resetMaterialRegistry()
    {
        _snapshot._listMaterialGroup.clear();
        _mapShaderPathToGroup.clear();
        _snapshot._listShaderPermutation.clear();
        _mapPermutationToIndex.clear();
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
        _snapshot._listMaterialGroup.push_back( std::move( group ) );
        const uint32 groupIndex = static_cast<uint32>( _snapshot._listMaterialGroup.size() - 1 );
        _mapShaderPathToGroup.emplace( shaderPath, groupIndex );
        return groupIndex;
    }

    uint32 GpuScene::assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex )
    {
        Material* const         pMaterial = material.get();
        MaterialInstance* const pInstance = instance.get();
        if ( pMaterial == nullptr || groupIndex >= _snapshot._listMaterialGroup.size() )
            return 0;
        GpuMaterialGroup&           group = _snapshot._listMaterialGroup[groupIndex];
        const GpuMaterialElementKey key{ pMaterial, pInstance };
        // 배치 안의 인스턴스는 같은 원소를 연속으로 묻는다 — 포인터 비교 한 번으로 끝낸다.
        if ( group._bHasLast != SW_FALSE && group._lastKey == key )
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
            group._listEntry[elementIndex] = GpuMaterialElement{ material, instance };
            group._mapEntryToIndex.emplace( key, elementIndex );
        }
        else
        {
            group._listEntry.push_back( GpuMaterialElement{ material, instance } );
            group._listEntryLastSeenBuild.push_back( 0 );
            elementIndex = static_cast<uint32>( group._listEntry.size() - 1 );
            group._mapEntryToIndex.emplace( key, elementIndex );
        }
        group._listEntryLastSeenBuild[elementIndex] = _buildCounter;
        group._lastKey                              = key;
        group._lastIndex                            = elementIndex;
        group._bHasLast                             = SW_TRUE;
        return elementIndex;
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

            // 원소 stride = 셰이더 리플렉션이 준 구조버퍼 원소 크기(Material::getElementStride, 백엔드마다 다르다).
            // 같은 셰이더의 머티리얼은 같은 stride 를 가진다 — 다르면 가장 큰 것을 쓴다.
            uint32 stride{ 0 };
            for ( const auto& entry : group._listEntry )
            {
                Material* pMaterial = entry._material.get();
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
            // 다시 만든다 — 구조버퍼의 stride 는 뷰에 박혀 있어 셰이더 선언과 달라지면 안 된다.
            const uint32 capacityElements = MathUtil::max( elementCount * 2u, 16u );
            const bool   bRecreate        = ( gpu._slot._buffer == 0 ) || ( gpu._slot._elementSize != stride ) ||
                                   ( gpu._slot._capacityElements < capacityElements );
            if ( gpu._slot.ensureCapacity( pDevice, stride, capacityElements,
                                           RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
            {
                SW_LOG_ERROR( "머티리얼 데이터 버퍼 생성 실패 (%#, stride %#, %# 원소).", group._shaderPath.c_str(), stride, elementCount );
                continue;
            }
            // 값이 지난 업로드와 같으면 올리지 않는다 — 머티리얼 수에 비례하던 프레임당 업로드가 바뀐 그룹만으로 준다.
            const bool bChanged = bRecreate || gpu._lastBytes.size() != needBytes ||
                                  Memory::compare( gpu._lastBytes.data(), _listMaterialScratch.data(), needBytes ) != 0;
            if ( bChanged )
            {
                gpu._slot.upload( pDevice, _listMaterialScratch.data(), needBytes );
                gpu._lastBytes.assign( _listMaterialScratch.begin(), _listMaterialScratch.begin() + needBytes );
            }
        }

        // **그룹당 한 번 풀어 두고, 배치는 인덱스로 집는다.**
        // 예전에는 배치마다 셰이더 **경로 문자열**로 해시 조회를 했다 — 그룹은 한둘인데 배치는 수백이라
        // 같은 답을 배치 수만큼 다시 구한 셈이다(Release 실측 프레임당 86us, RT 렌더 시간의 12%).
        // 그룹 수만큼만 조회해 표로 만들어 두면 배치 루프는 저장 몇 번으로 끝난다.
        struct ResolvedGroup
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
            uint32             _elementCount{ 0 };
        };
        const uint32          groupCount = static_cast<uint32>( _snapshot._listMaterialGroup.size() );
        vector<ResolvedGroup> listResolved( groupCount );
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
