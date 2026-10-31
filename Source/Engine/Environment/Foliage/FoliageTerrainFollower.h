/**
 * @file FoliageTerrainFollower.h
 * @brief 씬 하나의 폴리지가 아래 지형의 편집을 따라가게 합니다(씬 서비스). 컴포넌트 틱이 아니라 씬 프레임 단계라서 편집 모드(플레이 전)에서도 돈다.
 * @details 예전에는 `FoliageComponent::onTick` 이 지형 세대를 살폈다 - 편집 모드는 컴포넌트 틱이 돌지 않아 지형을 칠해도 폴리지가 낡은 채 남았고,
 *          시나리오 골든이 그 낡은 그림을 정답으로 담았다. 언리얼의 폴리지가 랜드스케이프 편집을 에디터에서 바로 따라가는 것과 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw
{
    /** @class FoliageTerrainFollower @brief 폴리지마다 지형 편집이 가라앉았는지 보고 다시 계산을 부르는 씬 서비스입니다. */
    class FoliageTerrainFollower final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "FoliageTerrainFollower";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit FoliageTerrainFollower( GameObjectManager& manager );
        ~FoliageTerrainFollower() override                                 = default;
        FoliageTerrainFollower( const FoliageTerrainFollower& )            = delete;
        FoliageTerrainFollower& operator=( const FoliageTerrainFollower& ) = delete;

        void update( float32 deltaTime ) override;
        void onSceneCleared() override {}

    private:
        GameObjectManager& _manager;
    };
} // namespace sw
