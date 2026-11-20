/**
 * @file display_encode.hlsli
 * @brief 화면 출력 한 벌입니다 — 출력 휘도를 아는 톤 곡선과 출력 끝 인코딩(SDR 그대로 · HDR10 PQ · scRGB 선형). C++ 거울은 `DisplayEncoding`(Core/Math)이다.
 * @details 장면 패스(톤맵 · 후처리 체인 · Present · FSR RCAS)는 SDR 과 같은 [0,1] 출력 신호를 낸다 — 1 이 출력 최대 휘도다. SDR 은 그 신호가 곧 백버퍼 값이고(골든 그대로),
 *          HDR 은 프레임 끝의 DisplayEncode 패스가 신호를 nits 로 풀어(감마 2.2 — SDR 화면이 신호를 보이는 곡선) 인코딩한다. 그래서 RCAS 는 바꿀 것이 없다.
 *          톤 곡선은 Reinhard `x / (x + 1)` 의 어깨를 `(최대 휘도 / 종이 흰색)^(1/2.2)` 로 늘린 `x / (x + 어깨)` 다 — SDR 은 어깨가 1 이라 지금 식 그대로다.
 *          인코딩은 퍼뮤테이션 `SW_DISPLAY_OUTPUT`(0 SDR · 1 HDR10 · 2 scRGB)이 고른다(언리얼 `r.HDR.Display.OutputDevice`, 유니티 HDROutputSettings, Godot 4.5 HDR 출력과 같은 자리).
 */
#ifndef SW_ENGINE_DISPLAY_ENCODE_HLSLI
#define SW_ENGINE_DISPLAY_ENCODE_HLSLI

#if !defined( SW_DISPLAY_OUTPUT )
    #define SW_DISPLAY_OUTPUT 0
#endif

struct DisplayEncode
{
    static float3 applyToneCurve( float3 exposed );
    static float3 toDisplayNits( float3 signal, float peakNits );
    static float encodePQ( float nits );
    static float3 convertRec709ToRec2020( float3 linearRec709 );
    static float3 encode( float3 nits );
};

/// SDR 화면이 신호를 보이는 감마 — 출력 신호(톤맵 결과)를 디스플레이 기준 선형으로 풀 때 쓴다.
static const float kDisplaySignalGamma = 2.2f;
/// PQ(SMPTE ST 2084)가 나타내는 최대 휘도(nits) — C++ `DisplayEncoding::kPQMaxNits`.
static const float kDisplayPQMaxNits = 10000.0f;
/// scRGB 의 1.0 이 뜻하는 휘도(nits) — C++ `DisplayEncoding::kScRGBReferenceNits`.
static const float kDisplayScRGBReferenceNits = 80.0f;

/**
 * @brief 노출을 건 장면 색을 [0,1] 출력 신호로 옮깁니다. 어깨는 PassCB `g_DisplayOutput.x` 다(SDR 1, HDR `(최대 휘도 / 종이 흰색)^(1/2.2)`).
 * @details 값을 받지 못한 패스(0)도 1 로 읽는다 — SDR 결과가 `x / (x + 1)` 그대로라 SDR 골든이 바뀌지 않는다(1 을 더하는 연산이 같다).
 */
static float3 DisplayEncode::applyToneCurve( float3 exposed )
{
    const float shoulder = max( g_DisplayOutput.x, 1.0f );
    return exposed / ( exposed + shoulder );
}

/** @brief 출력 신호 @p signal(1 = 최대 휘도)을 디스플레이 기준 선형 휘도(nits)로 풉니다. */
static float3 DisplayEncode::toDisplayNits( float3 signal, float peakNits )
{
    return pow( max( signal, 0.0f ), kDisplaySignalGamma ) * peakNits;
}

/** @brief 휘도 @p nits 를 PQ 신호(0 ~ 1)로 인코딩합니다(SMPTE ST 2084 — C++ `DisplayEncoding::encodePQ` 와 같은 상수 · 식). */
static float DisplayEncode::encodePQ( float nits )
{
    const float m1      = 2610.0f / 16384.0f;
    const float m2      = 2523.0f / 4096.0f * 128.0f;
    const float c1      = 3424.0f / 4096.0f;
    const float c2      = 2413.0f / 4096.0f * 32.0f;
    const float c3      = 2392.0f / 4096.0f * 32.0f;
    const float powered = pow( saturate( nits / kDisplayPQMaxNits ), m1 );
    return pow( ( c1 + c2 * powered ) / ( 1.0f + c3 * powered ), m2 );
}

/** @brief 선형 Rec.709 원색을 선형 BT.2020 원색으로 옮깁니다(ITU-R BT.2087 — C++ `DisplayEncoding::convertRec709ToRec2020` 과 같은 행렬). */
static float3 DisplayEncode::convertRec709ToRec2020( float3 linearRec709 )
{
    const float3x3 rec709ToRec2020 = float3x3( 0.6274040f, 0.3292820f, 0.0433136f,
                                               0.0690970f, 0.9195400f, 0.0113612f,
                                               0.0163916f, 0.0880132f, 0.8955950f );
    return mul( rec709ToRec2020, linearRec709 );
}

/** @brief 디스플레이 기준 선형 휘도(nits, Rec.709 원색)를 출력 장치의 백버퍼 값으로 인코딩합니다 — `SW_DISPLAY_OUTPUT` 이 고른다. */
static float3 DisplayEncode::encode( float3 nits )
{
#if SW_DISPLAY_OUTPUT == 1
    const float3 rec2020 = max( DisplayEncode::convertRec709ToRec2020( nits ), 0.0f );
    return float3( DisplayEncode::encodePQ( rec2020.r ), DisplayEncode::encodePQ( rec2020.g ), DisplayEncode::encodePQ( rec2020.b ) );
#elif SW_DISPLAY_OUTPUT == 2
    return nits / kDisplayScRGBReferenceNits;
#else
    return nits; // SDR — 출력 신호가 곧 백버퍼 값이라 이 길을 쓰는 패스가 없다(SDR 은 인코딩 패스가 없다)
#endif
}

#endif // SW_ENGINE_DISPLAY_ENCODE_HLSLI
