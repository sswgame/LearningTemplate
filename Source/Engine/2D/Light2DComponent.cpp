#include "pch.h"

#include "Engine/2D/Light2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "Light2D" );

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

            /** @brief 2D 빛 공통 덧칸(그림자 원천 반경 · 섞는 방식 · 뒤 원소 수)을 씁니다 — 칸의 뜻은 lighting2d.hlsli 머리 주석의 표. */
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
        , _innerAngle{ 2.0f * MathUtil::kPi }
        , _outerAngle{ 2.0f * MathUtil::kPi }
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
        const float32 outer = MathUtil::max( outerRadius, 1e-4f );
        const float32 inner = MathUtil::clamp( innerRadius, 0.0f, outer );
        if ( distance >= outer )
            return 0.0f;
        const float32 ramp = MathUtil::saturate( ( outer - distance ) / MathUtil::max( outer - inner, 1e-4f ) );
        return MathUtil::pow( ramp, MathUtil::max( exponent, 1e-3f ) );
    }

    void PointLight2DComponent::setRadius( float32 innerRadius, float32 outerRadius )
    {
        _outerRadius = MathUtil::max( outerRadius, Light2DComponentInternal::kMinimumRadius );
        _innerRadius = MathUtil::clamp( innerRadius, 0.0f, _outerRadius );
    }

    void PointLight2DComponent::setConeAngles( float32 innerAngle, float32 outerAngle )
    {
        _outerAngle = MathUtil::clamp( outerAngle, 0.0f, 2.0f * MathUtil::kPi );
        _innerAngle = MathUtil::clamp( innerAngle, 0.0f, _outerAngle );
    }

    void PointLight2DComponent::writeGPULightKindFields( GPULight& outLight ) const
    {
        using Internal           = Light2DComponentInternal;
        const float3 position    = getLightPosition();
        const float3 direction   = computeLightDirection( float3{ 1.0f, 0.0f, 0.0f } );
        float2       direction2D = float2{ direction._x, direction._y };
        if ( direction2D.getLengthSquared() <= MathUtil::kEpsilon )
            direction2D = float2{ 1.0f, 0.0f };
        direction2D.normalize();
        // 칸의 뜻은 lighting2d.hlsli 머리 주석의 표 — z 는 2D 거리에 쓰이지 않아 노멀 맵 높이를 싣는다.
        outLight._positionRadius = float4{ position._x, position._y, MathUtil::max( _normalMapHeight, 1e-3f ), _outerRadius };
        outLight._directionType  = float4{ direction2D._x, direction2D._y, _innerRadius, outLight._directionType._w };
        outLight._params         = float4{ _bCastShadows ? 1.0f : 0.0f, Internal::computeHalfAngleCos( _outerAngle ),
                                   Internal::computeHalfAngleCos( _innerAngle ), MathUtil::max( _falloffExponent, 1e-3f ) };
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
        const float32 ramp = MathUtil::saturate( 1.0f - distance / MathUtil::max( falloffDistance, 1e-4f ) );
        return MathUtil::pow( ramp, MathUtil::max( exponent, 1e-3f ) );
    }

    bool FreeformLight2DComponent::setPoints( const vector<float2>& listPoint )
    {
        const bool bValidCount = listPoint.size() >= 3 && listPoint.size() <= shaderslot::kLight2DMaxFreeformEdges;
        if ( bValidCount == false )
        {
            SW_LOG_ERROR( "FreeformLight2DComponent::setPoints: %# corners (needs 3 to %#) - the polygon is unchanged", static_cast<uint64>( listPoint.size() ), shaderslot::kLight2DMaxFreeformEdges );
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

    void FreeformLight2DComponent::writeGPULightKindFields( GPULight& outLight ) const
    {
        using Internal = Light2DComponentInternal;
        vector<float2> listWorld;
        computeWorldPolygon( listWorld );
        // 데이터(씬 파일)가 상한을 넘는 다각형을 들고 왔으면 변을 싣지 않는다 — 셰이더 루프가 끝까지 돌지 못한다. 오류로 알린다.
        const bool bValidPolygon = listWorld.size() >= 3 && listWorld.size() <= shaderslot::kLight2DMaxFreeformEdges;
        if ( bValidPolygon == false )
            SW_LOG_ERROR( "FreeformLight2DComponent: %# corners (needs 3 to %#) - the light is not drawn", static_cast<uint64>( listWorld.size() ), shaderslot::kLight2DMaxFreeformEdges );
        const float3 origin = getLightPosition();
        float32      reach  = 0.0f;
        for ( const float2& point : listWorld )
        {
            reach = MathUtil::max( reach, ( point - float2{ origin._x, origin._y } ).getLength() );
        }
        // 칸의 뜻은 lighting2d.hlsli 머리 주석의 표 — 반경(w)은 원점에서 빛이 닿는 가장 먼 거리다.
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
            return; // writeGPULightKindFields 가 변 수 0 으로 적고 오류를 남겼다
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

    void GlobalLight2DComponent::writeGPULightKindFields( GPULight& /*outLight*/ ) const
    {
        // 색 · 세기(공통 칸)만 읽는다.
    }
} // namespace sw
