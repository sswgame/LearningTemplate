/**
 * @file FoliageInfluencerComponent.h
 * @brief 풀을 눕히는 구 — 캐릭터 · 탈것에 붙이면 지나가는 자리의 풀이 바깥으로 휩니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class FoliageInfluencerComponent
     * @brief 오너 위치의 구 하나입니다. 식생이 프레임마다 카메라에 가까운 것부터 `kMaxInfluencerCount` 개를 머티리얼에 싣습니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Foliage Influencer", Tooltip = "Sphere that bends foliage away (characters, vehicles)" )
    class SW_API FoliageInfluencerComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 한 프레임에 셰이더가 보는 구의 수입니다(머티리얼 칸 수와 같다). */
        static constexpr uint32 kMaxInfluencerCount = 4;

        FoliageInfluencerComponent();
        virtual ~FoliageInfluencerComponent() override = default;

        float32 getRadius() const { return _radius; }
        void    setRadius( float32 radius ) { _radius = radius; }

        /**
         * @brief @p manager 의 구 가운데 @p viewPosition 에 가까운 것부터 넷을 (중심 xyz, 반지름 w) 로 채웁니다. 빈 칸은 반지름 0 입니다.
         * @return 채운 수입니다.
         */
        static uint32 collectNearest( const GameObjectManager& manager, const float3& viewPosition, float4 ( &outArrSphere )[kMaxInfluencerCount] );

    private:
        PROPERTY( Category = "Foliage", DisplayName = "Radius", Min = 0.0, Meta = "Units=m" )
        float32 _radius;
    };
} // namespace sw
