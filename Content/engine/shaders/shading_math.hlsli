/**
 * shading_math.hlsli — 셰이딩 모델의 스칼라 식(프레넬 · GGX · 시엔 · 박막 · 감싼 확산 · 머리결 · 선 덮임 · 눈동자) 한 벌. 셰이더와 C++ 단위 테스트가 **같은 파일**을 읽는다.
 *
 * 셰이더는 lighting.hlsli · shading_model.hlsli · appearance_*.hlsli 가 include 하고, C++ 는 `Engine/Graphics/Lighting/ShadingMath.h` 가 include 해 값을 잰다
 * (두 벌로 두면 테스트가 셰이더가 아닌 사본을 잰다). 그래서 이 파일은 두 언어가 함께 받는 모양만 쓴다:
 *   - 인자와 반환은 스칼라 `float` 뿐(벡터 · `out` 이 없다 — 벡터 식은 부르는 쪽이 채널마다 부른다).
 *   - 내장 함수는 `sqrt` · `pow` · `cos` · `exp2` · `log2` · `abs` · `floor` · `frac` · `saturate` · `lerp` · `max` · `min` · `clamp` 만(C++ 쪽은 ShadingMath.h 가 같은 이름을 둔다).
 *   - 공유 헤더 함수는 구조체 멤버다(NamingScripts HLSL). 바깥 정의의 `static` 은 C++ 에서 `inline` 이어야 하므로 `SW_SHADING_MATH_DEFINITION` 이 고른다.
 * 단위: 엔진의 확산 항은 π 로 나누지 않는다(lighting.hlsli) — 분포 함수는 `…TimesPi`(D·π)로 낸다.
 * 비교(기억에 의존): 언리얼 BRDF.ush 의 D_GGX · Vis_SmithJointApprox · D_Charlie · Vis_Ashikhmin(우리는 Neubelt) · F_Schlick, 박막은 Belcour · Barla 2017 의
 * 에어리 식을 파장 셋(R 650 · G 532 · B 450 nm)에서 잰 값싼 판이다.
 */

#ifndef SW_ENGINE_SHADINGMATH_HLSLI
#define SW_ENGINE_SHADINGMATH_HLSLI

#if defined( __cplusplus )
    #define SW_SHADING_MATH_DEFINITION inline // 헤더의 바깥 정의 — 번역 단위마다 한 벌
namespace sw::shadermath
{
#else
    #define SW_SHADING_MATH_DEFINITION static // FXC 는 선언과 정의의 static 이 다르면 거절한다
#endif

    // common.hlsli 의 kPi 와 같은 값 — C++ 가 읽는 이 파일은 셰이더 공용 헤더를 include 할 수 없다.
    static const float kShadingMathPi = 3.14159265358979323846f;
    // 분모의 하한 — 0 으로 나누지 않는다.
    static const float kShadingMathEpsilon = 1e-5f;

    // 눈동자 모양(머티리얼 칸 `_pupilShape` 의 값). 값을 바꾸면 라이브러리 프리셋이 다른 모양이 된다.
    static const float kPupilShapeRound           = 0.0f; ///< 둥근 눈동자(사람 · 개 · 새 · 물고기)
    static const float kPupilShapeVerticalSlit    = 1.0f; ///< 세로 틈(고양이 · 뱀 · 도마뱀)
    static const float kPupilShapeHorizontalBar   = 2.0f; ///< 가로 막대(염소 · 말 · 양)
    static const float kPupilShapeCephalopod      = 3.0f; ///< W · U 자(갑오징어 · 문어)
    static const float kPupilShapeCount           = 4.0f;
    static const float kPupilShapeSlitMinWidth    = 0.08f; ///< 틈 · 막대 · W 자가 가장 좁을 때의 반폭(긴 축 반지름 비)
    static const float kPupilShapeCephalopodCurve = 1.6f;  ///< W 자 곡선의 굽음(x² 계수)

