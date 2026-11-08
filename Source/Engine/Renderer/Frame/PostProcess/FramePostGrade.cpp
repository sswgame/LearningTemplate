#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/View/PostProcessValues.h"

namespace sw
{
    namespace
    {
        struct FramePostGradeInternal
        {
            /// @brief 3x3 행렬(행 셋)입니다. 화이트 밸런스 계산에만 쓴다.
            struct Basis
            {
                float3 _arrRow[3];
            };

            /// @brief 선형 Rec.709(sRGB 원색, D65) → CIE XYZ.
            static constexpr float32 kArrRGBToXYZ[3][3] = {
                {0.4124564f, 0.3575761f, 0.1804375f},
                {0.2126729f, 0.7151522f, 0.0721750f},
                {0.0193339f, 0.1191920f, 0.9503041f},
            };
            /// @brief CIE XYZ → 선형 Rec.709.
            static constexpr float32 kArrXYZToRGB[3][3] = {
                { 3.2404542f, -1.5371385f, -0.4985314f},
                {-0.9692660f,  1.8760108f,  0.0415560f},
                { 0.0556434f, -0.2040259f,  1.0572252f},
            };
            /// @brief Bradford 원뿔 반응(XYZ → LMS)과 그 역행렬.
            static constexpr float32 kArrBradford[3][3] = {
                { 0.8951f,  0.2664f, -0.1614f},
                {-0.7502f,  1.7135f,  0.0367f},
                { 0.0389f, -0.0685f,  1.0296f},
            };
            static constexpr float32 kArrBradfordInverse[3][3] = {
                { 0.9869929f, -0.1470543f, 0.1599627f},
                { 0.4323053f,  0.5183603f, 0.0492912f},
                {-0.0085287f,  0.0400428f, 0.9684867f},
            };
            /// @brief 플랑크 궤적 근사(Kim 외 3차식)의 x 계수 — x = c0 / T³ + c1 / T² + c2 / T + c3. 4000 K 아래와 위가 다르다.
            static constexpr float4 kPlanckXBelowSplit{ -0.2661239e9f, -0.2343589e6f, 0.8776956e3f, 0.179910f };
            static constexpr float4 kPlanckXAboveSplit{ -3.0258469e9f, 2.1070379e6f, 0.2226347e3f, 0.240390f };
            /// @brief 같은 근사의 y 계수 — y = c0 x³ + c1 x² + c2 x + c3. 2222 K 아래 · 4000 K 아래 · 그 위가 다르다.
            static constexpr float4 kPlanckYBelowLowSplit{ -1.1063814f, -1.34811020f, 2.18555832f, -0.20219683f };
            static constexpr float4 kPlanckYBelowSplit{ -0.9549476f, -1.37418593f, 2.09137015f, -0.16748867f };
            static constexpr float4 kPlanckYAboveSplit{ 3.0817580f, -5.87338670f, 3.75112997f, -0.37001483f };
            /// @brief 근사의 구간 경계(K)입니다.
            static constexpr float32 kPlanckSplit    = 4000.0f;
            static constexpr float32 kPlanckLowSplit = 2222.0f;
            /// @brief CIE 1931 xy ↔ 1960 UCS uv 의 계수 — u = 4x / d, v = 6y / d, d = -2x + 12y + 3 · x = 3u / d', y = 2v / d', d' = 2u - 8v + 4.
            static constexpr float4 kXyToUcs{ 4.0f, 6.0f, 12.0f, 3.0f };
            static constexpr float4 kUcsToXy{ 3.0f, 8.0f, 4.0f, 0.0f };
            /// @brief 플랑크 궤적 근사(Kim 외)가 맞는 온도 범위(K)입니다.
            static constexpr float32 kMinTemperature = 1667.0f;
            static constexpr float32 kMaxTemperature = 25000.0f;
            /// @brief 기준 흰 점 온도(K)입니다 — 이 온도 · 틴트 0 이 중립이다.
            static constexpr float32 kNeutralTemperature = 6500.0f;
            /// @brief 틴트 1 이 옮기는 1960 UCS v 거리입니다(언리얼 WhiteBalance 의 Duv 배율과 같은 크기).
            static constexpr float32 kTintScale = 0.05f;

            static float32 dot( const float3& a, const float3& b ) { return a._x * b._x + a._y * b._y + a._z * b._z; }

            static Basis makeBasis( const float32 ( &arrValue )[3][3] )
            {
                Basis basis{};
                for ( uint32 row = 0; row < 3; ++row )
                {
                    basis._arrRow[row] = float3{ arrValue[row][0], arrValue[row][1], arrValue[row][2] };
                }
                return basis;
            }

