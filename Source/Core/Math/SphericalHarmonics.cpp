#include "pch.h"

#include "Core/Math/SphericalHarmonics.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"

namespace sw
{
    namespace
    {
        /** @brief 실수 SH 기저의 정규화 상수와 코사인 로브 컨볼루션 비율입니다. π 계열은 MathUtil 에서만 가져온다. */
        struct SphericalHarmonicsInternal
        {
            /** @brief 1 / (2√π) — L0 기저입니다. */
            static float32 computeBand0() { return 0.5f / MathUtil::sqrt( MathUtil::kPi ); }
            /** @brief √3 / (2√π) — L1 기저입니다. */
            static float32 computeBand1() { return MathUtil::sqrt( 3.0f ) * computeBand0(); }
            /** @brief √15 / (2√π) — L2 의 xy · yz · xz 기저입니다. */
            static float32 computeBand2Cross() { return MathUtil::sqrt( 15.0f ) * computeBand0(); }
            /** @brief √5 / (4√π) — L2 의 (3z² − 1) 기저입니다. */
            static float32 computeBand2Zonal() { return MathUtil::sqrt( 5.0f ) * computeBand0() * 0.5f; }
            /** @brief √15 / (4√π) — L2 의 (x² − y²) 기저입니다. */
            static float32 computeBand2Square() { return computeBand2Cross() * 0.5f; }

            /// @brief 코사인 로브 컨볼루션 Â_l / π — 차수 0 · 1 · 2 (Ramamoorthi-Hanrahan 2001).
            static constexpr float32 kLobe0 = 1.0f;
            static constexpr float32 kLobe1 = 2.0f / 3.0f;
            static constexpr float32 kLobe2 = 0.25f;

