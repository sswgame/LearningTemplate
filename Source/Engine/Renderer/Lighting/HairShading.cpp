#include "pch.h"

#include "Engine/Renderer/Lighting/HairShading.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct HairShadingInternal
        {
            // 식의 상수 — hair_shading.hlsli 와 같은 값이다(언리얼 HairBxDF 근사, Karis 2016).
            static constexpr float32 kInverseSqrtTwoPi  = 0.39894228f; ///< 가우스 정규화 1 / sqrt( 2π )
            static constexpr float32 kHalf              = 0.5f;
            static constexpr float32 kAzimuthR          = 0.25f; ///< R 갈래의 방위 몫(cos φ/2 에 곱함)
            static constexpr float32 kShiftR            = 2.0f;  ///< R 은 뿌리 쪽으로 2α
            static constexpr float32 kShiftTRT          = 4.0f;  ///< TRT 는 끝 쪽으로 4α
            static constexpr float32 kWidthTT           = 0.5f;  ///< TT 폭 = 폭 / 2
            static constexpr float32 kWidthTRT          = 2.0f;  ///< TRT 폭 = 폭 × 2
            static constexpr float32 kIndexScale        = 1.19f; ///< 유효 굴절률 n' = 1.19 / cos θd + 0.36 cos θd
            static constexpr float32 kIndexBias         = 0.36f;
            static constexpr float32 kHeightBias        = 0.6f; ///< TT 의 출사 높이 h = cos φ/2 (1 + a (0.6 − 0.8 cos φ))
            static constexpr float32 kHeightScale       = 0.8f;
            static constexpr float32 kAzimuthTTScale    = -3.65f; ///< TT 방위 exp( −3.65 cos φ − 3.98 )
            static constexpr float32 kAzimuthTTBias     = -3.98f;
            static constexpr float32 kAzimuthTRTScale   = 17.0f; ///< TRT 방위 exp( 17 cos φ − 16.78 )
            static constexpr float32 kAzimuthTRTBias    = -16.78f;
            static constexpr float32 kAbsorbTRT         = 0.8f;  ///< TRT 흡수 거듭제곱 0.8 / cos θd
            static constexpr float32 kInnerAngle        = 0.5f;  ///< TRT 안쪽 반사의 cos = cos θd × 0.5
            static constexpr float32 kScatterKajiyaMix  = 0.33f; ///< 감싸기 확산과 Kajiya 확산의 섞음
            static constexpr float32 kScatterWrapScale  = 0.25f; ///< 감싸기 1 의 ( N·L + 1 ) / ( 1 + 1 )²
            static constexpr float32 kColorFloor        = 1e-5f; ///< 흡수 색 거듭제곱의 0 바닥
            static constexpr float32 kAzimuthEpsilon    = 1e-4f; ///< cos φ 분모가 0 이 되지 않게
            static constexpr float32 kFlatLengthEpsilon = 1e-4f; ///< 시선이 노멀과 나란하면 가짜 노멀을 쓰지 않는다

            /** @brief 갈래 식이 함께 쓰는 가닥 각 값입니다(셰이더와 같은 순서로 계산한다). */
            struct StrandInput
            {
                float32 _sumSin{ 0.0f };
                float32 _cosThetaD{ 1.0f };
                float32 _cosPhi{ 1.0f };
                float32 _cosHalfPhi{ 1.0f };
                float32 _width{ 0.0f };
            };

            static StrandInput makeStrandInput( const HairSurface& surface, const float3& toView, const float3& toLight )
            {
                StrandInput   input{};
                const float32 sinThetaL = MathUtil::clamp( surface._tangent.dot( toLight ), -1.0f, 1.0f );
                const float32 sinThetaV = MathUtil::clamp( surface._tangent.dot( toView ), -1.0f, 1.0f );
                input._cosThetaD        = MathUtil::cos( kHalf * MathUtil::abs( MathUtil::asin( sinThetaL ) - MathUtil::asin( sinThetaV ) ) );
                const float3 lightPerp  = toLight - surface._tangent * sinThetaL;
                const float3 viewPerp   = toView - surface._tangent * sinThetaV;
                input._cosPhi           = lightPerp.dot( viewPerp ) / MathUtil::sqrt( lightPerp.dot( lightPerp ) * viewPerp.dot( viewPerp ) + kAzimuthEpsilon );
                input._cosHalfPhi       = MathUtil::sqrt( MathUtil::saturate( kHalf + kHalf * input._cosPhi ) );
                const float32 roughness = MathUtil::max( surface._roughness, HairSurface::kMinRoughness );
                input._width            = roughness * roughness;
                input._sumSin           = sinThetaL + sinThetaV;
                return input;
            }

            /** @brief 흡수 색의 채널마다 거듭제곱입니다(셰이더 `pow( max( baseColor, 1e-5 ), exponent )`). */
            static float3 computeAbsorption( const float3& color, float32 exponent )
            {
                return float3{ MathUtil::pow( MathUtil::max( color._x, kColorFloor ), exponent ), MathUtil::pow( MathUtil::max( color._y, kColorFloor ), exponent ),
                               MathUtil::pow( MathUtil::max( color._z, kColorFloor ), exponent ) };
            }

            static float32 computeReflection( const HairSurface& surface, const StrandInput& input, const float3& toView, const float3& toLight )
            {
                const float32 fresnel = HairSurface::computeFresnel( MathUtil::sqrt( MathUtil::saturate( kHalf + kHalf * toLight.dot( toView ) ) ) );
                return HairSurface::computeLongitudinal( input._width, input._sumSin + kShiftR * surface._shift ) * kAzimuthR * input._cosHalfPhi * fresnel * surface._specular;
            }
        };
    } // namespace

    float32 HairSurface::computeLongitudinal( float32 width, float32 offset )
    {
        return MathUtil::exp( -HairShadingInternal::kHalf * offset * offset / ( width * width ) ) * HairShadingInternal::kInverseSqrtTwoPi / width;
    }

    float32 HairSurface::computeFresnel( float32 cosine )
    {
        const float32 oneMinus = 1.0f - MathUtil::saturate( cosine );
        const float32 square   = oneMinus * oneMinus;
        return kF0 + ( 1.0f - kF0 ) * square * square * oneMinus;
    }

    float3 HairSurface::shiftTangent( const float3& tangent, const float3& normal, float32 shift )
    {
        return ( tangent + normal * shift ).normalize();
    }

    float32 HairSurface::evaluateReflection( const float3& toView, const float3& toLight ) const
    {
        return HairShadingInternal::computeReflection( *this, HairShadingInternal::makeStrandInput( *this, toView, toLight ), toView, toLight );
    }

    float3 HairSurface::evaluateLobes( const float3& toView, const float3& toLight ) const
    {
        using Internal                    = HairShadingInternal;
        const Internal::StrandInput input = Internal::makeStrandInput( *this, toView, toLight );
        const float32               lobeR = Internal::computeReflection( *this, input, toView, toLight );

        const float32 inverseIndex    = 1.0f / ( Internal::kIndexScale / input._cosThetaD + Internal::kIndexBias * input._cosThetaD );
        const float32 height          = input._cosHalfPhi * ( 1.0f + inverseIndex * ( Internal::kHeightBias - Internal::kHeightScale * input._cosPhi ) );
        const float32 transmitFresnel = computeFresnel( input._cosThetaD * MathUtil::sqrt( MathUtil::saturate( 1.0f - height * height ) ) );
        const float32 transmitPower   = Internal::kHalf * MathUtil::sqrt( MathUtil::saturate( 1.0f - height * height * inverseIndex * inverseIndex ) ) / input._cosThetaD;
        const float32 transmitScale   = computeLongitudinal( input._width * Internal::kWidthTT, input._sumSin - _shift ) *
                                      MathUtil::exp( Internal::kAzimuthTTScale * input._cosPhi + Internal::kAzimuthTTBias ) * ( 1.0f - transmitFresnel ) * ( 1.0f - transmitFresnel );

        const float32 innerFresnel = computeFresnel( input._cosThetaD * Internal::kInnerAngle );
        const float32 innerScale   = computeLongitudinal( input._width * Internal::kWidthTRT, input._sumSin - Internal::kShiftTRT * _shift ) *
                                   MathUtil::exp( Internal::kAzimuthTRTScale * input._cosPhi + Internal::kAzimuthTRTBias ) * ( 1.0f - innerFresnel ) * ( 1.0f - innerFresnel ) * innerFresnel;
        return float3{ lobeR, lobeR, lobeR } + Internal::computeAbsorption( _baseColor, transmitPower ) * transmitScale +
               Internal::computeAbsorption( _baseColor, Internal::kAbsorbTRT / input._cosThetaD ) * innerScale;
    }

    float3 HairSurface::evaluateScatter( const float3& normal, const float3& toView, const float3& toLight ) const
    {
        using Internal           = HairShadingInternal;
        const float3  viewFlat   = toView - normal * toView.dot( normal );
        const float32 flatLength = viewFlat.getLength();
        const float3  fakeNormal = flatLength > Internal::kFlatLengthEpsilon ? viewFlat * ( 1.0f / flatLength ) : normal;
        const float32 wrapped    = MathUtil::saturate( ( fakeNormal.dot( toLight ) + 1.0f ) * Internal::kScatterWrapScale );
        const float32 kajiya     = 1.0f - MathUtil::abs( _tangent.dot( toLight ) );
        const float32 scale      = MathUtil::lerp( wrapped, kajiya, Internal::kScatterKajiyaMix ) * _scatter / MathUtil::kPi;
        return float3{ MathUtil::sqrt( MathUtil::max( _baseColor._x, 0.0f ) ), MathUtil::sqrt( MathUtil::max( _baseColor._y, 0.0f ) ), MathUtil::sqrt( MathUtil::max( _baseColor._z, 0.0f ) ) } *
               scale;
    }
} // namespace sw
