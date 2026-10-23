#include "pch.h"

#include "Engine/Environment/Sky/SkyGradient.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct SkyGradientInternal
        {
            /// 해 광선이 행성에 가려지는지 볼 때 표면에서 띄우는 거리(km)입니다(표면 위의 점이 제 구에 걸리지 않게).
            static constexpr float32 kSurfaceEpsilonKm = 1.0e-3f;

            /**
             * @brief 반직선 origin + t · direction 과 중심이 원점인 구(반지름 @p radius)의 교점 거리입니다. 만나면 true 이고 @p outNear <= @p outFar 입니다.
             */
            static bool intersectSphere( const float3& origin, const float3& direction, float32 radius, float32& outNear, float32& outFar )
            {
                const float32 halfB        = origin.dot( direction );
                const float32 c            = origin.dot( origin ) - radius * radius;
                const float32 discriminant = halfB * halfB - c;
                if ( discriminant < 0.0f )
                    return false;
                const float32 root = MathUtil::sqrt( discriminant );
                outNear            = -halfB - root;
                outFar             = -halfB + root;
                return true;
            }

            /**
             * @brief 점 @p position 에서 해 쪽으로 대기 끝까지의 레일리 · 미 광학 두께(밀도 적분, 계수 곱하기 전)입니다. 행성이 해를 가리면 false 입니다.
             * @details sky.hlsl `computeSunOpticalDepth` 와 같은 식이다. 높이는 0 아래로 내려가지 않게 자른다.
             */
            static bool computeSunOpticalDepth( const SkyAtmosphere& atmosphere, const float3& position, const float3& toSun, float32& outRayleigh, float32& outMie )
            {
                const float32 planetRadius = atmosphere._planetRadiusKm;
                const float32 topRadius    = planetRadius + atmosphere._atmosphereHeightKm;
                float32       nearT{ 0.0f };
                float32       farT{ 0.0f };
                outRayleigh = 0.0f;
                outMie      = 0.0f;
                // 행성 가림: 해 쪽 반직선이 행성 구와 앞에서 만나면 해가 가려진다.
                float32    planetNear{ 0.0f };
                float32    planetFar{ 0.0f };
                const bool bPlanetHit = intersectSphere( position, toSun, planetRadius, planetNear, planetFar ) && planetFar > kSurfaceEpsilonKm && planetNear > kSurfaceEpsilonKm;
                if ( bPlanetHit )
                    return false;
                if ( intersectSphere( position, toSun, topRadius, nearT, farT ) == false )
                    return false;
                const float32 stepLength = farT / static_cast<float32>( SkyAtmosphere::kLightStepCount );
                for ( uint32 step = 0; step < SkyAtmosphere::kLightStepCount; ++step )
                {
                    const float3  samplePosition = position + toSun * ( ( static_cast<float32>( step ) + 0.5f ) * stepLength );
                    const float32 height         = MathUtil::max( samplePosition.getLength() - planetRadius, 0.0f );
                    outRayleigh += MathUtil::exp( -height / SkyAtmosphere::kRayleighScaleHeightKm ) * stepLength;
                    outMie += MathUtil::exp( -height / SkyAtmosphere::kMieScaleHeightKm ) * stepLength;
                }
                return true;
            }
        };
    } // namespace

    float3 SkyAtmosphere::computeRayleighBeta() const
    {
        return float3{ kRayleighBetaRedPerKm, kRayleighBetaGreenPerKm, kRayleighBetaBluePerKm } * _rayleighScale;
    }

    float32 SkyAtmosphere::computeMieBeta() const
    {
        return kMieBetaPerKm * _mieScale;
    }

    float3 SkyAtmosphere::evaluateScattering( const float3& direction, const float3& toSun, const float3& groundAlbedo ) const
    {
        using Internal             = SkyGradientInternal;
        const float32 planetRadius = _planetRadiusKm;
        const float32 topRadius    = planetRadius + _atmosphereHeightKm;
        const float3  origin{ 0.0f, planetRadius + _observerAltitudeKm, 0.0f };
        float32       atmosphereNear{ 0.0f };
        float32       atmosphereFar{ 0.0f };
        if ( Internal::intersectSphere( origin, direction, topRadius, atmosphereNear, atmosphereFar ) == false )
            return float3{};

        // 시선은 대기 끝까지, 행성 표면을 먼저 만나면 거기까지다.
        float32    segmentEnd = atmosphereFar;
        float32    planetNear{ 0.0f };
        float32    planetFar{ 0.0f };
        const bool bHitsGround = Internal::intersectSphere( origin, direction, planetRadius, planetNear, planetFar ) && planetNear > 0.0f;
        if ( bHitsGround )
            segmentEnd = MathUtil::min( segmentEnd, planetNear );

        const float3  betaRayleigh      = computeRayleighBeta();
        const float32 betaMie           = computeMieBeta();
        const float32 betaMieExtinction = betaMie * kMieExtinctionRatio;
        const float32 stepLength        = segmentEnd / static_cast<float32>( kViewStepCount );
        float32       viewRayleigh{ 0.0f };
        float32       viewMie{ 0.0f };
        float3        sumRayleigh{};
        float3        sumMie{};
        for ( uint32 step = 0; step < kViewStepCount; ++step )
        {
            const float3  samplePosition  = origin + direction * ( ( static_cast<float32>( step ) + 0.5f ) * stepLength );
            const float32 height          = MathUtil::max( samplePosition.getLength() - planetRadius, 0.0f );
            const float32 densityRayleigh = MathUtil::exp( -height / kRayleighScaleHeightKm ) * stepLength;
            const float32 densityMie      = MathUtil::exp( -height / kMieScaleHeightKm ) * stepLength;
            viewRayleigh += densityRayleigh;
            viewMie += densityMie;
            float32 sunRayleigh{ 0.0f };
            float32 sunMie{ 0.0f };
            if ( Internal::computeSunOpticalDepth( *this, samplePosition, toSun, sunRayleigh, sunMie ) == false )
                continue;
            const float32 opticalRayleigh = viewRayleigh + sunRayleigh;
            const float32 opticalMie      = viewMie + sunMie;
            const float3  attenuation{ MathUtil::exp( -( betaRayleigh._x * opticalRayleigh + betaMieExtinction * opticalMie ) ),
                                      MathUtil::exp( -( betaRayleigh._y * opticalRayleigh + betaMieExtinction * opticalMie ) ),
                                      MathUtil::exp( -( betaRayleigh._z * opticalRayleigh + betaMieExtinction * opticalMie ) ) };
            sumRayleigh = sumRayleigh + attenuation * densityRayleigh;
            sumMie      = sumMie + attenuation * densityMie;
        }

        const float32 cosine        = direction.dot( toSun );
        const float32 phaseRayleigh = 3.0f / ( 16.0f * MathUtil::kPi ) * ( 1.0f + cosine * cosine );
        const float32 squaredG      = _mieAnisotropy * _mieAnisotropy;
        const float32 phaseMie      = 3.0f / ( 8.0f * MathUtil::kPi ) * ( ( 1.0f - squaredG ) * ( 1.0f + cosine * cosine ) ) /
                                 ( ( 2.0f + squaredG ) * MathUtil::pow( MathUtil::max( 1.0f + squaredG - 2.0f * _mieAnisotropy * cosine, 1.0e-4f ), 1.5f ) );
        float3 radiance{ sumRayleigh._x * betaRayleigh._x * phaseRayleigh + sumMie._x * betaMie * phaseMie, sumRayleigh._y * betaRayleigh._y * phaseRayleigh + sumMie._y * betaMie * phaseMie,
                         sumRayleigh._z * betaRayleigh._z * phaseRayleigh + sumMie._z * betaMie * phaseMie };

        // 땅 — 해 빛을 받은 알베도가 시선 투과율만큼 줄어 눈에 온다(램버트, 1/π).
        if ( bHitsGround )
        {
            const float3  groundPosition = origin + direction * segmentEnd;
            const float3  normal         = groundPosition * ( 1.0f / groundPosition.getLength() );
            const float32 normalDotLight = MathUtil::max( normal.dot( toSun ), 0.0f );
            float32       sunRayleigh{ 0.0f };
            float32       sunMie{ 0.0f };
            if ( normalDotLight > 0.0f && Internal::computeSunOpticalDepth( *this, groundPosition, toSun, sunRayleigh, sunMie ) )
            {
                const float32 opticalRayleigh = viewRayleigh + sunRayleigh;
                const float32 opticalMie      = viewMie + sunMie;
                const float32 scale           = normalDotLight / MathUtil::kPi;
                radiance._x += groundAlbedo._x * scale * MathUtil::exp( -( betaRayleigh._x * opticalRayleigh + betaMieExtinction * opticalMie ) );
                radiance._y += groundAlbedo._y * scale * MathUtil::exp( -( betaRayleigh._y * opticalRayleigh + betaMieExtinction * opticalMie ) );
                radiance._z += groundAlbedo._z * scale * MathUtil::exp( -( betaRayleigh._z * opticalRayleigh + betaMieExtinction * opticalMie ) );
            }
        }
        return radiance;
    }

    void SkyGradient::setSun( const float3& lightTravelDirection, const float3& color )
    {
        const float32 length = lightTravelDirection.getLength();
        if ( length <= MathUtil::kEpsilon )
        {
            _sunColor = float3{};
            return;
        }
        _toSunDirection = lightTravelDirection * ( -1.0f / length );
        _sunColor       = color;
    }

    float3 SkyGradient::evaluateRadiance( const float3& direction ) const
    {
        const float32 length = MathUtil::sqrt( direction.dot( direction ) );
        if ( _mode == SkyMode::Atmosphere )
        {
            // 길이가 0 인 방향은 지평선(+x)이다. 해 색이 0 이면 산란에 곱해 0 이 된다(해 없는 하늘).
            const float3  unit       = length > 0.0f ? direction * ( 1.0f / length ) : float3{ 1.0f, 0.0f, 0.0f };
            const float3  scattering = _atmosphere.evaluateScattering( unit, _toSunDirection, _groundColor );
            const float32 scale      = _atmosphere._sunIntensity * _intensity;
            return float3{ scattering._x * _sunColor._x, scattering._y * _sunColor._y, scattering._z * _sunColor._z } * scale;
        }
        // sky.hlsl 의 evaluateSky 와 같은 식이다. 길이가 0 인 방향은 y 가 0(지평선)이다.
        const float32 y             = length > 0.0f ? MathUtil::clamp( direction._y / length, -1.0f, 1.0f ) : 0.0f;
        const float32 horizonWeight = MathUtil::pow( 1.0f - MathUtil::abs( y ), _horizonExponent );
        const float3& poleColor     = y >= 0.0f ? _zenithColor : _groundColor;
        const float3  gradientColor = float3::lerp( poleColor, _horizonColor, horizonWeight );
        return gradientColor * _intensity;
    }
} // namespace sw
