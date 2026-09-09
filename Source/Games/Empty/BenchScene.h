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
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class CameraComponent;
    class Material;
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
         * @return 실제로 만들었으면 true. 플래그가 없거나 0 이면 false.
         */
        bool spawnFromGlobals();

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
        /** @brief 씬의 모든 카메라를 격자에 맞춥니다(에디터 뷰포트 카메라 포함). */
        void frameCameras( Scene* pScene, uint32 side, float32 spacing );
        /** @brief 카메라 하나를 격자 전체가 들어오도록 물립니다. */
        void frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing );

    private:
        /** @brief 벤치 큐브. 씬이 이들을 소유하며, 벤치 실행 중에는 파괴되지 않습니다. */
        vector<MeshComponent*> _listBenchMesh;
        /** @brief 반투명 큐브가 쓰는 머티리얼 에셋 (블렌드 모드·퍼뮤테이션이 불투명과 다르다). */
        unique_ptr<Material> _glassMaterial;
        /** @brief 애니메이션 누적 시간. */
        float32 _benchElapsed;
        /** @brief 격자 한 변의 큐브 수. 카메라를 다시 맞출 때 씁니다. */
        uint32 _benchGridSide;
        /** @brief 늦게 생기는 에디터 카메라까지 한 번 더 맞췄으면 1. */
        uint8                  _bRefreshedCameras : 1;
        [[maybe_unused]] uint8 _reserved          : 7;
    };
} // namespace sw
