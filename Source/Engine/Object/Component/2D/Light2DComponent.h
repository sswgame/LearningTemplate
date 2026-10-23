/**
 * @file Light2DComponent.h
 * @brief 2D 빛 — 점 · 스폿(`PointLight2DComponent`), 자유 모양(`FreeformLight2DComponent`), 전역(`GlobalLight2DComponent`)입니다. 빛 받는 스프라이트(`sprite2dlit.material`)만 비춥니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum Light2DBlendStyle
     * @brief 2D 빛이 다른 2D 빛과 섞이는 방식입니다(유니티 Light 2D 의 Blend Style 자리). 값은 셰이더 `SW_LIGHT2D_BLEND_*` 와 같습니다.
     * @details 더하기 · 빼기 빛을 모두 더한 뒤(0 아래는 0) 곱하기 빛을 곱한다 — 빛의 순서와 무관하다. 곱하기 빛은 닿는 곳을 lerp( 1, 색 × 세기, 감쇠 ) 로 곱한다.
     */
    ENUM()
    enum class Light2DBlendStyle : uint8
    {
        Additive    = 0, ///< 빛을 더한다(기본)
        Multiply    = 1, ///< 다른 빛의 합에 곱한다(색 필터 · 어둡게 하는 빛)
        Subtractive = 2, ///< 빛을 뺀다(어둠 빛)
    };

    /**
     * @class PointLight2DComponent
     * @brief 한 점에서 퍼지는 2D 빛입니다. 바깥 각을 줄이면 스폿(원뿔)입니다. 그림자를 켜면 2D 가림막(`ShadowCaster2DComponent`)에 가려집니다.
     * @details 유니티 Light 2D(Point · 안/바깥 반경 · 안/바깥 각 · Falloff · Normal Map Distance · Shadows), Godot PointLight2D 의 자리입니다.
     *          감쇠: 안 반경 안은 1, 바깥 반경에서 0, 그 사이는 ((바깥 − 거리) / (바깥 − 안))^지수(`computeAttenuation` — 셰이더 lighting2d.hlsli 와 같은 식).
     *          원뿔 방향은 컴포넌트의 로컬 +X 를 월드 회전으로 돌린 것입니다(2D 는 Z 축 회전).
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Point Light 2D", Tooltip = "2D point or spot light for lit sprites", Meta = "Icon=LightPoint, IconColor=Light, Billboard" )
    class SW_API PointLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        PointLight2DComponent();
        virtual ~PointLight2DComponent() override = default;

        /**
         * @brief 빛에서 @p distance 떨어진 곳의 감쇠(원뿔 제외)입니다. 셰이더 `LightData::computeLight2dAttenuation` 과 같은 식입니다.
         * @param exponent 1 이면 선형, 2 면 제곱으로 빨리 어두워집니다
         */
        static float32 computeAttenuation( float32 distance, float32 innerRadius, float32 outerRadius, float32 exponent );

        void setRadius( float32 innerRadius, float32 outerRadius );
        void setFalloffExponent( float32 exponent ) { _falloffExponent = exponent; }
        /** @brief 원뿔의 안 · 바깥 전체 각(라디안)입니다. 2π 면 원뿔이 없습니다. */
        void setConeAngles( float32 innerAngle, float32 outerAngle );
        void setCastShadows( bool bCastShadows ) { _bCastShadows = bCastShadows; }
        void setNormalMapHeight( float32 height ) { _normalMapHeight = height; }
        /** @brief 그림자 원천 반경(m)입니다. 0 이면 딱딱한 그림자, 크면 반그림자가 넓어집니다. 음수는 0 으로 자릅니다. */
        void    setShadowSoftness( float32 softness );
        void    setBlendStyle( Light2DBlendStyle blendStyle ) { _blendStyle = blendStyle; }
        float32 getOuterRadius() const { return _outerRadius; }
        /**
         * @brief 빛 텍스처(쿠키) 경로를 정합니다. 비면 쿠키가 없습니다. 텍스처는 바깥 반경의 정사각형을 덮고 원뿔 방향(로컬 +X)이 텍스처의 +U 다(유니티 Sprite Light 2D 의 자리).
         * @details 빛 색에 텍스처 rgb 를 곱한다. 한 프레임에 쓰는 쿠키는 `shaderslot::kLight2DCookieCellCount` 개까지다 — 넘는 쿠키는 오류를 남기고 쿠키 없이 그린다.
         */
        void          setCookieTexturePath( hashed_string path ) { _cookieTexturePath = path; }
        hashed_string getCookieTexturePath() const override { return _cookieTexturePath; }

    protected:
        void writeGPULightKindFields( GPULight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Inner Radius", Min = 0.0, Tooltip = "Full intensity inside this distance", Units = m )
        float32 _innerRadius;
        PROPERTY( Category = "Light", DisplayName = "Outer Radius", Min = 0.01, Tooltip = "Distance at which the light reaches zero", Units = m )
        float32 _outerRadius;
        PROPERTY( Category = "Light", DisplayName = "Falloff Exponent", Min = 0.01, Tooltip = "1 fades linearly, larger values fade faster near the edge" )
        float32 _falloffExponent;
        PROPERTY( Category = "Light", DisplayName = "Inner Angle", Min = 0.0, Max = 6.2831853, Tooltip = "Full intensity inside this cone angle", Units = rad )
        float32 _innerAngle;
        PROPERTY( Category = "Light", DisplayName = "Outer Angle", Min = 0.0, Max = 6.2831853, Tooltip = "Cone angle where the light reaches zero; a full turn is a point light",
                  Units = rad )
        float32 _outerAngle;
        PROPERTY( Category = "Light", DisplayName = "Normal Map Height", Min = 0.0, Tooltip = "How far in front of the sprite plane the light sits for normal maps",
                  Units = m )
        float32 _normalMapHeight;
        PROPERTY( Category = "Light", DisplayName = "Cast Shadows", Tooltip = "Shadow Caster 2D shapes block this light" )
        bool _bCastShadows;
        PROPERTY( Category = "Light", DisplayName = "Shadow Softness", Min = 0.0, Tooltip = "Radius of the light source; 0 casts hard shadows, larger values widen the penumbra",
                  Units = m )
        float32 _shadowSoftness;
        PROPERTY( Category = "Light", DisplayName = "Blend Style", Tooltip = "How this light combines with the other 2D lights" )
        Light2DBlendStyle _blendStyle;
        PROPERTY( Category = "Light", DisplayName = "Cookie Texture", AssetPath, AssetType = "Texture",
                  Tooltip = "Texture multiplied into the light color; it covers the outer radius square, +U along the cone direction" )
        hashed_string _cookieTexturePath;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FreeformLight2DComponent
     * @brief 다각형 모양의 2D 빛입니다. 다각형 안은 세기 그대로, 밖은 감쇠 거리까지 줄어듭니다(유니티 Freeform Light 2D 의 자리).
     * @details 꼭짓점은 컴포넌트 로컬 XY 이고 월드 트랜스폼을 따른다. GPU 에는 빛 원소 하나 뒤에 변마다 `shaderslot::kLightTypeFreeform2DEdge` 원소가 붙는다
     *          (`appendGPUTrailingElements`). 노멀 맵 · 그림자의 빛 위치는 컴포넌트 원점이다.
     *          감쇠: 안은 1, 밖은 (1 − 다각형까지 거리 / 감쇠 거리)^지수(`computeAttenuation` — 셰이더 lighting2d.hlsli 와 같은 식).
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Freeform Light 2D", Tooltip = "2D light shaped by a polygon for lit sprites", Meta = "Icon=LightPoint, IconColor=Light, Billboard" )
    class SW_API FreeformLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        FreeformLight2DComponent();
        virtual ~FreeformLight2DComponent() override = default;

        /**
         * @brief 점 @p point 에서 다각형 @p listPolygon(월드, 감김 무관)의 감쇠입니다. 셰이더 `LightData::computeFreeformLight2dAttenuation` 과 같은 식입니다.
         * @return 다각형 안은 1, 밖은 (1 − 거리 / @p falloffDistance)^@p exponent 를 [0, 1] 로 자른 값입니다. 꼭짓점이 셋 미만이면 0 입니다.
         */
        static float32 computeAttenuation( const float2& point, const vector<float2>& listPolygon, float32 falloffDistance, float32 exponent );

        /**
         * @brief 다각형 꼭짓점(로컬 XY)을 정합니다.
         * @return 꼭짓점이 셋 미만이거나 `shaderslot::kLight2DMaxFreeformEdges` 를 넘으면 오류를 남기고 바꾸지 않은 채 false 입니다.
         */
        [[nodiscard]] bool    setPoints( const vector<float2>& listPoint );
        const vector<float2>& getPoints() const { return _listPoint; }
        /** @brief 다각형 밖에서 빛이 0 이 되는 거리(m)와 감쇠 지수입니다. */
        void setFalloff( float32 distance, float32 exponent );
        void setCastShadows( bool bCastShadows ) { _bCastShadows = bCastShadows; }
        void setNormalMapHeight( float32 height ) { _normalMapHeight = height; }
        /** @brief 그림자 원천 반경(m)입니다(`PointLight2DComponent::setShadowSoftness` 와 같다). */
        void setShadowSoftness( float32 softness );
        void setBlendStyle( Light2DBlendStyle blendStyle ) { _blendStyle = blendStyle; }
        /** @brief 꼭짓점을 월드 XY 로 옮깁니다. */
        void computeWorldPolygon( vector<float2>& outListPoint ) const;

        void appendGPUTrailingElements( vector<GPULight>& inoutListLight ) const override;

    protected:
        void writeGPULightKindFields( GPULight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Points", Tooltip = "Polygon corners in local XY (3 to 64)", Units = m )
        vector<float2> _listPoint;
        PROPERTY( Category = "Light", DisplayName = "Falloff Distance", Min = 0.0, Tooltip = "Distance outside the polygon at which the light reaches zero", Units = m )
        float32 _falloffDistance;
        PROPERTY( Category = "Light", DisplayName = "Falloff Exponent", Min = 0.01, Tooltip = "1 fades linearly, larger values fade faster near the edge" )
        float32 _falloffExponent;
        PROPERTY( Category = "Light", DisplayName = "Normal Map Height", Min = 0.0, Tooltip = "How far in front of the sprite plane the light sits for normal maps",
                  Units = m )
        float32 _normalMapHeight;
        PROPERTY( Category = "Light", DisplayName = "Cast Shadows", Tooltip = "Shadow Caster 2D shapes block this light (seen from the component origin)" )
        bool _bCastShadows;
        PROPERTY( Category = "Light", DisplayName = "Shadow Softness", Min = 0.0, Tooltip = "Radius of the light source; 0 casts hard shadows", Units = m )
        float32 _shadowSoftness;
        PROPERTY( Category = "Light", DisplayName = "Blend Style", Tooltip = "How this light combines with the other 2D lights" )
        Light2DBlendStyle _blendStyle;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GlobalLight2DComponent
     * @brief 빛 받는 스프라이트 모두에 같은 밝기를 주는 바탕 빛입니다(유니티 Global Light 2D · Godot CanvasModulate 의 자리).
     * @details 2D 빛이 하나도 없으면 빛 받는 스프라이트는 빛 없이(그대로) 그려집니다 — 빛을 하나라도 두면 바탕이 0 이 되므로 어두운 장면은 이것으로 바탕을 줍니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Global Light 2D", Tooltip = "Ambient light for every lit sprite", Meta = "Icon=LightGlobal, IconColor=Light, Billboard" )
    class SW_API GlobalLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        GlobalLight2DComponent();
        virtual ~GlobalLight2DComponent() override = default;

    protected:
        void writeGPULightKindFields( GPULight& outLight ) const override;
    };
} // namespace sw