    struct ShadingMath
    {
        // --- 프레넬 · 반사 ---
        static float computeSchlickWeight( float cosine );                    ///< (1 - cos)^5
        static float computeFresnelSchlick( float f0, float cosine );         ///< 슐릭 근사
        static float computeDielectricF0( float ior );                        ///< 공기에서 굴절률 ior 유전체로의 수직 반사율 ((n-1)/(n+1))²
        static float computeRefractedTangent( float sinIncident, float ior ); ///< 굴절된 빛의 tan θt(공기 → ior). 각막 아래 홍채 시차의 기울기다
        static float computeDistributionGGXTimesPi( float normalDotHalf, float alpha );
        static float computeVisibilitySmithGGX( float normalDotLight, float normalDotView, float alpha );
        static float computeAlphaTangent( float alpha, float anisotropy );   ///< 비등방 GGX 의 탄젠트 쪽 α(Kulla · 이미지웍스 매핑)
        static float computeAlphaBitangent( float alpha, float anisotropy ); ///< 비등방 GGX 의 바이탄젠트 쪽 α
        static float computeDistributionAnisotropicGGXTimesPi( float normalDotHalf, float tangentDotHalf, float bitangentDotHalf, float alphaTangent, float alphaBitangent );
        static float computeVisibilityAnisotropicGGX( float normalDotLight, float normalDotView, float tangentDotView, float bitangentDotView, float tangentDotLight,
                                                      float bitangentDotLight, float alphaTangent, float alphaBitangent );
        // --- 천 · 털 ---
        static float computeDistributionCharlieTimesPi( float normalDotHalf, float sheenAlpha ); ///< Estevez · Kulla 2017 시엔 분포(정규화됨)
        static float computeVisibilityNeubelt( float normalDotLight, float normalDotView );      ///< Neubelt · Pettineo 2013 천 가시성
        static float computeStrandSpecular( float tangentDotHalf, float exponent );              ///< Kajiya · Kay 머리결 반사(sin^n)
        static float computeStrandExponent( float roughness );                                   ///< 거칠기 → 머리결 반사 지수
        // --- 확산 ---
        static float computeWrappedDiffuse( float normalDotLight, float wrap );      ///< 에너지 보존 감싼 확산(N·L 이 -wrap 까지 빛을 받는다, 적분은 램버트와 같다)
        static float computeDiffuseTransmission( float normalDotLight, float wrap ); ///< 얇은 면의 뒤에서 오는 빛(커튼 · 잎 · 귀)
        // --- 박막 간섭 ---
        static float computeThinFilmReflectance( float cosine, float thicknessNm, float filmIor, float baseIor, float wavelengthNm );
        // --- 무늬 ---
        static float computeLinePrefilter( float position, float halfWidth );                 ///< 주기 1 의 선 펄스열의 원시 함수(상자 거르기의 바탕)
        static float computeLineCoverage( float position, float halfWidth, float footprint ); ///< 픽셀 발자국 상자로 거른 선 덮임(밉에 따라 밀도로 수렴)
        // --- 눈 ---
        static float computePupilRadius( float minRadius, float maxRadius, float dilation );                                  ///< 확장 0..1 → 동공 반지름(홍채 반지름 비)
        static float computeDilationFromLuminance( float luminance, float darkLuminance, float brightLuminance );             ///< 장면 밝기 → 확장(어두우면 1)
        static float computePupilDistance( float shape, float x, float y, float minRadius, float maxRadius, float dilation ); ///< 홍채 좌표(홍채 반지름 1)에서 동공 경계까지의 부호 거리(안이 음수)
    };

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeSchlickWeight( float cosine )
    {
        const float oneMinusCosine = 1.0f - saturate( cosine );
        const float squared        = oneMinusCosine * oneMinusCosine;
        return squared * squared * oneMinusCosine; // pow 는 DXC 가 log · exp 로 낸다
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeFresnelSchlick( float f0, float cosine )
    {
        return f0 + ( 1.0f - f0 ) * ShadingMath::computeSchlickWeight( cosine );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDielectricF0( float ior )
    {
        const float ratio = ( ior - 1.0f ) / max( ior + 1.0f, kShadingMathEpsilon );
        return ratio * ratio;
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeRefractedTangent( float sinIncident, float ior )
    {
        // 스넬: sin θt = sin θi / n. 공기에서 더 짙은 매질로 가므로 전반사는 없다(n ≥ 1 을 부르는 쪽이 지킨다).
        const float sinRefracted = saturate( sinIncident / max( ior, 1.0f ) );
        const float cosRefracted = sqrt( max( 1.0f - sinRefracted * sinRefracted, kShadingMathEpsilon ) );
        return sinRefracted / cosRefracted;
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDistributionGGXTimesPi( float normalDotHalf, float alpha )
    {
        // D · π — 분포의 1/π 와 엔진 단위의 π 가 지워진다(lighting.hlsli SurfacePBR::evaluateSpecular 가 이 식을 부른다).
        const float alphaSquared = alpha * alpha;
        const float denominator  = normalDotHalf * normalDotHalf * ( alphaSquared - 1.0f ) + 1.0f;
        return alphaSquared / max( denominator * denominator, 1e-8f );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeVisibilitySmithGGX( float normalDotLight, float normalDotView, float alpha )
    {
        // 언리얼 Vis_SmithJointApprox — 높이 상관 스미스의 근사.
        return 0.5f / max( normalDotLight * ( normalDotView * ( 1.0f - alpha ) + alpha ) + normalDotView * ( normalDotLight * ( 1.0f - alpha ) + alpha ), kShadingMathEpsilon );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeAlphaTangent( float alpha, float anisotropy )
    {
        return max( alpha * ( 1.0f + anisotropy ), 1e-3f );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeAlphaBitangent( float alpha, float anisotropy )
    {
        return max( alpha * ( 1.0f - anisotropy ), 1e-3f );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDistributionAnisotropicGGXTimesPi( float normalDotHalf, float tangentDotHalf, float bitangentDotHalf, float alphaTangent,
                                                                                            float alphaBitangent )
    {
        // Burley 2012 비등방 GGX. α 가 같으면 등방 GGX 와 같다(테스트가 잰다).
        const float x           = tangentDotHalf / alphaTangent;
        const float y           = bitangentDotHalf / alphaBitangent;
        const float denominator = x * x + y * y + normalDotHalf * normalDotHalf;
        return 1.0f / max( alphaTangent * alphaBitangent * denominator * denominator, 1e-8f );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeVisibilityAnisotropicGGX( float normalDotLight, float normalDotView, float tangentDotView, float bitangentDotView,
                                                                                   float tangentDotLight, float bitangentDotLight, float alphaTangent, float alphaBitangent )
    {
        // Heitz 2014 높이 상관 비등방 스미스.
        const float lambdaView  = normalDotLight * sqrt( alphaTangent * alphaTangent * tangentDotView * tangentDotView +
                                                         alphaBitangent * alphaBitangent * bitangentDotView * bitangentDotView + normalDotView * normalDotView );
        const float lambdaLight = normalDotView * sqrt( alphaTangent * alphaTangent * tangentDotLight * tangentDotLight +
                                                        alphaBitangent * alphaBitangent * bitangentDotLight * bitangentDotLight + normalDotLight * normalDotLight );
        return 0.5f / max( lambdaView + lambdaLight, kShadingMathEpsilon );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDistributionCharlieTimesPi( float normalDotHalf, float sheenAlpha )
    {
        // D = (2 + 1/α) sin^(1/α)θ / 2π. × π 하면 (2 + 1/α) sin^(1/α)θ / 2. ∫ D cosθ dω = 1 이다(테스트가 잰다).
        const float inverseAlpha = 1.0f / max( sheenAlpha, 1e-3f );
        const float sinSquared   = max( 1.0f - normalDotHalf * normalDotHalf, 0.0f );
        return ( 2.0f + inverseAlpha ) * pow( sinSquared, inverseAlpha * 0.5f ) * 0.5f;
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeVisibilityNeubelt( float normalDotLight, float normalDotView )
    {
        return 1.0f / max( 4.0f * ( normalDotLight + normalDotView - normalDotLight * normalDotView ), kShadingMathEpsilon );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeStrandSpecular( float tangentDotHalf, float exponent )
    {
        const float sinTangentHalf = sqrt( max( 1.0f - tangentDotHalf * tangentDotHalf, 0.0f ) );
        return pow( sinTangentHalf, exponent );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeStrandExponent( float roughness )
    {
        // 블린-퐁 지수와 GGX α 의 흔한 대응 2/α² - 2 를 머리결에 쓴다. 거칠기 0 이 무한대가 되지 않게 하한을 건다.
        const float alpha = max( roughness * roughness, 0.02f );
        return max( 2.0f / ( alpha * alpha ) - 2.0f, 1.0f );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeWrappedDiffuse( float normalDotLight, float wrap )
    {
        // (N·L + w) / (1 + w)². 구 전체 적분이 π 로 램버트와 같다 — 빛을 둘레로 옮길 뿐 더하지 않는다.
        const float onePlusWrap = 1.0f + max( wrap, 0.0f );
        return max( normalDotLight + wrap, 0.0f ) / ( onePlusWrap * onePlusWrap );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDiffuseTransmission( float normalDotLight, float wrap )
    {
        // 뒤에서 오는 빛 — 면의 반대쪽에서 같은 감싼 확산이다(얇은 면은 앞뒤가 같은 매질이다).
        return ShadingMath::computeWrappedDiffuse( -normalDotLight, wrap );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeThinFilmReflectance( float cosine, float thicknessNm, float filmIor, float baseIor, float wavelengthNm )
    {
        // 공기(1) / 막(filmIor) / 바탕(baseIor, 유전체) 세 층의 에어리 반사율. s · p 편광을 따로 재 평균한다.
        const float cos1        = saturate( cosine );
        const float sin1Squared = 1.0f - cos1 * cos1;
        const float cos2        = sqrt( max( 1.0f - sin1Squared / ( filmIor * filmIor ), 0.0f ) );
        const float cos3        = sqrt( max( 1.0f - sin1Squared / ( baseIor * baseIor ), 0.0f ) );
        const float r12s        = ( cos1 - filmIor * cos2 ) / max( cos1 + filmIor * cos2, kShadingMathEpsilon );
        const float r12p        = ( filmIor * cos1 - cos2 ) / max( filmIor * cos1 + cos2, kShadingMathEpsilon );
        const float r23s        = ( filmIor * cos2 - baseIor * cos3 ) / max( filmIor * cos2 + baseIor * cos3, kShadingMathEpsilon );
        const float r23p        = ( baseIor * cos2 - filmIor * cos3 ) / max( baseIor * cos2 + filmIor * cos3, kShadingMathEpsilon );
        // 막 안을 한 번 오가는 위상차 δ = 4π n₂ d cos θ₂ / λ.
        const float cosPhase = cos( 4.0f * kShadingMathPi * filmIor * thicknessNm * cos2 / max( wavelengthNm, 1.0f ) );
        const float reflectS = ( r12s * r12s + r23s * r23s + 2.0f * r12s * r23s * cosPhase ) / max( 1.0f + r12s * r12s * r23s * r23s + 2.0f * r12s * r23s * cosPhase, kShadingMathEpsilon );
        const float reflectP = ( r12p * r12p + r23p * r23p + 2.0f * r12p * r23p * cosPhase ) / max( 1.0f + r12p * r12p * r23p * r23p + 2.0f * r12p * r23p * cosPhase, kShadingMathEpsilon );
        return saturate( 0.5f * ( reflectS + reflectP ) );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeLinePrefilter( float position, float halfWidth )
    {
        // 정수마다 가운데를 둔 폭 2w 펄스열의 원시 함수 F(x) = k·2w + clamp(t, -w, w) + w (k = floor(x + ½), t = frac(x + ½) - ½). 이어져 있다.
        const float shifted = position + 0.5f;
        const float cell    = floor( shifted );
        const float local   = shifted - cell - 0.5f;
        return cell * 2.0f * halfWidth + clamp( local, -halfWidth, halfWidth ) + halfWidth;
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeLineCoverage( float position, float halfWidth, float footprint )
    {
        // 상자 거르기 = (F(x + f/2) - F(x - f/2)) / f. 발자국이 칸만큼 커지면 정확히 선 밀도 2w 가 된다 — 멀리서 그물이 깜박이거나 사라지지 않는다
        // (밉맵이 하는 일을 해석적으로 한다). 발자국이 아주 작으면 계단 함수에 가깝다.
        const float halfFootprint = max( footprint, 1e-4f ) * 0.5f;
        const float covered       = ShadingMath::computeLinePrefilter( position + halfFootprint, halfWidth ) - ShadingMath::computeLinePrefilter( position - halfFootprint, halfWidth );
        return saturate( covered / ( 2.0f * halfFootprint ) );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computePupilRadius( float minRadius, float maxRadius, float dilation )
    {
        return lerp( minRadius, maxRadius, saturate( dilation ) );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computeDilationFromLuminance( float luminance, float darkLuminance, float brightLuminance )
    {
        // 동공은 밝기의 로그에 반응한다(Moon · Spencer 1944 의 모양). 어두운 끝에서 1(활짝), 밝은 끝에서 0(좁게).
        const float logDark   = log2( max( darkLuminance, 1e-6f ) );
        const float logBright = log2( max( brightLuminance, darkLuminance * 1.001f + 1e-6f ) );
        const float t         = ( log2( max( luminance, 1e-6f ) ) - logDark ) / ( logBright - logDark );
        return 1.0f - saturate( t );
    }

    SW_SHADING_MATH_DEFINITION float ShadingMath::computePupilDistance( float shape, float x, float y, float minRadius, float maxRadius, float dilation )
    {
        // 모양마다 확장이 여는 축이 다르다 — 틈 · 막대 · W 자는 짧은 축(두께)만 열리고 긴 축은 maxRadius 를 따른다(고양이 눈은 밝으면 세로 실이 된다).
        const float openness   = saturate( dilation );
        const float longRadius = max( maxRadius, kShadingMathEpsilon );
        const float slitWidth  = kPupilShapeSlitMinWidth * longRadius;
        float       distance   = sqrt( x * x + y * y ) - ShadingMath::computePupilRadius( minRadius, maxRadius, openness );
        if ( abs( shape - kPupilShapeVerticalSlit ) < 0.5f )
        {
            // 세로 타원: 반높이는 긴 축, 반폭은 좁은 틈에서 원까지.
            const float halfHeight = longRadius;
            const float halfWidth  = lerp( slitWidth, halfHeight, openness * openness );
            const float scaled     = sqrt( ( x / halfWidth ) * ( x / halfWidth ) + ( y / halfHeight ) * ( y / halfHeight ) );
            distance               = ( scaled - 1.0f ) * halfWidth;
        }
        else if ( abs( shape - kPupilShapeHorizontalBar ) < 0.5f )
        {
            // 가로 둥근 사각형: 반폭은 긴 축, 반높이가 열린다.
            const float halfWidth  = longRadius;
            const float halfHeight = lerp( slitWidth, longRadius * 0.6f, openness * openness );
            const float corner     = halfHeight * 0.8f;
            const float edgeX      = abs( x ) - ( halfWidth - corner );
            const float edgeY      = abs( y ) - ( halfHeight - corner );
            const float outsideX   = max( edgeX, 0.0f );
            const float outsideY   = max( edgeY, 0.0f );
            distance               = sqrt( outsideX * outsideX + outsideY * outsideY ) + min( max( edgeX, edgeY ), 0.0f ) - corner;
        }
        else if ( abs( shape - kPupilShapeCephalopod ) < 0.5f )
        {
            // W 자 띠: 곡선 y = k(|x| - w/2)² - k(w/2)²/2 (가운데가 솟고 양쪽이 처진다)에서의 거리(기울기로 나눈 근사) - 두께. 가로 끝은 반폭에서 자른다.
            const float halfWidth  = longRadius;
            const float thickness  = lerp( slitWidth, 0.35f * longRadius, openness );
            const float fromValley = abs( x ) - 0.5f * halfWidth;
            const float curveY     = kPupilShapeCephalopodCurve * ( fromValley * fromValley - 0.125f * halfWidth * halfWidth );
            const float slope      = 2.0f * kPupilShapeCephalopodCurve * fromValley;
            const float band       = abs( y - curveY ) / sqrt( 1.0f + slope * slope ) - thickness;
            distance               = max( band, abs( x ) - halfWidth );
        }
        return distance;
    }

#if defined( __cplusplus )
} // namespace sw::shadermath
#endif

#endif // SW_ENGINE_SHADINGMATH_HLSLI
