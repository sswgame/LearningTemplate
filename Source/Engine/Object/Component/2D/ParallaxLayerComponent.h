/**
 * @file ParallaxLayerComponent.h
 * @brief 카메라를 따라 늦게(또는 빠르게) 움직이고 되풀이 길이로 감기는 2D 시차 레이어입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class ParallaxLayerComponent
     * @brief 오브젝트(와 그 자식들)를 게임 카메라에 대해 `_scrollFactor` 배로 움직이고, `_repeatSize` 마다 감아 끝없이 이어지게 합니다.
     * @details Godot `ParallaxLayer`(motion_scale · motion_mirroring) 의 자리입니다. 유니티는 기본 부품이 없고 스크립트로 같은 식을 씁니다.
     *
     *          **식.** 카메라가 기준점 `_referencePoint` 에 있을 때 레이어는 저작한 자리(시작할 때의 로컬 위치)에 있습니다. 카메라가 C 로 가면
     *          레이어의 X · Y 는 `원래 자리 + (C − 기준) × (1 − 배율)` 입니다 — 배율 1 은 월드와 같이(시차 없음), 0 은 카메라에 붙어(하늘),
     *          0.5 는 절반 속도로 흐릅니다. 되풀이 길이 M 이 0 이 아닌 축은 카메라에 대한 상대 위치를 M 으로 감습니다(`computeLayerPosition`):
     *          레이어가 카메라에서 M/2 넘게 벗어나지 않으므로 **내용이 M 마다 되풀이되고 화면 폭 + M 이상을 덮으면** 끝없이 이어져 보입니다
     *          (타일 스프라이트 `SpriteDrawMode::Tiled` 를 폭 3M 으로 두는 것이 가장 쉽습니다). Z 는 건드리지 않습니다.
     *
     *          틱은 마지막 그룹(`TickGroup::PostUpdate`)입니다 — 카메라를 움직이는 컴포넌트는 그 앞 그룹에서 돌아야 같은 프레임의 카메라를 봅니다.
     *          플레이가 끝나면 저작한 자리로 되돌립니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Parallax Layer", Tooltip = "Moves this object relative to the game camera and wraps it" )
    class SW_API ParallaxLayerComponent : public Component
    {
    public:
        REFLECT_BODY();
        ParallaxLayerComponent();
        virtual ~ParallaxLayerComponent() override = default;

        /** @brief 저작한 자리를 기억하고 마지막 틱 그룹에 듭니다. */
        void onBeginPlay() override;
        /** @brief 저작한 자리로 되돌립니다. */
        void onEndPlay() override;
        /** @brief 게임 카메라를 따라 자리를 맞춥니다. */
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 레이어의 X · Y 를 구합니다(순수 함수 — 시험이 직접 부릅니다).
         * @param origin      저작한 자리(카메라가 기준점에 있을 때의 자리)
         * @param camera      카메라의 X · Y
         * @param reference   기준점
         * @param scroll      배율(축마다). 1 = 월드와 같이, 0 = 카메라에 붙음
         * @param repeatSize  되풀이 길이(축마다). 0 이하인 축은 감지 않습니다
         */
        static float2 computeLayerPosition( const float2& origin, const float2& camera, const float2& reference, const float2& scroll, const float2& repeatSize );

        /** @brief 배율입니다(축마다). */
        const float2& getScrollFactor() const { return _scrollFactor; }
        void          setScrollFactor( const float2& scroll ) { _scrollFactor = scroll; }
        /** @brief 되풀이 길이입니다(축마다, 0 = 감지 않음). */
        const float2& getRepeatSize() const { return _repeatSize; }
        void          setRepeatSize( const float2& repeatSize ) { _repeatSize = repeatSize; }
        /** @brief 카메라가 이 자리에 있을 때 레이어가 저작한 자리에 있습니다. */
        const float2& getReferencePoint() const { return _referencePoint; }
        void          setReferencePoint( const float2& reference ) { _referencePoint = reference; }

        /** @brief 카메라 X · Y 를 주고 자리를 맞춥니다(틱이 게임 카메라로 부릅니다. 시험은 직접 부릅니다). 시작 전이면 지금 자리를 저작한 자리로 잡습니다. */
        void applyCamera( const float2& camera );

    private:
        /** @brief 저작한 자리를 아직 안 잡았으면 지금 로컬 위치로 잡습니다. 잡았으면 true 입니다. */
        bool captureOrigin();

        PROPERTY( Category = "Parallax", DisplayName = "Scroll Factor", Tooltip = "Fraction of the camera motion this layer follows on screen; 1 = world, 0 = fixed to the camera" )
        float2 _scrollFactor;
        PROPERTY( Category = "Parallax", DisplayName = "Repeat Size", Tooltip = "Content repeats every this many units per axis (0 = no wrap)", Min = 0.0, Units = m )
        float2 _repeatSize;
        PROPERTY( Category = "Parallax", DisplayName = "Reference Point", Tooltip = "Camera position at which the layer sits where it was authored", Units = m )
        float2 _referencePoint;
        float3 _origin;     ///< 저작한 로컬 위치(시작할 때 잡습니다). 저장하지 않습니다
        uint8  _bHasOrigin; ///< `_origin` 을 잡았는가
    };
} // namespace sw
