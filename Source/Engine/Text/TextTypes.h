/**
 * @file TextTypes.h
 * @brief 글자 계층이 함께 쓰는 값 타입입니다 — 글꼴 굵기 · 기울기 · 면 번호 · 메트릭 · SDF 비트맵.
 * @details 길이는 둘로 나뉩니다. 면 · 글리프 메트릭은 **em 비율**(1 = 글꼴 크기)이고, 래스터 결과는 **래스터 픽셀**(`SdfRasterParams::_pixelSize` 기준)입니다.
 *          화면 크기는 em 비율 × 글꼴 크기(UI 단위)로 구합니다 — 래스터 크기와 화면 크기를 섞지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 글꼴 굵기입니다(OpenType usWeightClass 의 이름 있는 값). */
    ENUM()
    enum class FontWeight : uint16
    {
        Thin     = 100,
        Light    = 300,
        Regular  = 400,
        Medium   = 500,
        SemiBold = 600,
        Bold     = 700,
        Black    = 900
    };
} // namespace sw

namespace sw
{
    /** @brief 글꼴 기울기입니다. 가족에 기운 면이 없으면 배치가 기울여 그립니다(가짜 이탤릭). */
    ENUM()
    enum class FontSlant : uint8
    {
        Upright,
        Italic
    };
} // namespace sw

namespace sw
{
    /** @brief 불러온 글꼴 면(파일 하나 + 면 번호) 하나를 가리키는 번호입니다. 래스터라이저가 1 부터 줍니다. */
    using FontFaceID = uint32;
    /** @brief 면이 없음을 뜻하는 번호입니다(래스터라이저가 주는 번호는 1 부터다). */
    inline constexpr FontFaceID kInvalidFontFaceID = 0;

    /** @brief 면 하나의 세로 메트릭입니다(em 비율 — 위는 +). */
    struct FontFaceMetrics
    {
        float32 _ascender{ 0.0f };           ///< 기준선 위 높이
        float32 _descender{ 0.0f };          ///< 기준선 아래 깊이(음수)
        float32 _lineGap{ 0.0f };            ///< 줄 사이 여백
        float32 _underlinePosition{ 0.0f };  ///< 밑줄 중심(음수 = 기준선 아래)
        float32 _underlineThickness{ 0.0f }; ///< 밑줄 두께
    };
} // namespace sw

namespace sw
{
    /** @brief 글리프 하나의 메트릭입니다(em 비율). */
    struct GlyphMetrics
    {
        float2  _bearing{};       ///< 원점에서 그림 상자 왼쪽 위까지(y 는 기준선 위가 +)
        float2  _size{};          ///< 그림 상자 크기
        float32 _advance{ 0.0f }; ///< 다음 글리프 원점까지
    };
} // namespace sw

namespace sw
{
    /** @brief SDF 래스터화 인자입니다. */
    struct SdfRasterParams
    {
        uint32 _pixelSize{ 48 }; ///< em 하나의 래스터 픽셀 수
        uint32 _spreadPx{ 6 };   ///< 윤곽에서 이 픽셀 거리까지 거리값을 담는다(0.5 ± 0.5 로 정규화)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief SDF 글리프 비트맵입니다(1 채널 8 비트, 128 = 윤곽, 큰 값 = 안쪽).
     * @details 비트맵은 윤곽 둘레로 `_spreadPx` 만큼 넓다. 빈 글리프(공백)는 크기 0 이고 `_advancePx` 만 있다.
     */
    struct SdfGlyphBitmap
    {
        vector<uint8> _bytes{};           ///< 행 우선 · 빈틈없는 행(`_width` 바이트)
        float2        _bearingPx{};       ///< 원점에서 비트맵 왼쪽 위까지(y 는 기준선 위가 +, 래스터 픽셀)
        float32       _advancePx{ 0.0f }; ///< 래스터 픽셀 전진
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw
