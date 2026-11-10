/**
 * @file Engine/Visual/2D/Shadow/Scene2DOccluderProvider.h
 * @brief 2D 그림자 가림막의 렌더 기여입니다(씬 서비스) — 가림막 세그먼트를 씬 라이트 목록 맨 뒤에 붙입니다. 렌더러는 `ShadowCaster2DComponent` 를 모릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;

    /** @class Scene2DOccluderProvider @brief 씬의 `ShadowCaster2DComponent` 를 라이트 목록에 내는 씬 서비스입니다. */
    class Scene2DOccluderProvider final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "Scene2DOccluderProvider";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit Scene2DOccluderProvider( GameObjectManager& manager );

        void update( float32 deltaTime ) override { (void)deltaTime; }
        void onSceneCleared() override {}
        void contributeLights( SceneLightPhase phase, vector<GPULight>& inoutList ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw
