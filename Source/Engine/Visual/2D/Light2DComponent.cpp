#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/LightComponent.h"

namespace sw
{
    class Texture2D;

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
     *          감쇠: 안 반경 안은 1, 바깥 반경에서 0, 그 사이는 ((바깥 − 거리) / (바깥 − 안))^지수(`computeAttenuation` — 셰이더 lighting_2d.hlsli 와 같은 식).
     *          원뿔 방향은 컴포넌트의 로컬 +X 를 월드 회전으로 돌린 것입니다(2D 는 Z 축 회전).
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Point Light 2D", Tooltip = "2D point or spot light for lit sprites", Meta = "Icon=LightPoint, IconColor=Light, Billboard" )
    class PointLight2DComponent : public LightComponent
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
        void          setCookieTexturePath( string_view path ) { _cookieTexturePath = path; }
        hashed_string getCookieTexturePath() const override { return _cookieTexturePath.key(); } // 열쇠는 넣을 때 만든다 — 빛마다 프레임마다 intern 하지 않는다

    protected:
        void writeGPULightKindProperties( GPULight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Inner Radius", Min = 0.0, Tooltip = "Full intensity inside this distance", Units = m, Meta = "Shape=Circle" )
        float32 _innerRadius;
        PROPERTY( Category = "Light", DisplayName = "Outer Radius", Min = 0.01, Tooltip = "Distance at which the light reaches zero", Units = m, Meta = "Shape=Circle" )
        float32 _outerRadius;
        PROPERTY( Category = "Light", DisplayName = "Falloff Exponent", Min = 0.01, Tooltip = "1 fades linearly, larger values fade faster near the edge" )
        float32 _falloffExponent;
        PROPERTY( Category = "Light", DisplayName = "Inner Angle", Min = 0.0, Max = 360.0, Tooltip = "Full intensity inside this cone angle" )
        Radians _innerAngle;
        PROPERTY( Category = "Light", DisplayName = "Outer Angle", Min = 0.0, Max = 360.0, Tooltip = "Cone angle where the light reaches zero; a full turn is a point light" )
        Radians _outerAngle;
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
        PROPERTY( Category = "Light", DisplayName = "Cookie Texture", Tooltip = "Texture multiplied into the light color; it covers the outer radius square, +U along the cone direction" )
        AssetRef<Texture2D> _cookieTexturePath;
    };

    /**
     * @class FreeformLight2DComponent
     * @brief 다각형 모양의 2D 빛입니다. 다각형 안은 세기 그대로, 밖은 감쇠 거리까지 줄어듭니다(유니티 Freeform Light 2D 의 자리).
     * @details 꼭짓점은 컴포넌트 로컬 XY 이고 월드 트랜스폼을 따른다. GPU 에는 빛 원소 하나 뒤에 변마다 `shaderslot::kLightTypeFreeform2DEdge` 원소가 붙는다
     *          (`appendGPUTrailingElements`). 노멀 맵 · 그림자의 빛 위치는 컴포넌트 원점이다.
     *          감쇠: 안은 1, 밖은 (1 − 다각형까지 거리 / 감쇠 거리)^지수(`computeAttenuation` — 셰이더 lighting_2d.hlsli 와 같은 식).
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Freeform Light 2D", Tooltip = "2D light shaped by a polygon for lit sprites", Meta = "Icon=LightPoint, IconColor=Light, Billboard" )
    class FreeformLight2DComponent : public LightComponent
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
        void writeGPULightKindProperties( GPULight& outLight ) const override;

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

    /**
     * @class GlobalLight2DComponent
     * @brief 빛 받는 스프라이트 모두에 같은 밝기를 주는 바탕 빛입니다(유니티 Global Light 2D · Godot CanvasModulate 의 자리).
     * @details 2D 빛이 하나도 없으면 빛 받는 스프라이트는 빛 없이(그대로) 그려집니다 — 빛을 하나라도 두면 바탕이 0 이 되므로 어두운 장면은 이것으로 바탕을 줍니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Global Light 2D", Tooltip = "Ambient light for every lit sprite", Meta = "Icon=LightGlobal, IconColor=Light, Billboard" )
    class GlobalLight2DComponent : public LightComponent
    {
    public:
        REFLECT_BODY();
        GlobalLight2DComponent();
        virtual ~GlobalLight2DComponent() override = default;

    protected:
        void writeGPULightKindProperties( GPULight& outLight ) const override;
    };

    static_assert( static_cast<uint32>( Light2DBlendStyle::Additive ) == shaderslot::kLight2DBlendAdditive &&
                       static_cast<uint32>( Light2DBlendStyle::Multiply ) == shaderslot::kLight2DBlendMultiply &&
                       static_cast<uint32>( Light2DBlendStyle::Subtractive ) == shaderslot::kLight2DBlendSubtractive,
                   "Light2DBlendStyle 값은 셰이더 SW_LIGHT2D_BLEND_* 와 같아야 한다" );

    namespace
    {
        struct Light2DComponentInternal
        {
            static constexpr float3  kPointDefaultColor{ 1.0f, 0.85f, 0.6f };
            static constexpr float32 kPointDefaultIntensity{ 1.0f };
            static constexpr float3  kGlobalDefaultColor{ 1.0f, 1.0f, 1.0f };
            static constexpr float32 kGlobalDefaultIntensity{ 0.25f };
            static constexpr float32 kMinimumRadius{ 0.01f };
            static constexpr float32 kFreeformDefaultHalfSize{ 1.0f };
            static constexpr float32 kFreeformDefaultFalloff{ 1.0f };

            /** @brief 2D 빛 공통 덧칸(그림자 원천 반경 · 섞는 방식 · 뒤 원소 수)을 씁니다 — 칸의 뜻은 lighting_2d.hlsli 머리 주석의 표. */
            static float4 makeExtra( float32 shadowSoftness, Light2DBlendStyle blendStyle, uint32 trailingCount )
            {
                // w 는 쿠키 아틀라스 칸 — 빛은 모른다(렌더러가 칸을 나눠 적는다, Light2DCookieAtlas). 칸이 없다는 값으로 둔다.
                return float4{ MathUtil::max( shadowSoftness, 0.0f ), static_cast<float32>( static_cast<uint8>( blendStyle ) ), static_cast<float32>( trailingCount ),
                               static_cast<float32>( shaderslot::kLight2DNoCookieCell ) };
            }

