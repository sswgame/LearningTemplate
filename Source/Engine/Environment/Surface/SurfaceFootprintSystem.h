/**
 * @file SurfaceFootprintSystem.h
 * @brief 씬 하나의 발걸음 반응입니다. 발걸음을 표면 종류마다 세고, 발자국을 남기는 종류면 그 종류의 도장(`SurfaceKindDef::_footprint`)을 표면 변형 층에 찍습니다.
 * @details 발자국은 표면 변형 층(`SurfaceDeformSystem`)의 도장이다 — 높이장 · 충돌 · 내비를 고치지 않고 지면 머티리얼이 꺼져 보인다. 남는 시간은 도장의
 *          메워지는 속도가 정하고(눈 느리게 · 모래 빠르게), 겹쳐 찍어도 격자 하나라 수가 늘지 않는다. 표면마다 오브젝트를 두던 링은 없앴다(눈 전용이 아니다).
 *          씬 서비스(`ISceneService`)이고 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::SurfaceFootprintSystem>()`).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    struct SurfaceKindDef;

    class GameObjectManager;

    /**
     * @class SurfaceFootprintSystem
     * @brief 씬마다 하나 있는 발걸음 반응입니다(파일 머리말 참고).
     */
    class SW_API SurfaceFootprintSystem final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SurfaceFootprintSystem";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SurfaceFootprintSystem( GameObjectManager& manager );

        SurfaceFootprintSystem( const SurfaceFootprintSystem& )            = delete;
        SurfaceFootprintSystem& operator=( const SurfaceFootprintSystem& ) = delete;

        /** @brief 할 일이 없다 — 자국의 나이는 변형 층이 메운다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬을 비울 때 센 수를 버립니다. */
        void onSceneCleared() override;

        /** @brief 발걸음 하나를 알립니다. 종류마다 센 수를 올리고, 종류가 발자국을 남기면 @p point 에 요 @p yaw(라디안) 방향으로 그 도장을 찍습니다. */
        void addFootstep( const SurfaceKindDef& kind, const float3& point, float32 yaw );

        /** @brief 이 씬에서 찍은 발자국 수입니다(누적). */
        uint32 getPlacedCount() const { return _placedCount; }
        /** @brief 종류 @p kind 를 밟은 발걸음 수입니다(누적). */
        uint32 getFootstepCount( const hashed_string& kind ) const;
        /** @brief 마지막 발걸음의 종류입니다(없으면 비어 있다). */
        const hashed_string& getLastKind() const { return _lastKind; }

    private:
        GameObjectManager*                   _pManager;
        unordered_map<hashed_string, uint32> _mapFootstepCount;
        hashed_string                        _lastKind;
        uint32                               _placedCount;
    };
} // namespace sw
