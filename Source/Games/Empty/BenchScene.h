/**
 * @file BenchScene.h
 * @brief 메시 벤치 하네스입니다. `-gv_benchMeshes=N` 으로 켜지는 측정용 씬입니다.
 * @details **새 게임을 시작할 때 지울 파일이다.** 이 템플릿이 벤치를 가지고 있는 이유는 하나다.
 *          렌더 경로를 측정하려면 그릴 오브젝트를 씬에 올려야 하고, 씬을 만드는 일은 엔진이 아니라 게임의
 *          일이다. `Scripts/dev/RunBackendSmoke.py` 와 `docs/08_Verification.md` 의 측정 조건이
 *          이 플래그에 기대고 있으므로 타깃과 플래그 이름은 바꾸지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"

#include "Games/Empty/BenchFraming.h"
#include "Games/Empty/BenchSceneSettings.h"

namespace sw
{
    class CameraComponent;
    class DirectionalLightComponent;
    class GameObject;
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;
    class MeshInstanceBatch;
    class RagdollComponent;
    class Scene;
    class SkeletalMeshComponent;
    class Skeleton;
    class SocketSet;

    /**
     * @class BenchScene
     * @brief 큐브 격자를 만들고 매 프레임 흔들어 렌더 경로를 실제로 실행합니다.
     */
    class BenchScene
    {
    public:
        BenchScene();
        ~BenchScene();

        BenchScene( const BenchScene& )            = delete;
        BenchScene& operator=( const BenchScene& ) = delete;

        /**
         * @brief `-gv_benchMeshes=N` 이 주어졌으면 벤치 씬을 만듭니다.
         * @details 배치 값과 튜닝 값은 `benchscene.xml`(`BenchSceneSettings`)에서 읽습니다. 읽지 못하면 오류를 남기고 아무것도 만들지 않습니다.
         *          여러 번 불러도 됩니다. 상태 복원(모듈 리로드, RHI 교체)이 씬을 교체한 뒤 EmptyGame 이
         *          다시 부릅니다. 벤치 오브젝트는 스냅샷을 만들기 직전에 despawn 으로 정리하므로, 복원한 뒤에는 씬에 없습니다.
         * @return 실제로 만들었으면 true 를 반환합니다. 플래그가 없거나 0 이면 false 입니다.
         */
        [[nodiscard]] bool spawnFromGlobals();

        /**
         * @brief 벤치가 만든 오브젝트(큐브, 주 광원)를 씬에서 정리합니다. 멱등입니다.
         * @details 상태 스냅샷 직전에 부른다. 절차 생성물은 스냅샷에 실을 이유가 없고, 실리면 복원된 것은
         *          핸들과 다른 오브젝트인 데다 메시가 없는 유령이다. 스냅샷 규칙은 게임의 것이라
         *          (GameInstanceBase 훅) 엔진 API 를 건드리지 않는다.
         */
        void despawn();

        /** @brief 벤치 큐브를 움직입니다. 비어 있으면 아무것도 하지 않습니다. */
        void update( float32 deltaTime );

        /** @brief 벤치가 씬을 만들어 두었으면 true 를 반환합니다. */
        bool isActive() const
        {
            return _listBenchMesh.empty() == false || _listInstanceBatch.empty() == false || _framingMode == FramingMode::Character ||
                   _framingMode == FramingMode::Combat || _framingMode == FramingMode::Face || _listRigBody.empty() == false;
        }

    private:
        /** @brief 씬을 확보하고 큐브 meshCount 개를 격자로 채웁니다. */
        void spawn( uint32 meshCount );
        /** @brief 활성 씬을 반환합니다. 없으면 빈 씬을 만들고 기본 카메라를 보장합니다. 만들지 못하면 오류를 남기고 nullptr 입니다. */
        static Scene* ensureBenchScene();
        /** @brief 메시 종류(도형을 섞은 같은 기하의 사본) @p variantCount 개를 `_listMeshVariant` 에 만듭니다. 도형을 만들지 못하면 false 입니다. */
        [[nodiscard]] bool createMeshVariants( uint32 variantCount, uint32 shapeCount );
        /** @brief 투명 큐브가 돌려 쓸 머티리얼 인스턴스 3 개를 만듭니다(유리 머티리얼을 읽지 못하면 비어 있는 채로 둡니다). */
        void createTransparentMaterials( vector<shared_ptr<MaterialInstance>>& outList );
        /** @brief `-gv_benchInstanced=1`: 씬 컴포넌트 없이 메시 종류마다 인스턴스 배치 하나로 큐브 격자를 채웁니다. */
        void spawnInstancedGrid( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 meshCount, uint32 meshVariantCount, uint32 side, float32 origin );
        /** @brief 큐브마다 오브젝트를 만들어 격자를 채웁니다. 큐브별 머티리얼과 투명 비율은 이 경로에만 있습니다. */
        void spawnCubeGrid( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 meshCount, uint32 side, float32 origin, uint32 transparentPercent,
                            const vector<shared_ptr<MaterialInstance>>& listTransparentMaterial );
        /** @brief 지금 만든 벤치(얼굴, 리그, 래그돌, 캐릭터, 격자)에 맞춰 활성 씬의 카메라를 다시 맞춥니다. */
        void refreshFraming();
        /**
         * @brief @p requested 가 지금 모드보다 높으면 모드를 올리고, 모드에 맞는 맞춤 함수를 불러 @p pScene 의 카메라를 맞추고 그때의 입력을 `_framedInput` 에 적습니다.
         * @details 모든 모드의 맞춤은 이 한 곳을 지납니다. 스폰은 자기 모드를 @p requested 로 넘깁니다.
         */
        void applyFraming( Scene* pScene, FramingMode requested );
        /** @brief 지금의 보기 모드, 그 모드의 맞춤이 읽는 입력 값(`-gv_bench…`), @p pScene 의 카메라 수를 모읍니다. 카메라 수를 담아 맞춘 뒤에 생긴 카메라를 놓치지 않습니다. */
        FramingInput captureFramingInput( const Scene* pScene ) const;
        /** @brief 맞춤이 옮기는 카메라인지 반환합니다. 캡처 카메라(`spawnBenchViews`)는 아닙니다. */
        static bool isFramedCamera( const CameraComponent* pCamera );
        /** @brief @p pScene 에서 맞춤이 옮기는 카메라의 수입니다. */
        static uint32 countFramedCameras( const Scene* pScene );
        /** @brief 격자 둘레의 광원, 바닥, 카메라, 뷰를 만듭니다. 두 격자 경로가 함께 씁니다. */
        void finishGrid( Scene* pScene, uint32 side );
        /** @brief 리그 데모의 기울어진 바닥을 만듭니다. 물리용 상자와, 같은 위치와 크기의 큐브로 이루어집니다. */
        void spawnRigSlope( Scene* pScene, GameObjectManager& objects, float32 halfWidth );
        /** @brief 리그 데모의 시선 목표(작은 구)를 만듭니다. */
        void spawnRigLookTarget( Scene* pScene, GameObjectManager& objects );
        /** @brief 리그 데모의 기사 한 명을 스폰합니다. 몸, 무기, 망토 유닛과 리그를 붙입니다. */
        void spawnRigKnight( GameObjectManager& objects, uint32 index, float32 origin, bool bRig, const shared_ptr<SocketSet>& weaponSockets );
        /**
         * @brief 본 하나("root")짜리 데이터 스켈레톤을 만듭니다. 리그(`PoseModifierComponent`)는 암묵 스켈레톤을 받은 유닛에 묶이지 않으므로(`SkeletalMeshComponent::hasDataSkeleton`),
         *        스킨 없는 무기 유닛은 이것을 `setSkeleton` 으로 받아야 리그가 루트 본을 움직인다.
         */
        static shared_ptr<const Skeleton> createSingleBoneSkeleton();
        /** @brief 기사에게 붙는 무기 유닛을 만들고 그 유닛 컴포넌트를 반환합니다(만들지 못하면 nullptr). 리그가 오른손 소켓 위치를 따라갑니다. */
        SkeletalMeshComponent* spawnRigWeapon( GameObjectManager& objects, GameObject* pKnight, SkeletalMeshComponent* pBody, bool bRig );
        /** @brief 기사에게 붙는 망토 유닛을 만듭니다. 루트 뼈는 가슴 위치를 따라가고, 나머지 사슬 뼈는 스프링으로 움직입니다. */
        void spawnRigCape( GameObjectManager& objects, GameObject* pKnight, SkeletalMeshComponent* pBody, bool bRig );
        /** @brief `-gv_benchCharacters=N`: 스킨드 캐릭터 N 명을 한 줄로 스폰하고 Idle 과 Walking_A 를 번갈아 재생합니다. */
        void spawnCharacters( uint32 characterCount );
        /**
         * @brief `-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`: 캐릭터 @p pBody 에 리더 포즈로 따르는 부품 유닛과 리그가 붙은 무기 유닛을 만듭니다(애니메이션 의존 레벨 · 성분 측정).
         * @details 부품은 메시 없이 몸과 같은 스켈레톤만 가져 의존 순서의 비용만 잰다. 무기는 가장 깊은 부품(없으면 몸)을 리그 대상으로 삼아 한 레벨 더 깊다.
         */
        void spawnCharacterParts( GameObjectManager& objects, uint32 characterIndex, SkeletalMeshComponent* pBody );
        /** @brief `-gv_benchCombat=1`: 전투 연출(KayKit 스켈레톤 적, 칼, 강체 바닥, 연출 컴포넌트)을 스폰합니다. */
        void spawnCombat();
        /** @brief `-gv_benchRagdolls=N`: 래그돌을 가진 스켈레톤 N 명을 격자로 세웁니다. */
        void spawnRagdolls( uint32 ragdollCount );
        /** @brief 군중의 일부를 프레임마다 래그돌 ↔ 기상으로 바꿉니다(`-gv_benchRagdollPeriod`). */
        void updateRagdollChurn( GameObjectManager* pObjects );
        /** @brief 캐릭터 줄이 화면에 가득 차도록 씬의 카메라를 맞춥니다. 허리 높이를 바라봅니다. 격자로 배치했으면 앞 위쪽에서 내려다봅니다. */
        void frameCharacterCameras( Scene* pScene, float32 halfExtent );
        /** @brief 캐릭터 줄(격자면 한 행)의 반폭입니다. */
        float32 computeCharacterHalfExtent( uint32 characterCount ) const;
        /** @brief `-gv_benchFaces=N`: 테스트 머리 N 개를 한 줄로 스폰합니다(표정, 립싱크, 깜빡임, 시선). */
        void spawnFaces( uint32 faceCount );
        /** @brief 머리 줄을 정면에서 보도록 카메라를 맞춥니다. */
        void frameFaceCameras( Scene* pScene );
        /** @brief 머리 줄이 화면 폭에 차는 카메라 거리(m)입니다. */
        float32 computeFaceCameraDistance( uint32 faceCount ) const;
        /**
         * @brief `-gv_benchRig=N`: 후처리 리그를 적용한 KayKit 기사 N 명을 경사면 위에 스폰합니다(BenchSceneRig.cpp). 발 디딤, 머리 시선,
         *        무기 손잡이 왼손 IK, 망토 스프링 본을 확인합니다. 무기와 망토는 각자 유닛(자식 오브젝트)이고 리그가 몸의 본을 따릅니다.
         */
        void spawnRigCharacters( uint32 characterCount );
        /** @brief 리그 데모의 프레임 갱신입니다. 시선 목표를 돌리고, 몸을 좌우로 돌려 망토가 흔들리게 하며, 거리 LOD 기준점(카메라)을 넘깁니다. */
        void updateRigCharacters( float32 deltaTime );
        /** @brief 리그 데모 카메라를 맞춥니다. 기사 한 명이면 상반신 가까이, 여럿이면 줄 전체를 봅니다. */
        void frameRigCameras( Scene* pScene );
        /** @brief `-gv_benchRig` 값입니다(스위치는 BenchSceneRig.cpp 가 선언합니다). */
        static uint32 getRigCharacterCount();

        /** @brief 인덱스로부터 결정적인 밝은 색을 만듭니다. */
        static float4 makeBenchColor( uint32 index );
        /** @brief 인덱스가 투명 큐브인지 반환합니다(결정적 해시이므로 실행마다 같은 이미지가 나와 비교할 수 있습니다). */
        static bool isBenchTransparent( uint32 index, uint32 percent );
        /** @brief 격자 한 변의 절반 크기입니다. */
        static float32 halfExtentOf( uint32 side, float32 spacing );

        /** @brief 씬에 주 광원을 만들고 그림자 볼륨을 격자 크기에 맞춥니다. */
        void spawnLight( Scene* pScene, float32 halfExtent );
        /**
         * @brief 큐브 하나를 격자 위치 index 에 만듭니다. 메시 종류, 위치, 시드, 무버까지 설정하며, 만든 메시 컴포넌트를 반환합니다.
         * @details 첫 스폰과 `-gv_benchSpawnChurn` 의 재스폰이 같은 함수를 쓴다. 투명 큐브와 큐브별 머티리얼은 첫 스폰에서만 적용한다.
         */
        MeshComponent* spawnCube( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 index, uint32 side, float32 origin );
        /** @brief `-gv_benchSpawnChurn=N`: 프레임마다 큐브 N 개를 지우고 같은 위치에 새로 만듭니다. */
        void updateSpawnChurn( GameObjectManager* pObjects, Scene* pScene );
        /**
         * @brief `-gv_benchUiNameplates=K`: 큐브 @p index 가 앞쪽 K 개 안에 들면 RTS 이름(이름 한 줄과 HP 막대)를 붙이고 true 를 반환합니다(BenchSceneProbes.cpp).
         */
        [[nodiscard]] bool attachNameplate( GameObject& object, uint32 index ) const;
        /**
         * @brief 벤치 프레임을 세고, `-gv_benchProbeFrame` 프레임에 한 번 일어나는 탐침을 시작합니다. 탐침은 `-gv_benchUiHangulGlyphs` 의 새 한글 글리프와 `-gv_benchSceneLoadPath` 의 비동기 씬 로드입니다.
         */
        void updateProbes( GameObjectManager* pObjects );
        /**
         * @brief `-gv_benchLights=N` 개의 점광과 스포트라이트를 격자 위에 흩뿌립니다.
         * @details 위치는 **결정적 해시**로 정합니다. 실행마다 같은 이미지가 나와야 스크린샷을 비교할 수 있기 때문입니다.
         *          홀수 번째 광원은 스포트라이트입니다. 점광만 두면 원뿔 감쇠 경로가 한 번도 실행되지 않습니다.
         */
        void spawnBenchLights( Scene* pScene, float32 halfExtent );
        /**
         * @brief `-gv_benchGround=1` 이면 격자 아래에 바닥 평면을 깝니다.
         * @details 그림자를 **받는 면**입니다. 큐브만 떠 있으면 그림자가 어디에 생기는지 이미지로 확인할 수 없습니다.
         */
        void spawnGround( Scene* pScene, float32 halfExtent );
        /** @brief `-gv_benchViews=N` 이면 격자 둘레에 캡처 카메라(렌더 텍스처) N 개를 둡니다. */
        void spawnBenchViews( Scene* pScene, float32 halfExtent );
        /** @brief 씬의 모든 카메라를 격자에 맞춥니다(에디터 뷰포트 카메라 포함). */
        void frameCameras( Scene* pScene, uint32 side, float32 spacing );
        /** @brief 카메라 하나를 격자 전체가 들어오도록 뒤로 뺍니다. */
        void frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing );

        /**
         * @brief 머티리얼 스트레스입니다. 값, 집합, 퍼뮤테이션을 무작위로 바꿉니다.
         * @details `-gv_benchMaterialChurn*` 스위치 세 개가 모두 꺼져 있으면 바로 반환합니다. 자세한 이유는 그 스위치들의 주석에 있습니다.
         */
        void updateMaterialChurn( GameObjectManager* pObjects );
        /** @brief 인스턴스 하나의 값(색, 러프니스)을 무작위로 바꿉니다. */
        void churnInstanceValue( MaterialInstance* pInstance );
        /** @brief 목록에서 인스턴스를 해제합니다(마지막 참조면 여기서 파괴됩니다). 없으면 아무것도 하지 않습니다. */
        void releaseChurnInstance( const MaterialInstance* pInstance );
        /**
         * @brief 결정적 난수를 한 단계 진행합니다(xorshift32).
         * @details 표준 난수를 쓰지 않는 이유는, **같은 프레임 수만큼 실행하면 같은 순서가 나와야** 스크린샷과
         *          로그를 실행 사이에 비교할 수 있기 때문입니다. 시드도 고정입니다.
         */
        uint32 nextChurnRandom();

    private:
        /** @brief `benchscene.xml` 에서 읽은 배치 값과 튜닝 값입니다. `spawnFromGlobals` 가 채웁니다. */
        BenchSceneSettings _settings;
        /**
         * @brief 벤치 큐브의 핸들입니다. 씬이 큐브를 소유하므로 여기에는 주소가 없습니다.
         * @details 주의: 생 포인터를 보관하면 RHI 를 교체할 때 크래시가 납니다. 새 게임 인스턴스가 onInitialize 에서 큐브를
         *          만든 직후에 상태 복원이 씬을 통째로 지우고, 다음 update 가 해제된 주소에 setLocalPosition 을 호출하기 때문입니다.
         *          핸들은 조회 결과가 nullptr 이 될 뿐, 해제된 주소를 가리킬 수 없습니다.
         */
        vector<TComponentHandle<MeshComponent>> _listBenchMesh;
        /**
         * @brief `-gv_benchInstanced=1` 의 큐브입니다. 메시 종류마다 배치 하나이며, 큐브 i 는 배치 i % 종류 수의 항목 i / 종류 수입니다.
         * @details 씬 컴포넌트가 없으므로 `_listBenchMesh` 는 비어 있고, update 가 월드 행렬을 항목에 바로 쓴다.
         */
        vector<shared_ptr<MeshInstanceBatch>> _listInstanceBatch;
        uint32                                _instanceCubeCount;
        /** @brief 벤치가 섞어 쓰는 메시 종류입니다. 큐브 i 는 i % 종류 수를 쓰며, 재스폰이 같은 종류를 다시 씁니다. */
        vector<shared_ptr<Mesh>> _listMeshVariant;
        /** @brief 재스폰이 다음에 교체할 큐브 위치입니다(라운드 로빈). */
        uint32 _churnCursor;
        /// @brief 프레임마다 다시 채우는 배치 쓰기 목록입니다. 큐브마다 세터를 부르지 않습니다(update 주석 참고).
        vector<SceneTransformWrite> _listTransformWrite;
        /** @brief 벤치가 만든 주 광원의 핸들입니다. despawn 이 정리할 때 씁니다. */
        TComponentHandle<DirectionalLightComponent> _keyLight;
        /**
         * @brief 벤치가 만든 **큐브가 아닌** 것들의 핸들입니다. 흩뿌린 라이트와 바닥 평면입니다.
         * @details 모듈 리로드와 RHI 교체는 `despawn()` 과 `spawnFromGlobals()` 를 한 쌍으로
         *          돈다. 주의: 벤치가 만든 것을 여기 적지 않으면 `despawn()` 이 정리하지 못해 그때마다
         *          **한 벌씩 더 쌓이고**, 측정하려는 경로가 측정을 망가뜨린다.
         */
        vector<ComponentHandle> _listBenchExtra;
        /** @brief 반투명 큐브가 쓰는 머티리얼 에셋입니다(블렌드 모드와 퍼뮤테이션이 불투명과 다릅니다). */
        shared_ptr<Material> _glassMaterial;
        /**
         * @brief 벤치가 만든 머티리얼 인스턴스 전부입니다. 스트레스가 흔들 대상입니다.
         * @details 소유를 여기서도 가진다. 제거한 인스턴스를 이 목록에서도 해제해야 참조가 **실제로**
         *          사라지고, 그래야 요소 회수와 상수 버퍼 해제 경로가 돈다.
         */
        vector<shared_ptr<MaterialInstance>> _listChurnInstance;
        /** @brief 스트레스용 난수 상태입니다. 0 이 되면 xorshift 가 멈추므로 고정 시드로 시작합니다. */
        uint32 _churnRandom;
        /** @brief 스트레스가 돈 프레임 수입니다. 키워드를 바꾸는 주기에 씁니다. */
        uint64 _churnFrame;
        /** @brief 애니메이션 누적 시간입니다. */
        float32 _benchElapsed;
        /** @brief 격자 한 변의 큐브 수. 카메라를 다시 맞출 때 씁니다. */
        uint32 _benchGridSide;
        /** @brief `updateProbes` 가 센 벤치 프레임 수입니다(탐침을 거는 프레임을 고릅니다). */
        uint32 _probeFrame;
        /** @brief 늦게 생기는 에디터 카메라까지 한 번 더 맞췄으면 1. */
        uint8 _bRefreshedCameras : 1;
        /** @brief 리그 데모의 기사 몸 유닛 핸들입니다. 몸 돌리기와 걷기에 씁니다. */
        vector<TComponentHandle<SkeletalMeshComponent>> _listRigBody;
        /** @brief 래그돌 군중의 래그돌 컴포넌트 핸들입니다. */
        vector<TComponentHandle<RagdollComponent>> _listRagdoll;
        /** @brief 래그돌 군중 격자의 반폭입니다(카메라를 다시 맞출 때 씁니다). */
        float32 _ragdollHalfExtent;
        /** @brief 래그돌 군중이 센 프레임 수입니다. */
        uint32 _ragdollFrame;
        /** @brief 리그 데모의 시선 목표(작은 구). */
        TComponentHandle<MeshComponent> _rigLookTarget;
        /** @brief 마지막으로 카메라를 맞출 때의 입력입니다. `captureFramingInput()` 이 이와 달라지면(시나리오가 `-gv_bench…` 를 도중에 바꾼 경우) `update` 가 다시 맞춥니다. */
        FramingInput _framedInput;
        /** @brief 지금의 보기 모드입니다. 스폰이 정하고 `despawn` 이 격자로 되돌립니다. */
        FramingMode            _framingMode;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
