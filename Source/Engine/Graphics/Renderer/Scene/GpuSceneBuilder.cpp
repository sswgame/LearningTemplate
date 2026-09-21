#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineParallel.h"
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
             * @brief 인스턴스 페이로드(월드·바운드·블렌드·시드)가 raw 와 **비트 그대로** 다른지.
             * @details 엡실론 비교는 매 프레임 엡실론 미만으로 움직이는 물체를 영원히 "안 바뀜" 으로 보고 화면에
             *          오차를 누적시킨다(DrawCandidate::operator== 와 같은 이유). 전체 갱신과 부분 갱신이 같은 판정을 쓴다.
             */
            static bool isPayloadChanged( const GpuInstance& inst, const GpuInstance& raw )
            {
                return Memory::compare( &inst._world, &raw._world, sizeof( inst._world ) ) != 0 ||
                       Memory::compare( &inst._boundsCenter, &raw._boundsCenter, sizeof( inst._boundsCenter ) ) != 0 ||
                       Memory::compare( &inst._boundsRadius, &raw._boundsRadius, sizeof( inst._boundsRadius ) ) != 0 ||
                       inst._blendMode != raw._blendMode || inst._spinSeed != raw._spinSeed;
            }

            /** @brief raw 의 페이로드를 인스턴스에 옮깁니다. `_meshBatchIndex`·`_materialIndex` 는 그대로다 — 배치 구성이 같을 때만 부른다. */
            static void copyPayload( const GpuInstance& raw, GpuInstance& outInstance )
            {
                outInstance._world        = raw._world;
                outInstance._boundsCenter = raw._boundsCenter;
                outInstance._boundsRadius = raw._boundsRadius;
                outInstance._blendMode    = raw._blendMode;
                outInstance._spinSeed     = raw._spinSeed;
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
        _listInstanceRing.clear();
        _listInstanceRingBuild.clear();
        _pInstanceWrite.reset();
        _writeSlotIndex = 0;
        _pInstancePublished.reset();
        _listPublishHistory.clear();
        _publishCounter = 0;
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
        _listScratchOpaqueIdx.clear();
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

        // copy-on-write — 목록은 RT 와 공유하므로 제자리에 더하지 않는다. 새 퍼뮤테이션은 드물어(머티리얼 종류만큼) 복사가 싸다.
        shared_ptr<vector<GpuShaderPermutation>> pList = ( _snapshot._pListShaderPermutation != nullptr )
                                                           ? make_shared<vector<GpuShaderPermutation>>( *_snapshot._pListShaderPermutation )
                                                           : make_shared<vector<GpuShaderPermutation>>();
        pList->push_back( std::move( permutation ) );
        _snapshot._pListShaderPermutation = pList;

        const uint32 index = static_cast<uint32>( pList->size() - 1 );
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
        // 배치 방출은 불투명·투명이 같은 함수를 쓰고, 그 함수는 후보 인덱스 배열을 받는다 — 정렬된 항목에서 인덱스만 뽑아 둔다.
        _listScratchOpaqueIdx.resize( _listScratchOpaqueEntry.size() );
        for ( size_t entryIndex = 0; entryIndex < _listScratchOpaqueEntry.size(); ++entryIndex )
            _listScratchOpaqueIdx[entryIndex] = _listScratchOpaqueEntry[entryIndex]._srcIdx;
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
        // 퍼뮤테이션 해시는 **여기서 구하지 않는다** — 부르는 쪽이 게임 스레드에서 찍는다(stampPermutationHash).
        // 머티리얼의 해시 게터는 더티 플래그를 보고 캐시를 다시 만드는 지연 계산이라, 같은 머티리얼을
        // 나눠 쓰는 프리미티브들을 워커 여럿이 동시에 채우면 그 캐시를 동시에 고쳐 쓰게 된다.
        return true;
    }

    void GpuSceneBuilder::stampPermutationHash( DrawCandidate& cand )
    {
        // 어느 PSO 로 그릴지를 정하는 값이다 — 배치 키의 일부이고, 여기서 한 번 구해 두면
        // 나누기·정렬이 다시 구하지 않는다(투명은 원소마다 물었다).
        cand._permutationHash = permutationHashFor( cand._material.get(), cand._instance.get() );
    }

    void GpuSceneBuilder::fillPayload( const DrawCandidate& cand, GpuInstance& outInstance )
    {
        outInstance._world        = cand._world;
        outInstance._boundsCenter = cand._boundsCenter;
        outInstance._boundsRadius = cand._boundsRadius;
        outInstance._blendMode    = cand._blendMode;
        outInstance._spinSeed     = cand._spinSeed;
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
                if ( bIncluded )
                    stampPermutationHash( _candidateProbe );
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
        //
        // **워커가 자기 칸에서 비교까지 끝낸다** — 후보 채우기, 지난 후보와의 배치 키·내용 비교, 퍼뮤테이션
        // 해시 재사용. 예전에는 채우기만 워커가 하고 직렬 패스 둘이 같은 8000 칸을 따로 지나갔다: 해시 찍기
        // (전부, 머티리얼 게터 두 번씩) · 배치 키 비교(전부, 65~81 us). 직렬에 남는 것은 앞으로 당기기와,
        // 머티리얼·인스턴스·세대 중 하나가 달라진 칸의 해시 찍기(지연 캐시라 직렬)뿐이다.
        //
        // raw 페이로드는 **여기서 쓰지 않는다.** 워커가 자기 칸의 raw 까지 쓰는 판을 재 봤는데(2026-09-21, 같은
        // 조건 ×2) 채우기 패스 65 us 가 사라진 만큼이 수집(+50~115)과 배치(+66~98)에 도로 붙었다 — 정렬과 제자리
        // 갱신이 다른 코어가 쓴 raw 를 원격 캐시에서 끌어온다. 이 스레드가 이어서 쓰고 이어서 읽는 편이 싸다.
        bool bAllKeySame     = false;
        bool bAllContentSame = false;
        if ( bPartialDone == false )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.collect" );
            const uint32 primitiveCount = static_cast<uint32>( listPrimitive.size() );
            _listCollectFlag.resize( primitiveCount );

            // 지난 프레임의 "프리미티브 -> 후보" 표는 집합 세대가 같을 때만 뜻이 있다(해제는 자리를 옮긴다).
            // 표가 그 크기가 아니면 지난 후보와 짝을 지을 수 없으니 전부 "처음 본 것" 으로 간다.
            const bool bPrevAligned = bHasCache && bSetSame && _listPrimitiveToCandidate.size() == primitiveCount;
            if ( bPrevAligned == false )
                _listPrimitiveToCandidate.assign( primitiveCount, kInvalidCandidateIndex );

            // 워커는 컨테이너를 만지지 않는다 — 포인터만 넘긴다(컨테이너 레이스 탐지기가 워커의 인덱싱을 잡는다).
            // 지난 후보 배열은 **읽기만** 한다 — const 로 꺼낸 포인터다.
            struct CollectJob
            {
                GpuSceneBuilder*      _pBuilder{ nullptr };
                Scene*                _pScene{ nullptr };
                MeshComponent* const* _ppPrimitive{ nullptr };
                DrawCandidate*        _pCandidate{ nullptr };
                uint8*                _pFlag{ nullptr };
                const uint32*         _pPrevMap{ nullptr };
                const DrawCandidate*  _pPrevCandidate{ nullptr };
                uint32                _prevCount{ 0 };
                bool                  _bPermSame{ false };

                void fillRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        DrawCandidate& cand = _pCandidate[index];
                        uint8          flag = 0u;
                        if ( _pBuilder->fillCandidateFromPrimitive( _ppPrimitive[index], _pScene, cand ) )
                        {
                            flag                   = kCollectIncluded | kCollectNeedsStamp;
                            const uint32 prevIndex = ( _pPrevMap != nullptr ) ? _pPrevMap[index] : kInvalidCandidateIndex;
                            if ( prevIndex < _prevCount )
                            {
                                const DrawCandidate& prev = _pPrevCandidate[prevIndex];
                                // 해시는 (머티리얼, 인스턴스, 퍼뮤테이션 세대) 의 함수다 — 셋이 같으면 지난 값이 그대로 맞고,
                                // 그래야 키·내용 비교도 뜻이 있다(해시가 키에 들어 있다).
                                const bool bSameShader = _bPermSame && prev._material == cand._material && prev._instance == cand._instance;
                                if ( bSameShader )
                                {
                                    cand._permutationHash = prev._permutationHash;
                                    flag                  = kCollectIncluded;
                                    if ( cand.hasSameBatchKey( prev ) )
                                    {
                                        flag |= kCollectKeySame;
                                        if ( cand == prev )
                                            flag |= kCollectContentSame;
                                    }
                                }
                            }
                        }
                        _pFlag[index] = flag;
                    }
                }
            };
            CollectJob job{};
            job._pBuilder       = this;
            job._pScene         = pScene;
            job._ppPrimitive    = listPrimitive.data();
            job._pCandidate     = _listScratchCandidate.data();
            job._pFlag          = _listCollectFlag.data();
            job._pPrevMap       = bPrevAligned ? std::as_const( _listPrimitiveToCandidate ).data() : nullptr;
            job._pPrevCandidate = std::as_const( _listBuiltCandidate ).data();
            job._prevCount      = static_cast<uint32>( _listBuiltCandidate.size() );
            job._bPermSame      = bPermSame;

            engine::runParallel( primitiveCount, kParallelCollectPrimitiveCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &CollectJob::fillRange, &job ) );

            // **앞으로 당긴다.** 전부 실리는 씬(벤치가 그렇다)에서는 자리가 그대로라 한 칸도 옮기지 않는다.
            // "지난 프레임과 같다" 는 칸마다의 표시를 여기서 하나로 줄인다 — 자리까지 같아야 한다(prevIndex == 새 자리).
            bAllKeySame     = bPrevAligned;
            bAllContentSame = bPrevAligned;
            for ( uint32 primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex )
            {
                const uint8  flag      = _listCollectFlag[primitiveIndex];
                const uint32 prevIndex = _listPrimitiveToCandidate[primitiveIndex]; // 쓰기 전에 읽는다 — 같은 표를 제자리에서 새로 쓴다
                if ( ( flag & kCollectIncluded ) == 0u )
                {
                    _listPrimitiveToCandidate[primitiveIndex] = kInvalidCandidateIndex;
                    continue;
                }
                if ( candidateCount != primitiveIndex )
                    _listScratchCandidate[candidateCount] = std::move( _listScratchCandidate[primitiveIndex] );
                // 지연 캐시를 건드리는 해시는 여기 직렬 구간에서, 달라진 칸만 찍는다.
                if ( ( flag & kCollectNeedsStamp ) != 0u )
                    stampPermutationHash( _listScratchCandidate[candidateCount] );
                const bool bSamePlace                     = ( prevIndex == candidateCount );
                bAllKeySame                               = bAllKeySame && bSamePlace && ( flag & kCollectKeySame ) != 0u;
                bAllContentSame                           = bAllContentSame && bSamePlace && ( flag & kCollectContentSame ) != 0u;
                _listPrimitiveToCandidate[primitiveIndex] = static_cast<uint32>( candidateCount );
                ++candidateCount;
            }
            // 걸러진 만큼 줄인다 — 남은 원소는 여기서 소유를 놓는다.
            _listScratchCandidate.resize( candidateCount );
            // 지난 후보가 더 많았다면(무언가 빠졌다) 같을 수 없다.
            bAllKeySame     = bAllKeySame && ( candidateCount == _listBuiltCandidate.size() );
            bAllContentSame = bAllContentSame && ( candidateCount == _listBuiltCandidate.size() );
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
            // 전체 수집은 워커가 칸마다 비교를 끝냈다 — 여기는 그 합만 읽는다(예전엔 두 배열을 통째로 다시 비교했다).
            bContentSame = bPartialDone ? ( bPartialAnyChange == false && _snapshot.getInstances().empty() == false )
                                        : ( bAllContentSame && _snapshot.getInstances().empty() == false );
        }

        if ( bContentSame && bCamSame )
        {
            _lastPrimitiveSetGeneration = setGeneration;
            _lastPermutationGeneration  = permutationGeneration;
            return;
        }

        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        // 크기가 그대로면 raw 의 지난 프레임 값이 살아 있다 — 부분 채우기의 전제다. (전체 수집은 raw 를 통째로 새로 썼다.)
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
                                          : ( bContentSame == false && bAllKeySame );
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
            // raw 는 **이 스레드가** 쓴다. 워커가 자기 칸의 raw 까지 쓰게 해 봤지만(2026-09-21) 정렬·제자리 갱신이
            // 그 raw 를 읽을 때 원격 캐시에서 끌어와 배치 단계가 +66~98 us — 여기서 뺀 만큼이 저기서 도로 붙었다.
            if ( bPartialDone && bRawKept )
            {
                for ( uint32 slot : _listDirtyPrimitive )
                {
                    if ( slot >= _listPrimitiveToCandidate.size() )
                        continue;
                    const uint32 candidateIndex = _listPrimitiveToCandidate[slot];
                    if ( candidateIndex >= count )
                        continue;
                    fillPayload( _listScratchCandidate[candidateIndex], _listScratchRaw[candidateIndex] );
                }
            }
            else
            {
                for ( uint32 candidateIndex = 0; candidateIndex < count; ++candidateIndex )
                    fillPayload( _listScratchCandidate[candidateIndex], _listScratchRaw[candidateIndex] );
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
                instanceWork().clear();
                _snapshot._listOpaqueBatch.clear();
                _snapshot._listTransparentBatch.clear();
                _snapshot._listAllBatch.clear();
                buildBatches();
            }
            {
                SW_PROFILE_SCOPE( "GT.GpuScene.build.retire" );
                retireUnusedMaterialElements();
            }
        }

        // scratch 를 기준 집합으로 넘기고 낡은 기준을 scratch 로 돌려받는다 — 복사 없이 두 버퍼를
        // 번갈아 쓰므로 프레임당 힙 할당이 생기지 않는다.
        _listBuiltCandidate.swap( _listScratchCandidate );
        _lastCameraPos              = cameraPos;
        _lastPrimitiveSetGeneration = setGeneration;
        _lastPermutationGeneration  = permutationGeneration;
        _snapshot._bCpuDirty        = SW_TRUE;

        // **내용이 바뀐 이 자리에서만 발행한다.** 내용이 그대로인 프레임은 여기까지 오지 않으므로 지난 배열이 그대로 실린다.
        // 발행은 링 슬롯의 포인터를 넘기는 것이다 — 복사도 옮기기도 되복사도 없다(헤더의 링 주석).
        SW_PROFILE_SCOPE( "GT.GpuScene.build.publish" );
        publishInstances();
    }

    vector<GpuInstance>& GpuSceneBuilder::instanceWork()
    {
        if ( _pInstanceWrite != nullptr )
            return *_pInstanceWrite;

        // 아무도 안 읽는 슬롯 — 링만 들고 있는 것. 발행본과 패킷이 든 슬롯은 use_count 가 2 이상이라 걸러진다.
        for ( uint32 slotIndex = 0; slotIndex < _listInstanceRing.size(); ++slotIndex )
        {
            if ( _listInstanceRing[slotIndex] != nullptr && _listInstanceRing[slotIndex].use_count() == 1 )
            {
                _pInstanceWrite = _listInstanceRing[slotIndex];
                _writeSlotIndex = slotIndex;
                return *_pInstanceWrite;
            }
        }
        // 모자라면 하나 더 — 렌더 큐가 깊은 만큼만 자란다(패킷이 슬롯을 놓으면 그 슬롯이 다시 골라진다).
        _listInstanceRing.push_back( make_shared<vector<GpuInstance>>() );
        _listInstanceRingBuild.push_back( 0 );
        _writeSlotIndex = static_cast<uint32>( _listInstanceRing.size() - 1 );
        _pInstanceWrite = _listInstanceRing.back();
        return *_pInstanceWrite;
    }

    void GpuSceneBuilder::publishInstances()
    {
        if ( _pInstanceWrite == nullptr )
            return;
        ++_publishCounter;
        if ( _writeSlotIndex < _listInstanceRingBuild.size() )
            _listInstanceRingBuild[_writeSlotIndex] = _publishCounter;

        PublishRecord record{};
        record._build = _publishCounter;
        record._bAll  = _snapshot._bAllInstancesDirty;
        if ( record._bAll == SW_FALSE )
            record._listRun = _snapshot._listDirtyInstanceRun;
        if ( _listPublishHistory.size() >= kPublishHistoryCount )
            _listPublishHistory.erase( _listPublishHistory.begin() );
        _listPublishHistory.push_back( std::move( record ) );

        _pInstancePublished      = _pInstanceWrite;
        _snapshot._pListInstance = _pInstancePublished;
        _pInstanceWrite.reset();
    }

    void GpuSceneBuilder::syncWriteSlotFromPublished()
    {
        if ( _pInstancePublished == nullptr )
            return;
        const vector<GpuInstance>& prev = *_pInstancePublished;
        vector<GpuInstance>&       work = instanceWork();

        // 슬롯이 발행된 뒤 무엇이 바뀌었나 — 이력에서 (슬롯의 발행 번호, 마지막 발행 번호] 를 모은다.
        const uint64 slotBuild = ( _writeSlotIndex < _listInstanceRingBuild.size() ) ? _listInstanceRingBuild[_writeSlotIndex] : 0;
        bool         bWhole    = ( slotBuild == 0 ) || ( work.size() != prev.size() );
        uint64       expected  = slotBuild + 1;
        if ( bWhole == false )
        {
            for ( const PublishRecord& record : _listPublishHistory )
            {
                if ( record._build <= slotBuild )
                    continue;
                // 이력이 끊겼으면(중간 발행이 밀려났으면) 통째로.
                if ( record._build != expected || record._bAll != SW_FALSE )
                {
                    bWhole = true;
                    break;
                }
                ++expected;
            }
            if ( expected != _publishCounter + 1 )
                bWhole = true;
        }

        work.resize( prev.size() );
        if ( bWhole )
        {
            if ( prev.empty() == false )
                Memory::copy( work.data(), prev.data(), prev.size() * sizeof( GpuInstance ) );
            return;
        }
        for ( const PublishRecord& record : _listPublishHistory )
        {
            if ( record._build <= slotBuild )
                continue;
            for ( const GpuInstanceRun& run : record._listRun )
            {
                const size_t start = MathUtil::min<size_t>( run._start, prev.size() );
                const size_t end   = MathUtil::min<size_t>( static_cast<size_t>( run._start ) + run._count, prev.size() );
                if ( end > start )
                    Memory::copy( work.data() + start, prev.data() + start, ( end - start ) * sizeof( GpuInstance ) );
            }
        }
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
        SW_PROFILE_SCOPE( "GT.GpuScene.build.sortTransparent" );

        // 키를 한 번만 구한다 — 비교 함수 안에서 거리를 다시 구하면 원소마다 raw 를 무작위로 다시 읽는다(헤더 주석).
        const size_t transparentCount = _listScratchTransparentIdx.size();
        _listTransparentSortKey.resize( transparentCount );
        for ( size_t sortIndex = 0; sortIndex < transparentCount; ++sortIndex )
        {
            const uint32 candidateIndex                         = _listScratchTransparentIdx[sortIndex];
            _listTransparentSortKey[sortIndex]._distanceSquared = float3::getDistanceSquared( _listScratchRaw[candidateIndex]._boundsCenter, cameraPos );
            _listTransparentSortKey[sortIndex]._candidateIndex  = candidateIndex;
        }
        // 먼 것부터. 거리가 같으면 후보 인덱스로 — 정렬이 결정적이어야 "순서가 그대로" 판정이 흔들리지 않는다.
        std::sort( _listTransparentSortKey.begin(), _listTransparentSortKey.end(), []( const TransparentSortKey& keyA, const TransparentSortKey& keyB )
        {
            if ( keyA._distanceSquared != keyB._distanceSquared )
                return keyA._distanceSquared > keyB._distanceSquared;
            return keyA._candidateIndex < keyB._candidateIndex;
        } );
        for ( size_t sortIndex = 0; sortIndex < transparentCount; ++sortIndex )
            _listScratchTransparentIdx[sortIndex] = _listTransparentSortKey[sortIndex]._candidateIndex;
    }

    bool GpuSceneBuilder::refreshInstancesInPlace( bool bPartialCollect )
    {
        // 이전 값은 마지막 발행본에서 **읽기만** 한다(`_meshBatchIndex`·`_materialIndex` 는 배치 구성이 같으므로
        // 그대로 옮긴다). 결과는 아무도 안 읽는 링 슬롯에 쓴다 — 되복사가 없다(헤더의 링 주석).
        const vector<GpuInstance>* pPrevious = _pInstancePublished.get();
        // 매핑이 인스턴스 수와 맞아야 한다. 한 번이라도 전체 빌드를 안 했으면 못 쓴다.
        if ( pPrevious == nullptr || pPrevious->empty() || _listInstanceSrcIndex.size() != pPrevious->size() )
            return false;
        // 투명은 카메라 거리로 매 프레임 다시 정렬한다. 그 순서가 바뀌면 투명 인스턴스의 자리가 달라지지만
        // **불투명 접두부는 그대로다** — 접두부만 제자리 갱신하고 꼬리는 새 순서로 다시 방출한다.
        const bool bTransparentOrderChanged = ( _listScratchTransparentIdx != _listBuiltTransparentIdx );
        if ( bTransparentOrderChanged && ( _opaqueInstanceCount > pPrevious->size() || _opaqueBatchCount > _snapshot._listAllBatch.size() ||
                                           _opaqueElementEntryCount > _listBatchElementIndex.size() ) )
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
        // 투명 꼬리를 다시 짓는 프레임은 접두부를 통째로 훑는다 — 더티 목록에는 자리가 바뀔 투명 후보도 섞여 있다.
        const bool bPartialRefresh = bPartialCollect && bTransparentOrderChanged == false && _listCandidateToInstance.size() == _listScratchCandidate.size();
        if ( bPartialRefresh == false )
            _snapshot._spinInstanceCount = 0;

        SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.loop" );
        // 쓰기 슬롯을 잡는다. 전체 훑기는 슬롯 전부를 새로 쓰므로 낡은 내용이 상관없고, 부분 훑기는 안 건드릴 자리가
        // 발행본과 같아야 하므로 먼저 맞춘다(슬롯이 발행된 뒤 바뀐 구간만).
        vector<GpuInstance>& work = instanceWork();
        if ( bPartialRefresh )
        {
            SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.sync" );
            syncWriteSlotFromPublished();
        }
        else
        {
            work.resize( pPrevious->size() );
        }
        // 꼬리를 다시 지을 때는 접두부만 제자리 갱신이다.
        const size_t slotCount = bTransparentOrderChanged ? _opaqueInstanceCount : pPrevious->size();

        // **전체 훑기는 청크로 나눠 병렬로 돈다.** 슬롯 구간이 연속이라 더티 구간도 청크 안에서 만들고
        // 끝난 뒤 경계만 이어 붙인다. 부분 훑기는 더티 목록 순서라 구간이 흩어지므로 예전 직렬 루프 그대로다.
        if ( bPartialRefresh == false )
        {
            constexpr uint32 kRefreshChunkSize = 2048;
            const uint32     chunkCount        = static_cast<uint32>( ( slotCount + kRefreshChunkSize - 1 ) / kRefreshChunkSize );
            _listRefreshChunk.resize( chunkCount );
            for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
            {
                InstanceRefreshChunk& chunk = _listRefreshChunk[chunkIndex];
                chunk._start                = chunkIndex * kRefreshChunkSize;
                chunk._end                  = static_cast<uint32>( MathUtil::min<size_t>( chunk._start + kRefreshChunkSize, slotCount ) );
                chunk._spinCount            = 0;
                chunk._runCount             = 0;
                chunk._bFailed              = SW_FALSE;
                chunk._bTooManyRun          = SW_FALSE;
            }

            struct RefreshJob
            {
                GpuInstance*          _pInstance{ nullptr };
                const GpuInstance*    _pPrevious{ nullptr };
                const GpuInstance*    _pRaw{ nullptr };
                const uint32*         _pSrcIndex{ nullptr };
                uint32                _rawCount{ 0 };
                InstanceRefreshChunk* _pChunk{ nullptr };

                void refreshRange( uint32 start, uint32 end )
                {
                    for ( uint32 chunkIndex = start; chunkIndex < end; ++chunkIndex )
                        refreshInstanceChunk( _pInstance, _pPrevious, _pRaw, _pSrcIndex, _rawCount, _pChunk[chunkIndex] );
                }
            };
            RefreshJob job{};
            job._pInstance = work.data();
            job._pPrevious = pPrevious->data();
            job._pRaw      = _listScratchRaw.data();
            job._pSrcIndex = _listInstanceSrcIndex.data();
            job._rawCount  = rawCount;
            job._pChunk    = _listRefreshChunk.data();

            // 청크 하나면 나눌 것이 없다 — 문턱 2.
            engine::runParallel( chunkCount, 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &RefreshJob::refreshRange, &job ) );

            // 합친다 — 실패 하나면 전체 실패, 구간은 경계가 맞닿으면 잇고 상한을 넘으면 전체 더티다.
            size_t totalRunCount = 0;
            for ( const InstanceRefreshChunk& chunk : _listRefreshChunk )
            {
                if ( chunk._bFailed != SW_FALSE )
                    return false;
                _snapshot._spinInstanceCount += chunk._spinCount;
                if ( chunk._bTooManyRun != SW_FALSE )
                    bTooManyRuns = true;
                totalRunCount += chunk._runCount;
            }
            if ( bTooManyRuns == false && totalRunCount > kMaxDirtyInstanceRun )
                bTooManyRuns = true;
            if ( bTooManyRuns == false )
            {
                for ( const InstanceRefreshChunk& chunk : _listRefreshChunk )
                {
                    for ( uint32 runIndex = 0; runIndex < chunk._runCount; ++runIndex )
                    {
                        const GpuInstanceRun& run = chunk._arrRun[runIndex];
                        if ( _snapshot._listDirtyInstanceRun.empty() == false &&
                             _snapshot._listDirtyInstanceRun.back()._start + _snapshot._listDirtyInstanceRun.back()._count == run._start )
                            _snapshot._listDirtyInstanceRun.back()._count += run._count;
                        else
                            _snapshot._listDirtyInstanceRun.push_back( run );
                    }
                }
            }
        }

        if ( bTransparentOrderChanged )
        {
            rebuildTransparentTail();
            // 꼬리는 통째로 새 값이다 — 접두부 구간 뒤에 한 구간으로 잇는다.
            const uint32 tailCount = static_cast<uint32>( work.size() ) - _opaqueInstanceCount;
            if ( bTooManyRuns == false && tailCount > 0 )
            {
                if ( _snapshot._listDirtyInstanceRun.empty() == false &&
                     _snapshot._listDirtyInstanceRun.back()._start + _snapshot._listDirtyInstanceRun.back()._count == _opaqueInstanceCount )
                    _snapshot._listDirtyInstanceRun.back()._count += tailCount;
                else if ( _snapshot._listDirtyInstanceRun.size() >= kMaxDirtyInstanceRun )
                    bTooManyRuns = true;
                else
                    _snapshot._listDirtyInstanceRun.push_back( GpuInstanceRun{ _opaqueInstanceCount, tailCount } );
            }
        }

        const size_t stepCount = bPartialRefresh ? _listDirtyPrimitive.size() : 0;
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
            // 트랜스폼과 바운드, 그리고 회전 시드뿐이다. 판정과 복사는 청크 갱신과 **같은 헬퍼**다.
            const GpuInstance& prev             = ( *pPrevious )[slot];
            GpuInstance&       inst             = work[slot];
            const GpuInstance& raw              = _listScratchRaw[srcIndex];
            const bool         bChanged         = GpuSceneBuilderInternal::isPayloadChanged( prev, raw );
            const uint32       previousSpinSeed = prev._spinSeed;
            inst                                = prev;
            GpuSceneBuilderInternal::copyPayload( raw, inst );
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
        // 도장은 원소에 찍는다 — 인스턴스 8000 개를 돌 것 없이 배치가 적어 둔 (배치, 원소) 쌍만 돈다.
        const size_t batchCount = MathUtil::min( _snapshot._listAllBatch.size(), _listBatchElementRange.size() );
        for ( size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
        {
            const uint32 groupIndex = _snapshot._listAllBatch[batchIndex]._materialGroup;
            if ( groupIndex >= _listMaterialGroupState.size() )
                continue;
            MaterialGroupState&   state = _listMaterialGroupState[groupIndex];
            const GpuInstanceRun& range = _listBatchElementRange[batchIndex];
            for ( uint32 entry = range._start; entry < range._start + range._count && entry < _listBatchElementIndex.size(); ++entry )
            {
                const uint32 elementIndex = _listBatchElementIndex[entry];
                if ( elementIndex < state._listEntryLastSeenBuild.size() )
                    state._listEntryLastSeenBuild[elementIndex] = _buildCounter;
            }
        }
        return true;
    }

    void GpuSceneBuilder::refreshInstanceChunk( GpuInstance* pInstance, const GpuInstance* pPrevious, const GpuInstance* pRaw, const uint32* pSrcIndex,
                                                uint32 rawCount, InstanceRefreshChunk& chunk )
    {
        uint32 lastDirtySlot = 0xFFFFFFFFu;
        for ( uint32 slot = chunk._start; slot < chunk._end; ++slot )
        {
            const uint32 srcIndex = pSrcIndex[slot];
            if ( srcIndex >= rawCount )
            {
                chunk._bFailed = SW_TRUE;
                return;
            }

            // 배치 구성이 같으므로 _meshBatchIndex 와 _materialIndex 는 그대로다 — 바뀐 것은
            // 트랜스폼과 바운드, 그리고 회전 시드뿐이다. 판정과 복사는 직렬 루프와 **같은 헬퍼**다.
            const GpuInstance& prev     = pPrevious[slot];
            GpuInstance&       inst     = pInstance[slot];
            const GpuInstance& raw      = pRaw[srcIndex];
            const bool         bChanged = GpuSceneBuilderInternal::isPayloadChanged( prev, raw );
            inst                        = prev;
            GpuSceneBuilderInternal::copyPayload( raw, inst );
            if ( inst._spinSeed != 0 )
                ++chunk._spinCount;

            if ( bChanged == false || chunk._bTooManyRun != SW_FALSE )
                continue;

            if ( lastDirtySlot + 1 == slot && chunk._runCount > 0 )
                ++chunk._arrRun[chunk._runCount - 1]._count;
            else if ( chunk._runCount >= kMaxDirtyInstanceRun )
                chunk._bTooManyRun = SW_TRUE;
            else
                chunk._arrRun[chunk._runCount++] = GpuInstanceRun{ slot, 1 };
            lastDirtySlot = slot;
        }
    }

    void GpuSceneBuilder::buildBatches()
    {
        // 전체 재구축이다 — 구간을 적어 봐야 전부이므로 받는 쪽이 통째로 올리게 한다.
        _snapshot._bAllInstancesDirty = SW_TRUE;
        _snapshot._listDirtyInstanceRun.clear();
        instanceWork().reserve( _listScratchCandidate.size() );
        _listInstanceSrcIndex.clear();
        _listInstanceSrcIndex.reserve( _listScratchCandidate.size() );
        // 후보 -> 인스턴스 슬롯 역매핑. 더티 후보의 인스턴스 자리를 바로 찾기 위한 것이다.
        _listCandidateToInstance.assign( _listScratchCandidate.size(), kInvalidCandidateIndex );
        _listCandidateMaterialElement.assign( _listScratchCandidate.size(), 0u );
        _bReuseMaterialElement       = SW_FALSE;
        _snapshot._spinInstanceCount = 0;
        _listBatchElementIndex.clear();
        _listBatchElementRange.clear();

        // **머티리얼 원소 인덱스는 프레임을 넘어 유지된다** (언리얼 GPUScene 의 영속 PrimitiveID 와 같은 자리).
        // 예전엔 여기서 그룹을 통째로 지우고 인스턴스마다 다시 부여했다 — 인스턴스 N 개와 머티리얼 M 종에
        // O(N·M) 이었고, 무엇보다 같은 머티리얼의 인덱스가 프레임마다 달라져 "바뀐 것만 올린다" 를 할 수 없었다.
        // 이제 처음 본 (머티리얼, 인스턴스) 쌍에만 자리를 주고, 안 쓰이면 아래 retireUnusedMaterialElements 가
        // 지연 회수한다. 자리를 옮기지 않으므로 인덱스는 안정적이다.
        ++_buildCounter;
        for ( MaterialGroupState& state : _listMaterialGroupState )
            state._bHasLast = SW_FALSE;

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchOpaqueEntry.size(); ++entryIndex )
            {
                if ( entryIndex == _listScratchOpaqueEntry.size() || ( _listScratchOpaqueEntry[entryIndex]._key == _listScratchOpaqueEntry[batchStart]._key ) == false )
                {
                    // 키의 포인터는 정체성이고 소유는 배치 머리 후보의 것을 빌린다. 합치기가 켜지면 키의 인스턴스는
                    // nullptr 이라 배치에는 인스턴스를 싣지 않는다 — 원소 표(_listEntry)가 인스턴스마다 소유를 든다.
                    // 텍스처 슬롯은 인스턴스가 아니라 부모 머티리얼(= 키의 대표)이 소유한다.
                    const SortKey&       key      = _listScratchOpaqueEntry[batchStart]._key;
                    const DrawCandidate& headCand = _listScratchCandidate[_listScratchOpaqueEntry[batchStart]._srcIdx];
                    emitBatch( _listScratchOpaqueIdx.data(), batchStart, entryIndex, RHIBlendMode::Opaque,
                               GpuSceneBuilderInternal::shareMaterial( key._pMaterial ), ( key._pInstance != nullptr ) ? headCand._instance : nullptr );
                    batchStart = entryIndex;
                }
            }
        }

        // 투명 꼬리를 자르는 자리를 적어 둔다 — 투명 순서만 바뀐 프레임은 여기서부터 다시 방출한다.
        _opaqueInstanceCount     = static_cast<uint32>( instanceWork().size() );
        _opaqueBatchCount        = static_cast<uint32>( _snapshot._listAllBatch.size() );
        _opaqueElementEntryCount = static_cast<uint32>( _listBatchElementIndex.size() );

        emitTransparentBatches();
        _listBuiltTransparentIdx = _listScratchTransparentIdx;
    }

    void GpuSceneBuilder::emitTransparentBatches()
    {
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
                    // 투명은 머리 후보의 머티리얼·인스턴스를 그대로 싣는다 — 정렬이 깊이순이라 합치기 대표를 쓰지 않는다.
                    emitBatch( _listScratchTransparentIdx.data(), batchStart, entryIndex, RHIBlendMode::Transparent, pBatchHead->_material,
                               pBatchHead->_instance );
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

    void GpuSceneBuilder::rebuildTransparentTail()
    {
        SW_PROFILE_SCOPE( "GT.GpuScene.build.refresh.transparentTail" );
        // 꼬리를 잘라 낸다. 용량은 남아 있으므로 다시 붙일 때 할당이 없다.
        instanceWork().resize( _opaqueInstanceCount );
        _listInstanceSrcIndex.resize( _opaqueInstanceCount );
        _snapshot._listAllBatch.resize( _opaqueBatchCount );
        _snapshot._listTransparentBatch.clear();
        _listBatchElementIndex.resize( _opaqueElementEntryCount );
        _listBatchElementRange.resize( _opaqueBatchCount );
        // 배치 키가 그대로인 프레임에만 오는 자리다 — 원소 인덱스는 지난 방출의 것을 그대로 쓴다(영속 ID).
        _bReuseMaterialElement = ( _listCandidateMaterialElement.size() == _listScratchCandidate.size() ) ? SW_TRUE : SW_FALSE;
        emitTransparentBatches();
        _bReuseMaterialElement   = SW_FALSE;
        _listBuiltTransparentIdx = _listScratchTransparentIdx;
    }

    void GpuSceneBuilder::emitBatch( const uint32* pSrcIdx, uint32 begin, uint32 end, RHIBlendMode blendMode, const shared_ptr<Material>& material,
                                     const shared_ptr<MaterialInstance>& instance )
    {
        const DrawCandidate& headCand = _listScratchCandidate[pSrcIdx[begin]];

        GpuMeshBatch batch{};
        batch._mesh               = headCand._mesh; // 소유는 배치 머리 후보의 것
        batch._vertexCount        = batch._mesh->getVertexCount();
        vector<GpuInstance>& work = instanceWork();
        batch._instanceBase       = static_cast<uint32>( work.size() );
        batch._instanceCount      = end - begin;
        batch._blendMode          = blendMode;
        batch._material           = material;
        batch._materialInstance   = instance;
        if ( instance != nullptr )
            batch._materialCb = instance->getDescriptorIndex();
        else
            batch._materialCb = ( material != nullptr ) ? material->getDescriptorIndex() : kInvalidDescriptorIndex;
        // 텍스처 슬롯은 인스턴스가 아니라 부모 머티리얼이 소유한다(인스턴스는 CB 값만 덮어쓴다).
        GpuSceneBuilderInternal::fillMaterialTextureSrvs( batch, material.get() );
        batch._materialGroup = materialGroupFor( material.get() );
        // 퍼뮤테이션은 **배치에 실제로 든 후보**에서 뽑는다. 합치기가 켜지면 키의 인스턴스는 nullptr 이라,
        // 키로 물으면 대표 머티리얼의 define 만 나오고 인스턴스가 켠 키워드가 통째로 빠진다 — 그 배치는
        // 잘못된 셰이더로 그려진다. 한 배치의 구성원은 전부 같은 퍼뮤테이션 해시를 가지므로 아무 구성원이나 맞다.
        batch._shaderPermutation = shaderPermutationFor( headCand._material.get(), headCand._instance.get() );
        batch._materialIndex     = 0;

        // 인스턴스마다 자기 (머티리얼, 인스턴스) 원소를 받는다 — 합치기가 켜져 있으면 한 배치에 여러 머티리얼이 산다.
        // 배치가 쓰는 원소는 중복 없이 적어 둔다(회수 도장이 인스턴스가 아니라 이 목록을 돈다). 정렬돼 있어 같은
        // 원소는 연속으로 오므로 직전 것과 다를 때만 더한다.
        const uint32 batchIndex = static_cast<uint32>( _snapshot._listAllBatch.size() );
        _listBatchElementRange.push_back( GpuInstanceRun{ static_cast<uint32>( _listBatchElementIndex.size() ), 0 } );
        for ( uint32 entryIndex = begin; entryIndex < end; ++entryIndex )
        {
            const uint32         srcIdx = pSrcIdx[entryIndex];
            const DrawCandidate& cand   = _listScratchCandidate[srcIdx];
            GpuInstance          inst   = _listScratchRaw[srcIdx];
            inst._meshBatchIndex        = batchIndex;
            if ( _bReuseMaterialElement != SW_FALSE && srcIdx < _listCandidateMaterialElement.size() )
                inst._materialIndex = _listCandidateMaterialElement[srcIdx];
            else
                inst._materialIndex = assignMaterialElement( cand._material, cand._instance, batch._materialGroup );
            if ( srcIdx < _listCandidateMaterialElement.size() )
                _listCandidateMaterialElement[srcIdx] = inst._materialIndex;
            GpuInstanceRun& elementRange = _listBatchElementRange.back();
            if ( elementRange._count == 0 || _listBatchElementIndex.back() != inst._materialIndex )
            {
                _listBatchElementIndex.push_back( inst._materialIndex );
                ++elementRange._count;
            }
            if ( entryIndex == begin )
                batch._materialIndex = inst._materialIndex;
            if ( inst._spinSeed != 0 )
                ++_snapshot._spinInstanceCount;
            if ( srcIdx < _listCandidateToInstance.size() )
                _listCandidateToInstance[srcIdx] = static_cast<uint32>( _listInstanceSrcIndex.size() );
            _listInstanceSrcIndex.push_back( srcIdx );
            work.push_back( inst );
        }

        // 두 목록이 같은 배치를 든다. 앞쪽은 복사해야 하지만 마지막 하나는 옮길 수 있다 —
        // 배치마다 shared_ptr 셋의 참조 카운트가 한 벌씩 줄어든다.
        vector<GpuMeshBatch>& listByBlend = ( blendMode == RHIBlendMode::Transparent ) ? _snapshot._listTransparentBatch : _snapshot._listOpaqueBatch;
        listByBlend.push_back( batch );
        _snapshot._listAllBatch.push_back( std::move( batch ) );
    }

    void GpuSceneBuilder::retireUnusedMaterialElements()
    {
        // 이번 빌드에서 안 쓰인 원소는 바로 지우지 않는다 — 아직 GPU 가 읽는 중인 프레임이 있을 수 있다.
        // 패킷 링 깊이(constant::kRenderFrameQueueDepth)와 같은 지연 기준을 쓴다 — 큐잉된 패킷이 아직 원소를 읽을 수 있다.
        if ( _buildCounter <= constant::kRenderFrameQueueDepth )
            return;
        const uint64 staleBefore = _buildCounter - constant::kRenderFrameQueueDepth;

        const size_t groupCount = MathUtil::min( _snapshot._listMaterialGroup.size(), _listMaterialGroupState.size() );
        for ( size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            GpuMaterialGroup&   group = _snapshot._listMaterialGroup[groupIndex];
            MaterialGroupState& state = _listMaterialGroupState[groupIndex];
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index]._material == nullptr )
                    continue;
                if ( index < state._listEntryLastSeenBuild.size() && state._listEntryLastSeenBuild[index] >= staleBefore )
                    continue;

                const GpuMaterialElementKey key{ group._listEntry[index]._material.get(), group._listEntry[index]._instance.get() };
                state._mapEntryToIndex.erase( key );
                // 자리는 비워 두고 프리리스트로 돌린다. 뒤 원소를 당겨오면 그들의 인덱스가 바뀌어
                // 이미 인스턴스에 적힌 materialIndex 가 엉뚱한 머티리얼을 가리킨다.
                group._listEntry[index] = GpuMaterialElement{};
                state._listFreeEntry.push_back( index );
                state._bHasLast = SW_FALSE;
            }
        }
    }

    void GpuSceneBuilder::resetMaterialRegistry()
    {
        _snapshot._listMaterialGroup.clear();
        _listMaterialGroupState.clear();
        _mapShaderPathToGroup.clear();
        _snapshot._pListShaderPermutation.reset();
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
        _listMaterialGroupState.emplace_back();
        const uint32 groupIndex = static_cast<uint32>( _snapshot._listMaterialGroup.size() - 1 );
        _mapShaderPathToGroup.emplace( shaderPath, groupIndex );
        return groupIndex;
    }

    uint32 GpuSceneBuilder::assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex )
    {
        Material* const         pMaterial = material.get();
        MaterialInstance* const pInstance = instance.get();
        if ( pMaterial == nullptr || groupIndex >= _snapshot._listMaterialGroup.size() || groupIndex >= _listMaterialGroupState.size() )
            return 0;
        GpuMaterialGroup&           group = _snapshot._listMaterialGroup[groupIndex];
        MaterialGroupState&         state = _listMaterialGroupState[groupIndex];
        const GpuMaterialElementKey key{ pMaterial, pInstance };
        // 배치 안의 인스턴스는 같은 원소를 연속으로 묻는다 — 포인터 비교 한 번으로 끝낸다.
        if ( state._bHasLast != SW_FALSE && state._lastKey == key )
            return state._lastIndex;
        const auto it = state._mapEntryToIndex.find( key );
        uint32     elementIndex{ 0 };
        if ( it != state._mapEntryToIndex.end() )
        {
            elementIndex = it->second;
        }
        else if ( state._listFreeEntry.empty() == false )
        {
            // 회수된 자리를 재사용한다 — 새 자리를 늘리면 버퍼가 단조 증가한다.
            elementIndex = state._listFreeEntry.back();
            state._listFreeEntry.pop_back();
            group._listEntry[elementIndex] = GpuMaterialElement{ material, instance };
            state._mapEntryToIndex.emplace( key, elementIndex );
        }
        else
        {
            group._listEntry.push_back( GpuMaterialElement{ material, instance } );
            state._listEntryLastSeenBuild.push_back( 0 );
            elementIndex = static_cast<uint32>( group._listEntry.size() - 1 );
            state._mapEntryToIndex.emplace( key, elementIndex );
        }
        state._listEntryLastSeenBuild[elementIndex] = _buildCounter;
        state._lastKey                              = key;
        state._lastIndex                            = elementIndex;
        state._bHasLast                             = SW_TRUE;
        return elementIndex;
    }
} // namespace sw