            /** @brief 점에서 선분까지의 거리입니다. */
            static float32 computeSegmentDistance( const float2& point, const float2& start, const float2& end )
            {
                const float2  edge       = end - start;
                const float32 lengthSq   = edge.getLengthSquared();
                const float2  offset     = point - start;
                const float32 along      = lengthSq > MathUtil::kEpsilon ? MathUtil::saturate( ( offset._x * edge._x + offset._y * edge._y ) / lengthSq ) : 0.0f;
                const float2  projection = start + edge * along;
                return ( point - projection ).getLength();
            }

            /** @brief 전체 각(라디안)의 반각 cos 입니다. 2π 이상이면 −1(원뿔 없음)입니다. */
            static float32 computeHalfAngleCos( float32 angle )
            {
                const float32 clamped = MathUtil::clamp( angle, 0.0f, 2.0f * MathUtil::kPi );
                return MathUtil::cos( clamped * 0.5f );
            }
        };
    } // namespace

    PointLight2DComponent::PointLight2DComponent()
        : LightComponent( shaderslot::kLightTypePoint2D, Light2DComponentInternal::kPointDefaultColor, Light2DComponentInternal::kPointDefaultIntensity )
        , _innerRadius{ 0.0f }
        , _outerRadius{ 4.0f }
        , _falloffExponent{ 1.0f }
        , _innerAngle{ Radians::fromRadians( 2.0f * MathUtil::kPi ) }
        , _outerAngle{ Radians::fromRadians( 2.0f * MathUtil::kPi ) }
        , _normalMapHeight{ 1.0f }
        , _bCastShadows{ true }
        , _shadowSoftness{ 0.0f }
        , _blendStyle{ Light2DBlendStyle::Additive }
        , _cookieTexturePath{}
    {
    }

    void PointLight2DComponent::setShadowSoftness( float32 softness )
    {
        _shadowSoftness = MathUtil::max( softness, 0.0f );
    }

    float32 PointLight2DComponent::computeAttenuation( float32 distance, float32 innerRadius, float32 outerRadius, float32 exponent )
    {
        const float32 outer = MathUtil::max( outerRadius, MathUtil::kLooseEpsilon );
        const float32 inner = MathUtil::clamp( innerRadius, 0.0f, outer );
        if ( distance >= outer )
            return 0.0f;
        const float32 ramp = MathUtil::saturate( ( outer - distance ) / MathUtil::max( outer - inner, MathUtil::kLooseEpsilon ) );
        return MathUtil::pow( ramp, MathUtil::max( exponent, 1e-3f ) );
    }

    void PointLight2DComponent::setRadius( float32 innerRadius, float32 outerRadius )
    {
        _outerRadius = MathUtil::max( outerRadius, Light2DComponentInternal::kMinimumRadius );
        _innerRadius = MathUtil::clamp( innerRadius, 0.0f, _outerRadius );
    }

    void PointLight2DComponent::setConeAngles( float32 innerAngle, float32 outerAngle )
    {
        _outerAngle = Radians::fromRadians( MathUtil::clamp( outerAngle, 0.0f, 2.0f * MathUtil::kPi ) );
        _innerAngle = Radians::fromRadians( MathUtil::clamp( innerAngle, 0.0f, _outerAngle.radians() ) );
    }

    void PointLight2DComponent::writeGPULightKindProperties( GPULight& outLight ) const
    {
        using Internal           = Light2DComponentInternal;
        const float3 position    = getLightPosition();
        const float3 direction   = computeLightDirection( float3{ 1.0f, 0.0f, 0.0f } );
        float2       direction2D = float2{ direction._x, direction._y };
        if ( direction2D.getLengthSquared() <= MathUtil::kEpsilon )
            direction2D = float2{ 1.0f, 0.0f };
        direction2D = direction2D.normalize();
        // 칸의 뜻은 lighting_2d.hlsli 머리 주석의 표 — z 는 2D 거리에 쓰이지 않아 노멀 맵 높이를 싣는다.
        outLight._positionRadius = float4{ position._x, position._y, MathUtil::max( _normalMapHeight, 1e-3f ), _outerRadius };
        outLight._directionType  = float4{ direction2D._x, direction2D._y, _innerRadius, outLight._directionType._w };
        outLight._params         = float4{ _bCastShadows ? 1.0f : 0.0f, Internal::computeHalfAngleCos( _outerAngle.radians() ),
                                   Internal::computeHalfAngleCos( _innerAngle.radians() ), MathUtil::max( _falloffExponent, 1e-3f ) };
        outLight._extra          = Internal::makeExtra( _shadowSoftness, _blendStyle, 0 );
    }

    FreeformLight2DComponent::FreeformLight2DComponent()
        : LightComponent( shaderslot::kLightTypeFreeform2D, Light2DComponentInternal::kPointDefaultColor, Light2DComponentInternal::kPointDefaultIntensity )
        , _listPoint{
              float2{-Light2DComponentInternal::kFreeformDefaultHalfSize, -Light2DComponentInternal::kFreeformDefaultHalfSize},
              float2{ Light2DComponentInternal::kFreeformDefaultHalfSize, -Light2DComponentInternal::kFreeformDefaultHalfSize},
              float2{ Light2DComponentInternal::kFreeformDefaultHalfSize,  Light2DComponentInternal::kFreeformDefaultHalfSize},
              float2{-Light2DComponentInternal::kFreeformDefaultHalfSize,  Light2DComponentInternal::kFreeformDefaultHalfSize}
    },
        _falloffDistance{ Light2DComponentInternal::kFreeformDefaultFalloff },
        _falloffExponent{ 1.0f },
        _normalMapHeight{ 1.0f },
        _bCastShadows{ false },
        _shadowSoftness{ 0.0f },
        _blendStyle{ Light2DBlendStyle::Additive }
    {
    }

    float32 FreeformLight2DComponent::computeAttenuation( const float2& point, const vector<float2>& listPolygon, float32 falloffDistance, float32 exponent )
    {
        using Internal = Light2DComponentInternal;
        if ( listPolygon.size() < 3 )
            return 0.0f;
        // 안 · 밖은 짝홀 규칙(반직선 교차 수), 거리는 변까지의 최소 거리다 — 셰이더와 같은 순서로 센다.
        bool    bInside{ false };
        float32 distance = MathUtil::kMaxFloat;
        for ( size_t index = 0; index < listPolygon.size(); ++index )
        {
            const float2& start   = listPolygon[index];
            const float2& end     = listPolygon[( index + 1 ) % listPolygon.size()];
            distance              = MathUtil::min( distance, Internal::computeSegmentDistance( point, start, end ) );
            const bool bStraddles = ( start._y > point._y ) != ( end._y > point._y );
            if ( bStraddles && point._x < ( end._x - start._x ) * ( point._y - start._y ) / ( end._y - start._y ) + start._x )
                bInside = !bInside;
        }
        if ( bInside )
            return 1.0f;
        const float32 ramp = MathUtil::saturate( 1.0f - distance / MathUtil::max( falloffDistance, MathUtil::kLooseEpsilon ) );
        return MathUtil::pow( ramp, MathUtil::max( exponent, 1e-3f ) );
    }

    bool FreeformLight2DComponent::setPoints( const vector<float2>& listPoint )
    {
        const bool bValidCount = listPoint.size() >= 3 && listPoint.size() <= shaderslot::kLight2DMaxFreeformEdges;
        if ( bValidCount == false )
        {
            SW_LOG_ERROR_ID( "Light2D.FreeformLight2DComponentSetPointsCornersNeeds", "FreeformLight2DComponent::setPoints: %# corners (needs 3 to %#) - the polygon is unchanged", static_cast<uint64>( listPoint.size() ), shaderslot::kLight2DMaxFreeformEdges );
            return false;
        }
        _listPoint = listPoint;
        return true;
    }

    void FreeformLight2DComponent::setFalloff( float32 distance, float32 exponent )
    {
        _falloffDistance = MathUtil::max( distance, 0.0f );
        _falloffExponent = MathUtil::max( exponent, 1e-3f );
    }

    void FreeformLight2DComponent::setShadowSoftness( float32 softness )
    {
        _shadowSoftness = MathUtil::max( softness, 0.0f );
    }

    void FreeformLight2DComponent::computeWorldPolygon( vector<float2>& outListPoint ) const
    {
        outListPoint.clear();
        const float4x4 world = getWorldMatrix();
        for ( const float2& point : _listPoint )
        {
            const float3 worldPoint = float3::transform( float3{ point._x, point._y, 0.0f }, world );
            outListPoint.push_back( float2{ worldPoint._x, worldPoint._y } );
        }
    }

    void FreeformLight2DComponent::writeGPULightKindProperties( GPULight& outLight ) const
    {
        using Internal = Light2DComponentInternal;
        vector<float2> listWorld;
        computeWorldPolygon( listWorld );
        // 데이터(씬 파일)가 상한을 넘는 다각형을 들고 왔으면 변을 싣지 않는다 — 셰이더 루프가 끝까지 돌지 못한다. 오류로 알린다.
        const bool bValidPolygon = listWorld.size() >= 3 && listWorld.size() <= shaderslot::kLight2DMaxFreeformEdges;
        if ( bValidPolygon == false )
            SW_LOG_ERROR_ID( "Light2D.FreeformLight2DComponentCornersNeedsLight", "FreeformLight2DComponent: %# corners (needs 3 to %#) - the light is not drawn", static_cast<uint64>( listWorld.size() ), shaderslot::kLight2DMaxFreeformEdges );
        const float3 origin = getLightPosition();
        float32      reach  = 0.0f;
        for ( const float2& point : listWorld )
        {
            reach = MathUtil::max( reach, ( point - float2{ origin._x, origin._y } ).getLength() );
        }
        // 칸의 뜻은 lighting_2d.hlsli 머리 주석의 표 — 반경(w)은 원점에서 빛이 닿는 가장 먼 거리다.
        outLight._positionRadius = float4{ origin._x, origin._y, MathUtil::max( _normalMapHeight, 1e-3f ), reach + _falloffDistance };
        outLight._directionType  = float4{ 0.0f, 0.0f, _falloffDistance, outLight._directionType._w };
        outLight._params         = float4{ _bCastShadows ? 1.0f : 0.0f, 0.0f, 0.0f, MathUtil::max( _falloffExponent, 1e-3f ) };
        outLight._extra          = Internal::makeExtra( _shadowSoftness, _blendStyle, bValidPolygon ? static_cast<uint32>( listWorld.size() ) : 0u );
    }

    void FreeformLight2DComponent::appendGPUTrailingElements( vector<GPULight>& inoutListLight ) const
    {
        vector<float2> listWorld;
        computeWorldPolygon( listWorld );
        const bool bValidPolygon = listWorld.size() >= 3 && listWorld.size() <= shaderslot::kLight2DMaxFreeformEdges;
        if ( bValidPolygon == false )
            return; // writeGPULightKindProperties 가 변 수 0 으로 적고 오류를 남겼다
        for ( size_t index = 0; index < listWorld.size(); ++index )
        {
            const float2& start = listWorld[index];
            const float2& end   = listWorld[( index + 1 ) % listWorld.size()];
            GPULight      edge{};
            edge._positionRadius = float4{ start._x, start._y, end._x, end._y };
            edge._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypeFreeform2DEdge ) };
            inoutListLight.push_back( edge );
        }
    }

    GlobalLight2DComponent::GlobalLight2DComponent()
        : LightComponent( shaderslot::kLightTypeGlobal2D, Light2DComponentInternal::kGlobalDefaultColor, Light2DComponentInternal::kGlobalDefaultIntensity )
    {
    }

    void GlobalLight2DComponent::writeGPULightKindProperties( GPULight& /*outLight*/ ) const
    {
        // 색 · 세기(공통 칸)만 읽는다.
    }
} // namespace sw

#include "Light2DComponent.cpp.gen.inl"