            static float3 transform( const Basis& m, const float3& v ) { return float3{ dot( m._arrRow[0], v ), dot( m._arrRow[1], v ), dot( m._arrRow[2], v ) }; }

            static Basis multiply( const Basis& a, const Basis& b )
            {
                Basis        out{};
                const float3 column0{ b._arrRow[0]._x, b._arrRow[1]._x, b._arrRow[2]._x };
                const float3 column1{ b._arrRow[0]._y, b._arrRow[1]._y, b._arrRow[2]._y };
                const float3 column2{ b._arrRow[0]._z, b._arrRow[1]._z, b._arrRow[2]._z };
                for ( uint32 row = 0; row < 3; ++row )
                {
                    out._arrRow[row] = float3{ dot( a._arrRow[row], column0 ), dot( a._arrRow[row], column1 ), dot( a._arrRow[row], column2 ) };
                }
                return out;
            }

            /** @brief 3차식 c0 v³ + c1 v² + c2 v + c3 입니다. */
            static float32 evaluateCubic( const float4& coefficient, float32 value )
            {
                return ( ( coefficient._x * value + coefficient._y ) * value + coefficient._z ) * value + coefficient._w;
            }

            /** @brief 온도 @p temperature · 틴트 @p tint 의 흰 점(XYZ, Y = 1)입니다. */
            static float3 computeWhitePoint( float32 temperature, float32 tint )
            {
                const float32 t = MathUtil::clamp( temperature, kMinTemperature, kMaxTemperature );
                // x 식은 1 / T 의 3차식이다.
                const float32 x = evaluateCubic( t <= kPlanckSplit ? kPlanckXBelowSplit : kPlanckXAboveSplit, 1.0f / t );
                const float32 y = evaluateCubic( t <= kPlanckLowSplit ? kPlanckYBelowLowSplit : ( t <= kPlanckSplit ? kPlanckYBelowSplit : kPlanckYAboveSplit ), x );
                // 틴트는 1960 UCS 의 v 로 옮긴다(위가 초록). 양수는 장면 빛이 초록이라는 뜻이라 적응이 초록을 빼 자홍 쪽이 된다.
                const float32 denominator = -2.0f * x + kXyToUcs._z * y + kXyToUcs._w;
                const float32 u           = kXyToUcs._x * x / denominator;
                const float32 v           = kXyToUcs._y * y / denominator + tint * kTintScale;
                const float32 back        = 2.0f * u - kUcsToXy._y * v + kUcsToXy._z;
                const float32 whiteX      = kUcsToXy._x * u / back;
                const float32 whiteY      = 2.0f * v / back;
                return float3{ whiteX / whiteY, 1.0f, ( 1.0f - whiteX - whiteY ) / whiteY };
            }

            /** @brief 범위 하나의 rgb 곱 값(전역 × 범위, w 를 rgb 에 곱한다)입니다. */
            static float4 combineScale( const float4& global, const float4& range )
            {
                const float32 w = global._w * range._w;
                return float4{ global._x * range._x * w, global._y * range._y * w, global._z * range._z * w, 0.0f };
            }

            /** @brief 범위 하나의 rgb 더할 값(전역 + 범위, w 를 rgb 에 더한다)입니다. */
            static float4 combineOffset( const float4& global, const float4& range )
            {
                const float32 w = global._w + range._w;
                return float4{ global._x + range._x + w, global._y + range._y + w, global._z + range._z + w, 0.0f };
            }

            static bool isUnit( const float4& value ) { return value._x == 1.0f && value._y == 1.0f && value._z == 1.0f; }

