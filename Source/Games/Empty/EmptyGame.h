/**
 * @file EmptyGame.h
 * @brief 최소 SWGame 템플릿 (키트 없음)
 */
#pragma once
#include "Core/Container/vector.h"

#include "GameFramework/Base/GameInstanceBase.h"

namespace sw
{
    class MeshComponent;
    class Scene;

    /** @brief 부트스트랩만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstanceBase
    {
    public:
        EmptyGame()           = default;
        ~EmptyGame() override = default;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override;
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;

    private:
        /**
         * @brief `-gv_benchMeshes=N` 이 주어지면 씬을 만들고 큐브 N 개를 격자로 채웁니다.
         * @details 이 앱에는 `createScene` 을 부르는 곳이 한 곳도 없었다. 그래서 렌더 경로에
         *          그릴 것이 한 번도 올라간 적이 없고, 성능은 코드를 읽어 추정할 수밖에 없었다
         *          — 드로우가 0 이면 드로우 비용도 0 이라 프로파일러를 붙여도 숫자가 안 나온다.
         *          씬을 만드는 건 엔진이 아니라 게임의 일이므로 여기에 둔다.
         */
        void spawnBenchScene( uint32 meshCount );

        /**
         * @brief 벤치 큐브를 매 프레임 움직입니다.
         * @details 정지한 씬은 "아무것도 안 바뀜" 빠른 경로만 재게 된다 — 그건 최선의 경우일 뿐
         *          실제 비용이 아니다. 위치·회전을 계속 바꿔야 더티 신호, 인스턴스 재업로드,
         *          배치 재구성 판정이 전부 실제로 돈다.
         */
        void updateBenchScene( float32 deltaTime );

        /** @brief 인덱스로부터 결정적인 밝은 색을 만듭니다. */
        static float4 makeBenchColor( uint32 index );

        /** @brief 격자 한 변의 절반 크기입니다. */
        static float32 halfExtentOf( uint32 side, float32 spacing );

        /** @brief 씬에 주광을 만들고 그림자 볼륨을 격자 크기에 맞춥니다. */
        void spawnBenchLight( Scene* pScene, float32 halfExtent );

        /** @brief 씬의 모든 카메라를 격자에 맞춥니다(에디터 뷰포트 카메라 포함). */
        void frameBenchCamera( Scene* pScene, uint32 side, float32 spacing );
        /** @brief 카메라 하나를 격자 전체가 들어오도록 물립니다. */
        void frameOneCamera( class CameraComponent* pCamera, uint32 side, float32 spacing );

        /** @brief 벤치 큐브. 씬이 이들을 소유하며, 벤치 실행 중에는 파괴되지 않습니다. */
        vector<MeshComponent*> _listBenchMesh;
        /** @brief 애니메이션 누적 시간. */
        float32 _benchElapsed{ 0.0f };
        /** @brief 격자 한 변의 큐브 수. 카메라를 다시 맞출 때 씁니다. */
        uint32 _benchGridSide{ 0 };
        /** @brief 늦게 생기는 에디터 카메라까지 한 번 더 맞췄으면 1. */
        uint8 _bRefreshedCameras{ 0 };
    };
} // namespace sw