            /** @brief 계수 @p basisIndex 의 RGB 입니다. */
            static float3 loadColor( const SH9Color& color, uint32 basisIndex )
            {
                const uint32 first = basisIndex * SH9Color::kChannelCount;
                return float3{ color._arrCoefficient[first], color._arrCoefficient[first + 1], color._arrCoefficient[first + 2] };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SH9Color::computeBasis( const float3& direction, float32 ( &outArrBasis )[kBasisCount] )
    {
        using Internal      = SphericalHarmonicsInternal;
        const float32 dirX  = direction._x;
        const float32 dirY  = direction._y;
        const float32 dirZ  = direction._z;
        const float32 band1 = Internal::computeBand1();
        const float32 cross = Internal::computeBand2Cross();
        outArrBasis[0]      = Internal::computeBand0();
        outArrBasis[1]      = band1 * dirY;
        outArrBasis[2]      = band1 * dirZ;
        outArrBasis[3]      = band1 * dirX;
        outArrBasis[4]      = cross * dirX * dirY;
        outArrBasis[5]      = cross * dirY * dirZ;
        outArrBasis[6]      = Internal::computeBand2Zonal() * ( 3.0f * dirZ * dirZ - 1.0f );
        outArrBasis[7]      = cross * dirX * dirZ;
        outArrBasis[8]      = Internal::computeBand2Square() * ( dirX * dirX - dirY * dirY );
    }

    SH9Color SH9Color::makeConstant( const float3& radiance )
    {
        // 상수 함수는 L0 만 있다 — ∫ c · Y0 dω = c · 4π · Y0 = c · 2√π.
        SH9Color      result{};
        const float32 scale       = 4.0f * MathUtil::kPi * SphericalHarmonicsInternal::computeBand0();
        result._arrCoefficient[0] = radiance._x * scale;
        result._arrCoefficient[1] = radiance._y * scale;
        result._arrCoefficient[2] = radiance._z * scale;
        return result;
    }

    SH9Color SH9Color::makeDirectional( const float3& toLight, const float3& irradiance )
    {
        SH9Color result{};
        result.addSample( toLight.normalize(), irradiance, 1.0f );
        return result;
    }

    float3 SH9Color::makeUniformSphereDirection( uint32& inoutState )
    {
        // z 를 [-1, 1] 에서, 방위각을 [0, 2π) 에서 고르게 고르면 구 위에서 고르다(아르키메데스의 모자 상자 정리).
        inoutState            = XorShift32::next( inoutState );
        const float32 height  = XorShift32::toUnit24( inoutState ) * 2.0f - 1.0f;
        inoutState            = XorShift32::next( inoutState );
        const float32 azimuth = XorShift32::toUnit24( inoutState ) * MathUtil::kTwoPi;
        const float32 ring    = MathUtil::sqrt( MathUtil::max( 1.0f - height * height, 0.0f ) );
        return float3{ ring * MathUtil::cos( azimuth ), ring * MathUtil::sin( azimuth ), height };
    }

    void SH9Color::addSample( const float3& direction, const float3& radiance, float32 weight )
    {
        float32 arrBasis[kBasisCount];
        computeBasis( direction, arrBasis );
        for ( uint32 basisIndex = 0; basisIndex < kBasisCount; ++basisIndex )
        {
            const float32 factor = arrBasis[basisIndex] * weight;
            _arrCoefficient[basisIndex * kChannelCount] += radiance._x * factor;
            _arrCoefficient[basisIndex * kChannelCount + 1] += radiance._y * factor;
            _arrCoefficient[basisIndex * kChannelCount + 2] += radiance._z * factor;
        }
    }

    void SH9Color::add( const SH9Color& other )
    {
        for ( uint32 index = 0; index < kCoefficientCount; ++index )
        {
            _arrCoefficient[index] += other._arrCoefficient[index];
        }
    }

    void SH9Color::scale( float32 factor )
    {
        for ( float32& coefficient : _arrCoefficient )
        {
            coefficient *= factor;
        }
    }

    float3 SH9Color::evaluateRadiance( const float3& direction ) const
    {
        float32 arrBasis[kBasisCount];
        computeBasis( direction, arrBasis );
        float3 result{ 0.0f, 0.0f, 0.0f };
        for ( uint32 basisIndex = 0; basisIndex < kBasisCount; ++basisIndex )
        {
            result += SphericalHarmonicsInternal::loadColor( *this, basisIndex ) * arrBasis[basisIndex];
        }
        return result;
    }

    float3 SH9Color::evaluateIrradiance( const float3& normal ) const
    {
        using Internal = SphericalHarmonicsInternal;
        float32 arrBasis[kBasisCount];
        computeBasis( normal, arrBasis );
        float3 result{ 0.0f, 0.0f, 0.0f };
        for ( uint32 basisIndex = 0; basisIndex < kBasisCount; ++basisIndex )
        {
            const float32 lobe = ( basisIndex == 0 ) ? Internal::kLobe0 : ( ( basisIndex < 4 ) ? Internal::kLobe1 : Internal::kLobe2 );
            result += Internal::loadColor( *this, basisIndex ) * ( arrBasis[basisIndex] * lobe );
        }
        return result * MathUtil::kPi;
    }

    void SH9Color::makeGPUElement( float4 ( &outArrElement )[kGPUFloat4Count] ) const
    {
        // 조도 / π = Σ (Â_l / π) L_lm Y_lm(n) 를 내적 셋으로 접는다(유니티 ShadeSH9 의 배치):
        //   A · (x, y, z, 1) + B · (xy, yz, z², xz) + C · (x² − y²).
        // (3z² − 1) 의 상수 −1 은 A.w 로, 3z² 은 B.z 로 나뉜다.
        using Internal       = SphericalHarmonicsInternal;
        const float32 band0  = Internal::computeBand0() * Internal::kLobe0;
        const float32 band1  = Internal::computeBand1() * Internal::kLobe1;
        const float32 cross  = Internal::computeBand2Cross() * Internal::kLobe2;
        const float32 zonal  = Internal::computeBand2Zonal() * Internal::kLobe2;
        const float32 square = Internal::computeBand2Square() * Internal::kLobe2;
        for ( uint32 channel = 0; channel < kChannelCount; ++channel )
        {
            const float32* pBasis      = _arrCoefficient + channel; // 계수 k 의 이 채널은 pBasis[k * 3]
            outArrElement[channel]     = float4{ pBasis[3 * kChannelCount] * band1, pBasis[1 * kChannelCount] * band1, pBasis[2 * kChannelCount] * band1,
                                             pBasis[0] * band0 - pBasis[6 * kChannelCount] * zonal };
            outArrElement[3 + channel] = float4{ pBasis[4 * kChannelCount] * cross, pBasis[5 * kChannelCount] * cross, pBasis[6 * kChannelCount] * zonal * 3.0f,
                                                 pBasis[7 * kChannelCount] * cross };
        }
        outArrElement[6] = float4{ _arrCoefficient[8 * kChannelCount] * square, _arrCoefficient[8 * kChannelCount + 1] * square, _arrCoefficient[8 * kChannelCount + 2] * square,
                                   0.0f };
    }
} // namespace sw
