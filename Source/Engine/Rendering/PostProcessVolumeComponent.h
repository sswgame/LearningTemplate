/**
 * @file PostProcessVolumeComponent.h
 * @brief 후처리 볼륨 — 카메라가 안에(또는 블렌드 반경 안에) 서면 덮어쓴 후처리 칸을 우선순위 순으로 카메라 값 위에 섞습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ComponentRegistry;

    /** @brief 후처리 볼륨의 모양입니다. 모양은 컴포넌트의 월드 위치가 가운데인 축 정렬입니다. */
    ENUM()
    enum class PostProcessVolumeShape : uint8
    {
        Box = 0, ///< 축 정렬 상자(반 크기)
        Sphere,  ///< 구(반지름)
        Unbound, ///< 어디서나(언리얼 bUnbound — 레벨 전체의 기본 룩)
    };

    /**
     * @class PostProcessVolumeComponent
     * @brief 후처리 값(`PostProcessValues`)과 칸마다의 덮어쓰기 표시(`PostProcessOverrides`)를 가진 볼륨입니다.
     * @details 언리얼 Post Process Volume(Priority · Blend Weight · Blend Radius · Unbound)과 같은 규칙이다. 카메라 위치의 무게는 안에서 1, 모양 밖으로
     *          `_blendRadius` 만큼 1 → 0, 그 밖 0 이고 `_blendWeight` 를 곱한다. 겹친 볼륨은 우선순위가 낮은 것부터 차례로 섞어 높은 것이 이긴다(같으면 등록 순서).
     *          바탕은 카메라 자신의 값이다(`resolveAt`, 렌더 뷰 수집 `RenderViewCollector` 가 뷰마다 부른다). 유니티 HDRP Volume(Global · Local, Priority, Blend Distance)도 같은 모양이다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Post Process Volume", Tooltip = "Overrides post-process settings for cameras inside (blended by priority and distance)", Meta = "Icon=Camera, IconColor=Light" )
    class SW_API PostProcessVolumeComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 값(상자 반 크기 5 m, 블렌드 반경 1 m, 무게 1, 우선순위 0, 덮어쓴 칸 없음)으로 만듭니다. */
        PostProcessVolumeComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~PostProcessVolumeComponent() override = default;

        /** @brief 레지스트리에 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 뺍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief @p position 에서의 무게(0..1)입니다 — 모양 안 1, 블렌드 반경 동안 1 → 0, 그 밖 0 에 블렌드 무게를 곱한다. */
        float32 computeWeight( const float3& position ) const;

        /** @brief 덮어쓰는 값입니다. */
        const PostProcessValues& getValues() const { return _values; }
        /** @brief 덮어쓰는 칸 표시입니다. */
        const PostProcessOverrides& getOverrides() const { return _overrides; }
        /** @brief 값과 덮어쓰는 칸을 정합니다. */
        void setSettings( const PostProcessValues& values, const PostProcessOverrides& overrides );
        /** @brief 우선순위입니다. 큰 것이 나중에 섞여 이긴다. */
        float32 getPriority() const { return _priority; }
        /** @brief 모양 · 크기 · 블렌드 반경 · 무게 · 우선순위를 정합니다. */
        void setShape( PostProcessVolumeShape shape, const float3& halfExtents, float32 radius, float32 blendRadius, float32 blendWeight, float32 priority );

        /**
         * @brief @p base(카메라 자신의 값) 위에 @p position 에 닿는 볼륨들을 우선순위 순으로 섞은 값입니다. 활성이 아니거나 무게가 0 인 볼륨은 빠진다.
         * @param registry 볼륨이 등록된 씬의 컴포넌트 레지스트리
         */
        static PostProcessValues resolveAt( const ComponentRegistry& registry, const float3& position, const PostProcessValues& base );

    private:
        PROPERTY( Category = "Post Process", DisplayName = "Values", Tooltip = "Post-process values this volume applies (only the fields switched on in Overrides)" )
        PostProcessValues _values;
        PROPERTY( Category = "Post Process", DisplayName = "Overrides", Tooltip = "Which value fields this volume overrides" )
        PostProcessOverrides _overrides;
        PROPERTY( Category = "Volume", DisplayName = "Half Extents", Tooltip = "Box half size", Units = m )
        float3 _halfExtents;
        PROPERTY( Category = "Volume", DisplayName = "Radius", Min = 0.0, Tooltip = "Sphere radius", Units = m )
        float32 _radius;
        PROPERTY( Category = "Volume", DisplayName = "Blend Radius", Min = 0.0, Tooltip = "Distance outside the shape over which the weight falls from 1 to 0", Units = m )
        float32 _blendRadius;
        PROPERTY( Category = "Volume", DisplayName = "Blend Weight", Min = 0.0, Max = 1.0, Tooltip = "Weight inside the shape; 0 turns the volume off" )
        float32 _blendWeight;
        PROPERTY( Category = "Volume", DisplayName = "Priority", Tooltip = "Overlapping volumes blend in rising priority; the highest wins" )
        float32 _priority;
        PROPERTY( Category = "Volume", DisplayName = "Shape", Tooltip = "Box, Sphere or Unbound (everywhere)" )
        PostProcessVolumeShape _shape;
    };
} // namespace sw
