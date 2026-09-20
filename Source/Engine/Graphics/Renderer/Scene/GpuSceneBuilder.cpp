#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct GpuSceneBuilderInternal
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GpuSceneBuilder::invalidateBuildCache()
    {
        _listBuiltCandidate.clear();
        _lastCameraPos              = float3{};
        _lastPrimitiveSetGeneration = 0;
        _lastPermutationGeneration  = 0;
        _snapshot._bCpuDirty        = SW_TRUE;
    }

    void GpuSceneBuilder::clear()
    {
        _listInstanceWork.clear();
        _snapshot._pListInstance.reset();
        _snapshot._listOpaqueBatch.clear();
        _snapshot._listTransparentBatch.clear();
        _snapshot._listAllBatch.clear();
        // 머티리얼 등록부는 "그룹 목록(스냅샷)" 과 "셰이더 경로→인덱스 맵" 이 한 몸이다. 목록만 지우면 맵이 옛 인덱스를
        // 돌려주고 materialGroupFor 가 범위 밖이라 조용히 건너뛴다 — 배치에 머티리얼 버퍼가 안 실려 폴백(0)으로
        // 그려지고, 투명 머티리얼은 알파 0 이라 화면에서 사라진다(백엔드 교체 뒤 유리 큐브가 없어지던 원인).
        resetMaterialRegistry();
        _listScratchCandidate.clear();
        _listScratchRaw.clear();
        _listScratchOpaqueEntry.clear();
        _listScratchTransparentIdx.clear();
        invalidateBuildCache();
    }

    void GpuSceneBuilder::setMergeBatchesAcrossMaterials( bool bMerge )
    {
        const uint8 value = bMerge ? 1 : 0;
        if ( _bMergeAcrossMaterials == value )
            return;
        _bMergeAcrossMaterials = value;
        // 합치기 여부가 배치 키를 바꾼다 = 원소 구성이 달라진다. 영속 인덱스를 그대로 두면 예전 기준의 자리가 남는다.
        resetMaterialRegistry();
        invalidateBuildCache();
    }

    uint64 GpuSceneBuilder::permutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance )
    {
        if ( pMaterial == nullptr )
            return 0;
        // 인스턴스가 있으면 그 해시를 쓴다 — 키워드 오버라이드가 퍼뮤테이션을 바꾸고, 그 해시는 부모 것을 이미 포함한다.
        const uint64 defineHash = ( pInstance != nullptr ) ? pInstance->getPermutationHash() : pMaterial->getPermutationHash();
        uint64       hash       = pMaterial->getShaderPathHash();
        hash ^= defineHash + 0x9e3779b97f4a7c15ull + ( hash << 6 ) + ( hash >> 2 );
        return hash;
    }

    uint32 GpuSceneBuilder::shaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance )
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

    Material* GpuSceneBuilder::batchKeyMaterial( Material* pMaterial, uint64 permutationHash )
    {
        if ( _bMergeAcrossMaterials == SW_FALSE || pMaterial == nullptr )
            return pMaterial;
        // 대표는 **퍼뮤테이션 단위**다. 예전엔 셰이더 경로만 봐서, 같은 .hlsl 을 쓰지만 정적 스위치가 다른
        // 머티리얼이 한 배치로 접혔다 — 배치는 PSO 하나로 그리므로 한쪽 퍼뮤테이션이 통째로 버려졌다.
        const uint64 hash = permutationHash;
        auto         it   = _mapShaderRepresentative.find( hash );
        if ( it != _mapShaderRepresentative.end() )
            return it->second;
        _mapShaderRepresentative.emplace( hash, pMaterial );
        return pMaterial;
    }

    bool GpuSceneBuilder::hasSameBatchKeysAsBuilt() const
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

    void GpuSceneBuilder::rebuildPartitionTables()
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
            SortKey key{ cand._mesh.get(), batchKeyMaterial( cand._material.get(), cand._permutationHash ),
                         _bMergeAcrossMaterials != SW_FALSE ? nullptr : cand._instance.get(), cand._permutationHash };
            _listScratchOpaqueEntry.push_back( SortEntry{ key, instanceIndex } );
        }

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            // 정렬 순서가 곧 **멀티 드로우 그룹의 길이**다. 드로우 루프는 (PSO · 머티리얼 버퍼 · 머티리얼 CB · 텍스처)가 같은
            // 연속 배치를 한 번의 drawIndirect(멀티 드로우)로 내므로, 그것들이 앞 키여야 그룹이 길다. 메시는 정점 풀 하나라 키가 아니어도
            // 되지만 풀 밖 메시(예산 초과)끼리 모이도록 뒤에 둔다. 2026-09-13 에 이 순서를 상태 변경만 줄이려고 바꿔 봤을 땐
            // 잡음 범위였다(배치당 호출이 비용) — 호출 수가 줄어드는 지금은 이유가 다르다.
            std::sort( _listScratchOpaqueEntry.begin(), _listScratchOpaqueEntry.end(), []( const SortEntry& entryA, const SortEntry& entryB )
            {
                if ( entryA._key._permutationHash != entryB._key._permutationHash )
                    return entryA._key._permutationHash < entryB._key._permutationHash;
                if ( entryA._key._pMaterial != entryB._key._pMaterial )
                    return entryA._key._pMaterial < entryB._key._pMaterial;
                if ( entryA._key._pMesh != entryB._key._pMesh )
                    return entryA._key._pMesh < entryB._key._pMesh;
                return entryA._key._pInstance < entryB._key._pInstance;
            } );
        }
    }

    bool GpuSceneBuilder::fillCandidateFromPrimitive( MeshComponent* pMeshComp, Scene* pScene, DrawCandidate& cand )
    {
        if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
            return false;
        GameObject* pObj = pMeshComp->getOwner();
        if ( pObj == nullptr || pObj->isActiveInHierarchy() == false )
            return false;
        Mesh* pMesh = pMeshComp->getRawMesh();
        if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
            return false;

        const float4x4 world = pMeshComp->getWorldMatrix();
        cand._world          = world;
        cand._boundsCenter   = world.getTranslation();
        cand._boundsRadius   = pMeshComp->getBoundsRadius();
        cand._spinSeed       = pMeshComp->getGpuSpinSeed();
        // 소유를 싣는다 — RT 가 upload() 에서 역참조한다. 스냅샷은 머티리얼·인스턴스의 소유도
        // 함께 싣는다(렌더 스레드가 패킷을 다 쓸 때까지 살아 있어야 한다). 세 줄 모두 **날 포인터로
        // 먼저 비교**한다 — 같으면 대입하지 않아 참조 카운트를 건드리지 않는다.
        if ( cand._mesh.get() != pMesh )
            cand._mesh = pMeshComp->getMesh();
        if ( cand._instance.get() != pMeshComp->getRawMaterialInstance() )
            cand._instance = pMeshComp->getMaterialInstance();
        // 머티리얼 없는 메시는 씬 기본 머티리얼로 (언리얼의 기본 머티리얼).
        Material* pMaterial = pMeshComp->getMaterial();
        if ( pMaterial == nullptr )
            pMaterial = pScene->getMaterial();
        if ( cand._material.get() != pMaterial )
            cand._material = GpuSceneBuilderInternal::shareMaterial( pMaterial );

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
        // 어느 PSO 로 그릴지를 정하는 값이다 — 배치 키의 일부이고, 여기서 한 번 구해 두면
        // 나누기·정렬이 다시 구하지 않는다(투명은 원소마다 물었다).
        cand._permutationHash = permutationHashFor( cand._material.get(), cand._instance.get() );
        return true;
    }

    void GpuSceneBuilder::buildFromScene( Scene* pScene, const float3& cameraPos )
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
        // 퍼뮤테이션은 프리미티브를 더럽히지 않는다 — 머티리얼/인스턴스의 정적 스위치·키워드·멀티컴파일을
        // 바꾸면 그릴 셰이더가 달라지는데 씬에서는 아무 일도 일어나지 않은 것처럼 보인다. 세대로 가른다.
        const uint64 permutationGeneration = MaterialUtil::getPermutationGeneration();
        const bool   bPermSame             = bHasCache && ( permutationGeneration == _lastPermutationGeneration );

        // 아무도 "바뀌었다"고 말하지 않았고 카메라도 그대로면 **수집 자체를 하지 않는다**.
        // 예전엔 이 판단을 하려고 매 프레임 모든 GameObject 의 모든 Component 를 castTo 로 훑어
        // 후보를 다 만든 다음에야 "그대로였네" 하고 버렸다. 씬이 커질수록, 화면이 정지해 있어도
        // 비용이 늘었다. 이제는 바꾼 쪽이 알려주므로 정지한 씬의 비용이 0 에 수렴한다.
        if ( bSetSame && bCamSame && bPermSame && primitives.hasDirty() == false )
            return;

        _listDirtyPrimitive.clear();
        pObjects->getPrimitiveRegistry().consumeDirty( _listDirtyPrimitive );

        const vector<MeshComponent*>& listPrimitive = primitives.getAll();

        // 후보 배열을 **비우지 않고 제자리에 덮어쓴다.** `clear()` + `push_back` 은 원소마다
        // shared_ptr 셋의 참조 카운트를 내렸다(clear) 올린다(push_back) — 프레임당 원자 연산이
        // 6N 번이고, 그 중 대부분이 **지난 프레임과 같은 객체**를 가리킨다(메시·머티리얼·인스턴스는
        // 물체가 움직인다고 바뀌지 않는다). 포인터가 그대로면 손대지 않는다.
        //
        // **필드는 전부 다시 채워야 한다.** 비우지 않으므로 채우지 않은 필드에는 지난 프레임 값이
        // 남는다 — `DrawCandidate` 에 필드를 더하면 아래 루프에도 같이 적을 것.
        if ( _listScratchCandidate.size() < listPrimitive.size() )
            _listScratchCandidate.resize( listPrimitive.size() );

        size_t candidateCount = 0;

        // **바뀐 것만 다시 모은다.** 집합 세대가 그대로면 등록부의 자리 배치가 그대로이므로, 지난
        // 프레임 후보를 그대로 두고 더티 프리미티브의 자리만 새로 채우면 된다. 예전에는 8000 개 중
        // 10 개만 움직여도 8000 개를 전부 다시 모았다(수집 244 us — 전부 움직일 때와 같은 값이었다).
        //
        // 조건이 하나라도 어긋나면 **아래 전체 수집으로 떨어진다** — 부분 갱신이 틀리는 것보다 느린
        // 것이 낫고, 두 경로가 같은 `fillCandidateFromPrimitive` 를 쓰므로 채우는 규칙이 갈리지 않는다.
        const bool bCanPartial = bSetSame && bPermSame && _listPrimitiveToCandidate.size() == listPrimitive.size() &&
                                 _lastCandidateCount <= _listScratchCandidate.size() + _listBuiltCandidate.size() &&
                                 _listDirtyPrimitive.size() * 4 < listPrimitive.size();
        bool bPartialDone      = false;
        bool bPartialKeysSame  = true;
        bool bPartialAnyChange = false;
        if ( bCanPartial )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.collect" );
            // 지난 프레임 후보를 작업 자리로 가져온다 — 이중 버퍼라 scratch 는 두 프레임 전 것이다.
            _listScratchCandidate.swap( _listBuiltCandidate );
            bPartialDone = _listScratchCandidate.size() >= _lastCandidateCount;

            for ( uint32 slot : _listDirtyPrimitive )
            {
                if ( bPartialDone == false )
                    break;
                if ( slot >= listPrimitive.size() )
                {
                    bPartialDone = false;
                    break;
                }
                const uint32 candidateIndex = _listPrimitiveToCandidate[slot];
                const bool   bWasIncluded   = ( candidateIndex != kInvalidCandidateIndex );

                _candidateProbe      = DrawCandidate{};
                const bool bIncluded = fillCandidateFromPrimitive( listPrimitive[slot], pScene, _candidateProbe );
                // 실릴지 말지가 바뀌면 자리 배치가 달라진다 — 그때는 통째로 다시 모은다.
                if ( bIncluded != bWasIncluded || ( bIncluded && candidateIndex >= _lastCandidateCount ) )
                {
                    bPartialDone = false;
                    break;
                }
                if ( bIncluded == false )
                    continue;

                DrawCandidate& cand = _listScratchCandidate[candidateIndex];
                if ( cand.hasSameBatchKey( _candidateProbe ) == false )
                    bPartialKeysSame = false;
                if ( cand != _candidateProbe )
                {
                    bPartialAnyChange = true;
                    cand              = _candidateProbe;
                }
            }

            if ( bPartialDone )
            {
                candidateCount = _lastCandidateCount;
            }
            else
            {
                // 되돌린다 — 아래 전체 수집이 scratch 를 처음부터 채운다.
                _listScratchCandidate.swap( _listBuiltCandidate );
            }
        }

        // 등록부에는 그릴 수 있는 것만 들어 있다 — 타입 검사가 없다.
        if ( bPartialDone == false )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.collect" );
            _listPrimitiveToCandidate.assign( listPrimitive.size(), kInvalidCandidateIndex );
            for ( size_t primitiveIndex = 0; primitiveIndex < listPrimitive.size(); ++primitiveIndex )
            {
                if ( fillCandidateFromPrimitive( listPrimitive[primitiveIndex], pScene, _listScratchCandidate[candidateCount] ) == false )
                    continue;
                _listPrimitiveToCandidate[primitiveIndex] = static_cast<uint32>( candidateCount );
                ++candidateCount;
            }
            // 걸러진 만큼 줄인다 — 남은 원소는 여기서 소유를 놓는다.
            _listScratchCandidate.resize( candidateCount );
        }
        _lastCandidateCount = candidateCount;

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
            // 부분 수집을 했으면 **무엇이 바뀌었는지 이미 안다** — 두 배열을 통째로 비교하지 않는다.
            // (그리고 그때 `_listBuiltCandidate` 는 두 프레임 전 것이라 비교 대상이 될 수도 없다.)
            bContentSame = bPartialDone ? ( bPartialAnyChange == false && _snapshot.getInstances().empty() == false )
                                        : ( bHasCache && _listBuiltCandidate == _listScratchCandidate && _snapshot.getInstances().empty() == false );
        }

        if ( bContentSame && bCamSame )
        {
            _lastPrimitiveSetGeneration = setGeneration;
            _lastPermutationGeneration  = permutationGeneration;
            return;
        }

        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        // 크기가 그대로면 raw 의 지난 프레임 값이 살아 있다 — 부분 채우기의 전제다.
        const bool bRawKept = ( _listScratchRaw.size() == count );
        _listScratchRaw.resize( count );

        // 물체가 움직이기만 했으면 배치 구성은 그대로다 — 인스턴스 값만 새로 채우고, 나누기와
        // 정렬은 건너뛴다. 움직이는 씬에서 남아 있던 유일한 O(N log N) 이 이 정렬이었다.
        //
        // 이 판단 자체가 후보 배열 **둘을 통째로 훑는다** — 스코프 없이 두면 표에서 `build` 와
        // 하위 항목들의 차이로만 나타나 아무도 보지 않는다. 재는 자리를 만들어 둔다.
        bool bBatchKeysSame = false;
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.batchKeys" );
            bBatchKeysSame = bPartialDone ? ( bContentSame == false && bPartialKeysSame )
                                          : ( bHasCache && bContentSame == false && hasSameBatchKeysAsBuilt() );
        }

        if ( bContentSame == false )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.fill" );
            // 이 스레드에서 그대로 채운다. 워커로 나누면 **모든 크기에서 느려진다** — 원소당 일이
            // 필드 몇 개 복사뿐이라 디스패치와 대기가 일보다 비싸다(GpuScene.h buildFromScene 주석의 숫자).
            //
            // 부분 수집을 했으면 **바뀐 후보만** 옮긴다. raw 는 후보에서 1:1 로 나오는 값이라, 손대지
            // 않은 후보의 raw 는 지난 프레임 것이 그대로 맞다. (전체 수집 프레임에는 raw 자체가
            // 새로 만들어지므로 전부 채워야 한다.)
            if ( bPartialDone && bRawKept )
            {
                for ( uint32 slot : _listDirtyPrimitive )
                {
                    if ( slot >= _listPrimitiveToCandidate.size() )
                        continue;
                    const uint32 candidateIndex = _listPrimitiveToCandidate[slot];
                    if ( candidateIndex >= count )
                        continue;
                    GpuSceneBuilderInternal::fillRangePtr( _listScratchCandidate.data(), _listScratchRaw.data(), candidateIndex,
                                                           candidateIndex + 1 );
                }
            }
            else
            {
                GpuSceneBuilderInternal::fillRangePtr( _listScratchCandidate.data(), _listScratchRaw.data(), 0, count );
            }

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
            const bool bRefreshed = bBatchKeysSame && refreshInstancesInPlace( bPartialDone );
            if ( bRefreshed == false )
            {
                _listInstanceWork.clear();
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
        _lastPermutationGeneration  = permutationGeneration;
        _snapshot._bCpuDirty        = SW_TRUE;

        // **내용이 바뀐 이 자리에서만 새 배열을 발행한다.** 내용이 그대로인 프레임은 여기까지 오지
        // 않으므로 지난 배열이 그대로 실린다.
        //
        // 발행은 **풀에서 빈 배열을 빌려 덮어쓰는 것**이다. 예전에는 작업 배열을 옮겨 주고 다음
        // 프레임에 되복사했는데, 그 되복사가 매 프레임 800 KB 를 새로 할당했다(74 us). 지금은
        // 작업 배열이 그대로 남아 되복사가 없고, 빌린 배열은 용량이 남아 있어 할당도 없다.
        // 복사가 아니라 **옮긴다** — 다음 프레임에 제자리 갱신이 필요하면 발행본에서 되돌려 받는다.
        // (발행용 배열을 풀에 돌려 쓰는 것도 재 봤는데 더 느렸다: 풀 2/3/8 에서 발행 85/78/76 us 로
        //  갓 할당한 블록보다 차가웠다. 800 KB 복사 자체가 ~75 us 이고 할당은 그 안에서 작다.)
        _snapshot._pListInstance = make_shared<const vector<GpuInstance>>( std::move( _listInstanceWork ) );
        _listInstanceWork.clear();
    }

    void GpuSceneBuilder::requestGpuUploads( GpuUploadQueue& queue ) const
    {
        // 배치가 메시의 소유를 들고 있으므로(스냅샷 소유 규칙) 큐에 넘겨도 워커가 도는 동안 사라지지 않는다.
        for ( const GpuMeshBatch& batch : _snapshot._listAllBatch )
            queue.requestMesh( batch._mesh );
    }

    void GpuSceneBuilder::exportCpuSnapshot( GpuSceneSnapshot& outSnapshot )
    {
        // 옮겨지는 것은 GpuSceneSnapshot 이 든 것 **전부이고 그것뿐**이다. 복사다 — 퍼뮤테이션 표는 GT 가
        // 계속 늘려 가는 정본이라 빼앗아 가면 다음 프레임의 인덱스가 0 부터 다시 매겨진다.
        outSnapshot          = _snapshot;
        _snapshot._bCpuDirty = SW_FALSE;
    }

    void GpuSceneBuilder::sortTransparent( const float3& cameraPos )
    {
        if ( _listScratchTransparentIdx.size() <= 1 )
            return;
        std::sort( _listScratchTransparentIdx.begin(), _listScratchTransparentIdx.end(), [&]( uint32 idxA, uint32 idxB )
        { return float3::getDistanceSquared( _listScratchRaw[idxA]._boundsCenter, cameraPos ) > float3::getDistanceSquared( _listScratchRaw[idxB]._boundsCenter, cameraPos ); } );
    }

    bool GpuSceneBuilder::refreshInstancesInPlace( bool bPartialCollect )
    {
        // 지난 프레임에 **발행하며 옮겨 줬으면 되돌려 받는다.** 제자리 갱신은 이전 값이 필요하다
        // (`_meshBatchIndex`·`_materialIndex` 는 배치 구성이 같으므로 그대로 쓴다). 800 KB 복사라
        // ~73 us 다 — 내용이 바뀌는 프레임에만 일어나고, 정적 씬은 여기까지 오지 않는다.
        if ( _listInstanceWork.empty() && _snapshot._pListInstance != nullptr )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.restore" );
            _listInstanceWork = *_snapshot._pListInstance;
        }

        // 매핑이 인스턴스 수와 맞아야 한다. 한 번이라도 전체 빌드를 안 했으면 못 쓴다.
        if ( _listInstanceWork.empty() || _listInstanceSrcIndex.size() != _listInstanceWork.size() )
            return false;
        // 투명은 카메라 거리로 매 프레임 다시 정렬한다. 그 순서가 바뀌면 어느 인스턴스가 어느 자리에
        // 앉는지가 달라지므로 매핑을 그대로 쓸 수 없다.
        if ( _listScratchTransparentIdx != _listBuiltTransparentIdx )
            return false;

        // **바뀐 슬롯만 적어 둔다.** 받는 쪽이 그 구간만 GPU 에 올린다 — 예전에는 하나만 움직여도
        // 인스턴스 버퍼 전체를 다시 올렸다. 구간이 너무 잘게 흩어지면 작은 업로드가 도리어 비싸므로
        // 상한을 넘기면 전체로 돌린다(그때는 구간 목록이 뜻을 잃는다).
        _snapshot._listDirtyInstanceRun.clear();
        _snapshot._bAllInstancesDirty = SW_FALSE;
        bool   bTooManyRuns           = false;
        size_t lastDirtySlot          = static_cast<size_t>( -1 );

        const uint32 rawCount = static_cast<uint32>( _listScratchRaw.size() );

        // **바뀐 인스턴스만 훑는다.** 부분 수집을 했으면 어느 후보가 달라졌는지 알고, 후보 -> 인스턴스
        // 역매핑이 있으니 그 자리만 고치면 된다. 8000 개 중 10 개가 움직일 때 이 루프가 59 -> 1 us 다.
        //
        // 회전 인스턴스 수는 전부 훑지 않으므로 **증감으로 유지한다** — 슬롯 하나를 고칠 때 옛 값이
        // 0 이 아니었으면 빼고 새 값이 0 이 아니면 더한다. 전체 훑기 경로만 0 부터 다시 센다.
        const bool bPartialRefresh = bPartialCollect && _listCandidateToInstance.size() == _listScratchCandidate.size();
        if ( bPartialRefresh == false )
            _snapshot._spinInstanceCount = 0;

        SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.loop" );
        const size_t slotCount = _listInstanceWork.size();
        const size_t stepCount = bPartialRefresh ? _listDirtyPrimitive.size() : slotCount;
        for ( size_t step = 0; step < stepCount; ++step )
        {
            size_t slot = step;
            if ( bPartialRefresh )
            {
                const uint32 primitiveSlot = _listDirtyPrimitive[step];
                if ( primitiveSlot >= _listPrimitiveToCandidate.size() )
                    return false;
                const uint32 candidateIndex = _listPrimitiveToCandidate[primitiveSlot];
                if ( candidateIndex >= _listCandidateToInstance.size() )
                    continue;
                const uint32 instanceSlot = _listCandidateToInstance[candidateIndex];
                if ( instanceSlot == kInvalidCandidateIndex )
                    continue;
                if ( instanceSlot >= slotCount )
                    return false;
                slot = instanceSlot;
            }

            const uint32 srcIndex = _listInstanceSrcIndex[slot];
            if ( srcIndex >= rawCount )
                return false;

            // 배치 구성이 같으므로 _meshBatchIndex 와 _materialIndex 는 그대로다 — 바뀐 것은
            // 트랜스폼과 바운드, 그리고 회전 시드뿐이다.
            GpuInstance&       inst = _listInstanceWork[slot];
            const GpuInstance& raw  = _listScratchRaw[srcIndex];

            // 비교는 **비트 그대로** 한다 — 엡실론 비교는 매 프레임 엡실론 미만으로 움직이는 물체를
            // 영원히 "안 바뀜" 으로 보고 화면에 오차를 누적시킨다(DrawCandidate::operator== 와 같은 이유).
            const bool bChanged = Memory::compare( &inst._world, &raw._world, sizeof( inst._world ) ) != 0 ||
                                  Memory::compare( &inst._boundsCenter, &raw._boundsCenter, sizeof( inst._boundsCenter ) ) != 0 ||
                                  Memory::compare( &inst._boundsRadius, &raw._boundsRadius, sizeof( inst._boundsRadius ) ) != 0 ||
                                  inst._blendMode != raw._blendMode || inst._spinSeed != raw._spinSeed;

            inst._world                   = raw._world;
            inst._boundsCenter            = raw._boundsCenter;
            inst._boundsRadius            = raw._boundsRadius;
            inst._blendMode               = raw._blendMode;
            const uint32 previousSpinSeed = inst._spinSeed;
            inst._spinSeed                = raw._spinSeed;
            if ( bPartialRefresh )
            {
                if ( previousSpinSeed != 0 && inst._spinSeed == 0 && _snapshot._spinInstanceCount > 0 )
                    --_snapshot._spinInstanceCount;
                else if ( previousSpinSeed == 0 && inst._spinSeed != 0 )
                    ++_snapshot._spinInstanceCount;
            }
            else if ( inst._spinSeed != 0 )
            {
                ++_snapshot._spinInstanceCount;
            }

            if ( bChanged == false || bTooManyRuns )
                continue;

            if ( lastDirtySlot + 1 == slot && _snapshot._listDirtyInstanceRun.empty() == false )
            {
                ++_snapshot._listDirtyInstanceRun.back()._count;
            }
            else if ( _snapshot._listDirtyInstanceRun.size() >= kMaxDirtyInstanceRun )
            {
                bTooManyRuns = true;
            }
            else
            {
                _snapshot._listDirtyInstanceRun.push_back( GpuInstanceRun{ static_cast<uint32>( slot ), 1 } );
            }
            lastDirtySlot = slot;
        }

        if ( bTooManyRuns )
        {
            _snapshot._listDirtyInstanceRun.clear();
            _snapshot._bAllInstancesDirty = SW_TRUE;
        }

        // 배치 구성은 그대로지만 **회수 시계는 돌아야 한다**. 안 그러면 물체가 움직이기만 하는 씬에서
        // 시계가 멈춰, 안 쓰이게 된 머티리얼 원소가 영원히 회수되지 않는다(자리가 조금씩 샌다).
        // 지금 인스턴스가 가리키는 원소는 전부 살아 있으므로 이번 빌드 번호로 도장을 찍어 둔다.
        // **부분 갱신 프레임에는 회수 시계를 돌리지 않는다.** 시계를 돌리면서 더티 인스턴스의 원소만
        // 도장을 찍으면, 손대지 않은(그러나 여전히 쓰이는) 원소가 낡은 것으로 보여 회수돼 버린다.
        // 시계를 멈추면 아무것도 낡지 않으므로 잘못된 회수가 생기지 않는다 — 회수는 전체 훑기
        // 프레임(집합 변화·큰 변경)으로 미뤄질 뿐이다.
        if ( bPartialRefresh )
            return true;

        ++_buildCounter;
        SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.stamp" );
        for ( const GpuInstance& inst : _listInstanceWork )
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

    void GpuSceneBuilder::buildBatches()
    {
        // 전체 재구축이다 — 구간을 적어 봐야 전부이므로 받는 쪽이 통째로 올리게 한다.
        _snapshot._bAllInstancesDirty = SW_TRUE;
        _snapshot._listDirtyInstanceRun.clear();
        _listInstanceWork.reserve( _listScratchCandidate.size() );
        _listInstanceSrcIndex.clear();
        _listInstanceSrcIndex.reserve( _listScratchCandidate.size() );
        // 후보 -> 인스턴스 슬롯 역매핑. 더티 후보의 인스턴스 자리를 바로 찾기 위한 것이다.
        _listCandidateToInstance.assign( _listScratchCandidate.size(), kInvalidCandidateIndex );
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
                    batch._instanceBase  = static_cast<uint32>( _listInstanceWork.size() );
                    batch._instanceCount = entryIndex - batchStart;
                    batch._blendMode     = RHIBlendMode::Opaque;
                    // 키의 포인터는 정체성이고 소유는 배치 머리 후보의 것을 빌린다. 합치기가 켜지면 키의 인스턴스는
                    // nullptr 이라 배치에는 인스턴스를 싣지 않는다 — 원소 표(_listEntry)가 인스턴스마다 소유를 든다.
                    const DrawCandidate& headCand = _listScratchCandidate[_listScratchOpaqueEntry[batchStart]._srcIdx];
                    batch._material               = GpuSceneBuilderInternal::shareMaterial( key._pMaterial );
                    batch._materialInstance       = ( key._pInstance != nullptr ) ? headCand._instance : nullptr;
                    if ( key._pInstance != nullptr )
                        batch._materialCb = key._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = key._pMaterial ? key._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;
                    // 텍스처 슬롯은 인스턴스가 아니라 부모 머티리얼이 소유한다(인스턴스는 CB 값만 덮어쓴다).
                    GpuSceneBuilderInternal::fillMaterialTextureSrvs( batch, key._pMaterial );
                    batch._materialGroup = materialGroupFor( key._pMaterial );
                    // 퍼뮤테이션은 **키가 아니라 배치에 실제로 든 후보**에서 뽑는다. 합치기가 켜지면
                    // 키의 인스턴스는 nullptr 이라, 키로 물으면 대표 머티리얼의 define 만 나오고
                    // 인스턴스가 켠 키워드가 통째로 빠진다 — 그 배치는 잘못된 셰이더로 그려진다.
                    // 한 배치의 구성원은 전부 같은 퍼뮤테이션 해시를 가지므로 아무 구성원이나 맞다.
                    batch._shaderPermutation = shaderPermutationFor( headCand._material.get(), headCand._instance.get() );
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
                        if ( srcIdx < _listCandidateToInstance.size() )
                            _listCandidateToInstance[srcIdx] = static_cast<uint32>( _listInstanceSrcIndex.size() );
                        _listInstanceSrcIndex.push_back( srcIdx );
                        _listInstanceWork.push_back( inst );
                    }
                    // 두 목록이 같은 배치를 든다. 앞쪽은 복사해야 하지만 마지막 하나는 옮길 수 있다 —
                    // 배치마다 shared_ptr 셋의 참조 카운트가 한 벌씩 줄어든다.
                    _snapshot._listOpaqueBatch.push_back( batch );
                    _snapshot._listAllBatch.push_back( std::move( batch ) );

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
            Material*            pBatchHeadKey = batchKeyMaterial( pBatchHead->_material.get(), pBatchHead->_permutationHash );

            for ( uint32 entryIndex = 1; entryIndex <= _listScratchTransparentIdx.size(); ++entryIndex )
            {
                const bool bEnd = entryIndex == _listScratchTransparentIdx.size();
                bool       bKeyChange{ false };
                if ( bEnd == false )
                {
                    const DrawCandidate& current = _listScratchCandidate[_listScratchTransparentIdx[entryIndex]];
                    bKeyChange                   = ( pBatchHead->_mesh != current._mesh ) ||
                                 ( pBatchHead->_permutationHash != current._permutationHash ) ||
                                 ( pBatchHeadKey != batchKeyMaterial( current._material.get(), current._permutationHash ) ) ||
                                 ( _bMergeAcrossMaterials == SW_FALSE && pBatchHead->_instance != current._instance );
                }
                if ( bEnd || bKeyChange )
                {
                    const DrawCandidate& cand = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    GpuMeshBatch         batch{};
                    batch._mesh             = cand._mesh;
                    batch._vertexCount      = batch._mesh->getVertexCount();
                    batch._instanceBase     = static_cast<uint32>( _listInstanceWork.size() );
                    batch._instanceCount    = entryIndex - batchStart;
                    batch._blendMode        = RHIBlendMode::Transparent;
                    batch._materialInstance = cand._instance;
                    batch._material         = cand._material;
                    if ( cand._instance.get() != nullptr )
                        batch._materialCb = cand._instance.get()->getDescriptorIndex();
                    else
                        batch._materialCb = cand._material.get() ? cand._material.get()->getDescriptorIndex() : kInvalidDescriptorIndex;
                    GpuSceneBuilderInternal::fillMaterialTextureSrvs( batch, cand._material.get() );
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
                        if ( srcIdx < _listCandidateToInstance.size() )
                            _listCandidateToInstance[srcIdx] = static_cast<uint32>( _listInstanceSrcIndex.size() );
                        _listInstanceSrcIndex.push_back( srcIdx );
                        _listInstanceWork.push_back( inst );
                    }
                    _snapshot._listTransparentBatch.push_back( batch );
                    _snapshot._listAllBatch.push_back( std::move( batch ) );
                    batchStart = entryIndex;
                    if ( bEnd == false )
                    {
                        pBatchHead    = &_listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                        pBatchHeadKey = batchKeyMaterial( pBatchHead->_material.get(), pBatchHead->_permutationHash );
                    }
                }
            }
        }
    }

    void GpuSceneBuilder::retireUnusedMaterialElements()
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

    void GpuSceneBuilder::resetMaterialRegistry()
    {
        _snapshot._listMaterialGroup.clear();
        _mapShaderPathToGroup.clear();
        _snapshot._listShaderPermutation.clear();
        _mapPermutationToIndex.clear();
    }

    uint32 GpuSceneBuilder::materialGroupFor( const Material* pMaterial )
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

    uint32 GpuSceneBuilder::assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex )
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
} // namespace sw