            static bool isZero( const float4& value ) { return value._x == 0.0f && value._y == 0.0f && value._z == 0.0f; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PostGradeConstants::computeWhiteBalance( float32 temperature, float32 tint, float4 ( &outArrRow )[3] )
    {
        using Internal                   = FramePostGradeInternal;
        const Internal::Basis bradford   = Internal::makeBasis( Internal::kArrBradford );
        const float3          sourceLms  = Internal::transform( bradford, Internal::computeWhitePoint( temperature, tint ) );
        const float3          neutralLms = Internal::transform( bradford, Internal::computeWhitePoint( Internal::kNeutralTemperature, 0.0f ) );
        // 장면 빛의 흰 점을 기준 흰 점으로 옮긴다: RGB → XYZ → LMS → 원뿔마다 비율 → XYZ → RGB.
        Internal::Basis scale{};
        scale._arrRow[0]            = float3{ neutralLms._x / sourceLms._x, 0.0f, 0.0f };
        scale._arrRow[1]            = float3{ 0.0f, neutralLms._y / sourceLms._y, 0.0f };
        scale._arrRow[2]            = float3{ 0.0f, 0.0f, neutralLms._z / sourceLms._z };
        const Internal::Basis cone  = Internal::multiply( Internal::makeBasis( Internal::kArrBradfordInverse ), Internal::multiply( scale, bradford ) );
        const Internal::Basis total = Internal::multiply( Internal::makeBasis( Internal::kArrXYZToRGB ), Internal::multiply( cone, Internal::makeBasis( Internal::kArrRGBToXYZ ) ) );
        for ( uint32 row = 0; row < 3; ++row )
        {
            outArrRow[row] = float4{ total._arrRow[row], 0.0f };
        }
    }

    PostGradeConstants PostGradeConstants::make( const PostProcessValues& values, uint32 lutSize )
    {
        using Internal = FramePostGradeInternal;
        PostGradeConstants constants{};
        const bool         bWhiteBalance = values._whiteTemperature != Internal::kNeutralTemperature || values._whiteTint != 0.0f;
        if ( bWhiteBalance )
            computeWhiteBalance( values._whiteTemperature, values._whiteTint, constants._arrWhiteBalance );
        else
        {
            // 중립은 계산하지 않고 단위 행렬이다(근사식의 반올림이 그림을 바꾸지 않게).
            constants._arrWhiteBalance[0] = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
            constants._arrWhiteBalance[1] = float4{ 0.0f, 1.0f, 0.0f, 0.0f };
            constants._arrWhiteBalance[2] = float4{ 0.0f, 0.0f, 1.0f, 0.0f };
        }

        const float4* arrRangeValue[kRangeCount][kRangeFieldCount] = {
            {   &values._shadowsSaturation,    &values._shadowsContrast,    &values._shadowsGamma,    &values._shadowsGain,    &values._shadowsOffset},
            {  &values._midtonesSaturation,   &values._midtonesContrast,   &values._midtonesGamma,   &values._midtonesGain,   &values._midtonesOffset},
            {&values._highlightsSaturation, &values._highlightsContrast, &values._highlightsGamma, &values._highlightsGain, &values._highlightsOffset},
        };
        const float4*    arrGlobalValue[kRangeFieldCount] = { &values._colorSaturation, &values._colorContrast, &values._colorGamma, &values._colorGain, &values._colorOffset };
        constexpr uint32 kOffsetField                     = kRangeFieldCount - 1;
        bool             bGrading                         = bWhiteBalance;
        for ( uint32 range = 0; range < kRangeCount; ++range )
        {
            for ( uint32 field = 0; field < kRangeFieldCount; ++field )
            {
                const bool    bOffset                                 = field == kOffsetField;
                const float4& global                                  = *arrGlobalValue[field];
                const float4& local                                   = *arrRangeValue[range][field];
                const float4  value                                   = bOffset ? Internal::combineOffset( global, local ) : Internal::combineScale( global, local );
                constants._arrRange[range * kRangeFieldCount + field] = value;
                bGrading                                              = bGrading || ( bOffset ? Internal::isZero( value ) == false : Internal::isUnit( value ) == false );
            }
        }
        constants._params = float4{ MathUtil::saturate( values._shadowsMax ), MathUtil::saturate( values._highlightsMin ), bGrading ? 1.0f : 0.0f, 0.0f };

        // LUT 는 읽힌 것이 있고 세기가 0 보다 클 때만 건다 — 읽는 중이면 이번 프레임은 LUT 없이 그린다(못 읽으면 텍스처 쪽이 오류를 남긴다).
        const float32 lutIntensity = lutSize > 0 ? MathUtil::saturate( values._colorGradingLUTIntensity ) : 0.0f;
        constants._lut             = float4{ lutIntensity > 0.0f ? static_cast<float32>( lutSize ) : 0.0f, lutIntensity, 0.0f, 0.0f };
        return constants;
    }

    float2 PostGradeConstants::computeLUTUV( const float3& color, uint32 lutSize, uint32 slice )
    {
        const float32 size  = static_cast<float32>( lutSize );
        const float32 red   = MathUtil::saturate( color._x ) * ( size - 1.0f );
        const float32 green = MathUtil::saturate( color._y ) * ( size - 1.0f );
        return float2{ ( static_cast<float32>( slice ) * size + red + 0.5f ) / ( size * size ), ( green + 0.5f ) / size };
    }
} // namespace sw
