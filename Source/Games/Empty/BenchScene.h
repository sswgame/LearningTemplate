/**
 * @file BenchScene.h
 * @brief 메시 벤치 하네스 — `-gv_benchMeshes=N` 으로 켜지는 측정용 씬.
 *
 * @details **새 게임을 시작할 때 지울 파일이다.** 이 템플릿이 벤치를 들고 있는 이유는 하나다 —
 *          렌더 경로를 재려면 그릴 것을 씬에 올려야 하고, 씬을 만드는 것은 엔진이 아니라 게임의
 *          일이다. `Scripts/dev/BackendSmoke.py` 와 `Engine/Graphics/README.md` 의 측정 조건이
 *          이 플래그에 기대고 있으므로 타겟과 플래그 이름은 바꾸지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class CameraComponent;
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class MeshComponent;
    class Scene;

    /**
     * @class BenchScene
     * @brief 큐브 격자를 만들고 매 프레임 흔들어 렌더 경로를 실제로 태웁니다.
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
         * @details 다시 불러도 된다 — 상태 복원(모듈 리로드 · RHI 교체)이 씬을 갈아 끼운 뒤 EmptyGame 이
         *          다시 부른다. 벤치 오브젝트는 스냅샷 직전에 despawn 으로 걷히므로 복원 뒤 씬에 없다.
         * @return 실제로 만들었으면 true. 플래그가 없거나 0 이면 false.
         */
        bool spawnFromGlobals();

        /**
         * @brief 벤치가 만든 오브젝트(큐브 · 주광)를 씬에서 걷습니다. 멱등입니다.
         * @details 상태 스냅샷 직전에 부른다. 절차 생성물은 스냅샷에 실을 이유가 없고, 실리면 복원된 것은
         *          핸들과 다른 오브젝트인 데다 메시가 없는 유령이다. 스냅샷 규칙은 게임의 것이라
         *          (GameInstanceBase 훅) 엔진 API 를 건드리지 않는다.
         */
        void despawn();

        /** @brief 벤치 큐브를 움직입니다. 비어 있으면 아무것도 하지 않습니다. */
        void update( float32 deltaTime );

        /** @brief 벤치가 씬을 만들어 두었으면 true. */
        bool isActive() const { return _listBenchMesh.empty() == false; }

    private:
        /** @brief 씬을 확보하고 큐브 meshCount 개를 격자로 채웁니다. */
        void spawn( uint32 meshCount );

        /** @brief 인덱스로부터 결정적인 밝은 색을 만듭니다. */
        static float4 makeBenchColor( uint32 index );
        /** @brief 인덱스가 투명 큐브인지 (결정적 해시 — 실행마다 같은 그림이 나와야 비교가 된다). */
        static bool isBenchTransparent( uint32 index, uint32 percent );
        /** @brief 격자 한 변의 절반 크기입니다. */
        static float32 halfExtentOf( uint32 side, float32 spacing );

        /** @brief 씬에 주광을 만들고 그림자 볼륨을 격자 크기에 맞춥니다. */
        void spawnLight( Scene* pScene, float32 halfExtent );
        /**
         * @brief `-gv_benchLights=N` 개의 점광·스포트라이트를 격자 위에 흩뿌립니다.
         * @details 자리는 **결정적 해시**로 정한다 — 실행마다 같은 그림이 나와야 스크린샷 비교가 된다.
         *          홀수 번째는 스포트라이트다(점광만 두면 원뿔 감쇠 경로가 한 번도 안 돈다).
         */
        void spawnBenchLights( Scene* pScene, float32 halfExtent );
        /**
         * @brief `-gv_benchGround=1` 이면 격자 아래에 바닥 평면을 깝니다.
         * @details 그림자를 **받을 면**이다. 큐브만 떠 있으면 그림자가 어디에 지는지 그림으로 볼 수 없다.
         */
        void spawnGround( Scene* pScene, float32 halfExtent );
        /** @brief 씬의 모든 카메라를 격자에 맞춥니다(에디터 뷰포트 카메라 포함). */
        void frameCameras( Scene* pScene, uint32 side, float32 spacing );
        /** @brief 카메라 하나를 격자 전체가 들어오도록 물립니다. */
        void frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing );

        /**
         * @brief 머티리얼 스트레스 — 값·집합·퍼뮤테이션을 무작위로 흔듭니다.
         * @details `-gv_benchMaterialChurn*` 셋이 없으면 곧장 돌아온다. 자세한 사연은 그 스위치들의 주석.
         */
        void updateMaterialChurn( GameObjectManager* pObjects );
        /** @brief 인스턴스 하나의 값(색·러프니스)을 무작위로 바꿉니다. */
        void churnInstanceValue( MaterialInstance* pInstance );
        /** @brief 목록에서 인스턴스를 놓습니다(마지막 참조면 여기서 죽는다). 없으면 아무것도 안 합니다. */
        void releaseChurnInstance( const MaterialInstance* pInstance );
        /**
         * @brief 결정적 난수 한 걸음 (xorshift32).
         * @details 표준 난수를 쓰지 않는 이유: **같은 프레임 수를 돌리면 같은 순서가 나와야** 스크린샷과
         *          로그를 실행 간에 비교할 수 있다. 시드도 고정이다.
         */
        uint32 nextChurnRandom();

    private:
        /**
         * @brief 벤치 큐브의 핸들. 씬이 큐브를 소유하고, 여기에는 주소가 없다.
         * @details 생포인터를 들었을 때 RHI 교체가 여기서 죽었다 — 새 게임 인스턴스가 onInitialize 에서 큐브를
         *          만든 직후 상태 복원이 씬을 통째로 지웠고, 다음 update 가 죽은 주소에 setLocalPosition 을 했다.
         *          핸들은 해석이 nullptr 로 끝날 뿐 죽은 주소가 될 수 없다.
         */
        vector<ComponentHandle> _listBenchMesh;
        /** @brief 벤치가 만든 주광의 핸들. despawn 이 걷을 때 쓴다. */
        ComponentHandle _keyLight;
        /**
         * @brief 벤치가 만든 **큐브가 아닌** 것들의 핸들 — 흩뿌린 라이트와 바닥 평면.
         * @details 이것들이 아무 데도 안 적혀 있어서 `despawn()` 이 큐브와 주광만 걷었다.
         *          모듈 리로드 · RHI 교체는 `despawn()` → `spawnFromGlobals()` 를 한 쌍으로
         *          도는데, 그때마다 **라이트와 바닥이 한 벌씩 더 쌓였다.** 이 벤치가 존재하는
         *          이유가 바로 그 경로를 재는 것이라, 재려는 대상이 측정을 망가뜨리고 있었다.
         */
        vector<ComponentHandle> _listBenchExtra;
        /** @brief 반투명 큐브가 쓰는 머티리얼 에셋 (블렌드 모드·퍼뮤테이션이 불투명과 다르다). */
        shared_ptr<Material> _glassMaterial;
        /**
         * @brief 벤치가 만든 머티리얼 인스턴스 전부 — 스트레스가 흔들 대상.
         * @details 소유를 여기서도 든다. 떼어낸 인스턴스를 이 목록에서도 놓아야 참조가 **실제로**
         *          사라지고, 그래야 원소 회수와 상수버퍼 해제 경로가 돈다.
         */
        vector<shared_ptr<MaterialInstance>> _listChurnInstance;
        /** @brief 스트레스용 난수 상태. 0 이 되면 xorshift 가 멈추므로 고정 시드로 시작한다. */
        uint32 _churnRandom;
        /** @brief 스트레스가 돈 프레임 수 — 키워드 흔들기 주기에 쓴다. */
        uint64 _churnFrame;
        /** @brief 애니메이션 누적 시간. */
        float32 _benchElapsed;
        /** @brief 격자 한 변의 큐브 수. 카메라를 다시 맞출 때 씁니다. */
        uint32 _benchGridSide;
        /** @brief 늦게 생기는 에디터 카메라까지 한 번 더 맞췄으면 1. */
        uint8                  _bRefreshedCameras : 1;
        [[maybe_unused]] uint8 _reserved          : 7;
    };
} // namespace sw
