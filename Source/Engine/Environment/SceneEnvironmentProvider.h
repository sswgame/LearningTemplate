/**
 * @file Engine/Environment/SceneEnvironmentProvider.h
 * @brief 하늘 · 안개의 렌더 기여입니다(씬 서비스) — 렌더러는 값(`SkyGradient` · `FogSettings`)만 알고 컴포넌트를 모릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;

    /** @class SceneEnvironmentProvider @brief 씬의 하늘 · 안개 값을 렌더러에 내는 씬 서비스입니다. */
    class SceneEnvironmentProvider final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneEnvironmentProvider";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneEnvironmentProvider( GameObjectManager& manager );

        void               update( float32 deltaTime ) override { (void)deltaTime; }
        void               onSceneCleared() override {}
        [[nodiscard]] bool contributeSky( SkyGradient& outGradient ) const override;
        [[nodiscard]] bool contributeFog( const float3& viewPosition, FogSettings& outSettings ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw
