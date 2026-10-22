/**
 * @file TextTypes.h
 * @brief 텍스트 계층이 함께 쓰는 값 타입을 정의합니다. 폰트 굵기, 기울기, 면 번호, 메트릭, SDF 비트맵이 있습니다.
 * @details 길이는 둘로 나뉜다. 면과 글리프 메트릭은 **em 비율**(1 = 폰트 크기)이고, 래스터 결과는 **래스터 픽셀**(`SdfRasterParams::_pixelSize` 기준)이다.
 *          화면 크기는 em 비율 × 폰트 크기(UI 단위)로 구하며, 래스터 크기와 화면 크기를 섞지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 폰트 굵기입니다(OpenType usWeightClass 의 이름 있는 값). */
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
    /** @brief 폰트 기울기입니다. 패밀리에 기운 면이 없으면 배치가 기울여 그립니다(가짜 이탤릭). */
    ENUM()
    enum class FontSlant : uint8
    {
        Upright,
        Italic
    };
} // namespace sw

namespace sw
{
    /** @brief 로드한 폰트 면(파일 하나 + 면 번호) 하나를 가리키는 번호입니다. 래스터라이저가 1 부터 부여합니다. */
    using FontFaceID = uint32;
    /** @brief 면이 없음을 뜻하는 번호입니다(래스터라이저가 주는 번호는 1 부터다). */
    inline constexpr FontFaceID kInvalidFontFaceID = 0;

    /** @brief 면 하나의 세로 메트릭입니다(em 비율 — 위는 +). */
    struct FontFaceMetrics
    {
        float32 _ascender{ 0.0f };           ///< 기준선 위 높이입니다.
        float32 _descender{ 0.0f };          ///< 기준선 아래 깊이(음수)
        float32 _lineGap{ 0.0f };            ///< 줄 사이 여백입니다.
        float32 _underlinePosition{ 0.0f };  ///< 밑줄 중심(음수 = 기준선 아래)
        float32 _underlineThickness{ 0.0f }; ///< 밑줄 두께입니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 글리프 하나의 메트릭입니다(em 비율). */
    struct GlyphMetrics
    {
        float2  _bearing{};       ///< 원점에서 그리기 상자 왼쪽 위까지의 거리입니다(y 는 기준선 위가 +).
        float2  _size{};          ///< 그리기 상자 크기입니다.
        float32 _advance{ 0.0f }; ///< 다음 글리프 원점까지의 거리입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 글리프 아틀라스에 담는 거리장 종류입니다(폰트 카탈로그 `_glyphField`).
     * @details `Sdf` 는 단일 채널 R8(FreeType `sdf` 렌더러, 결정 R2)이고 `Msdf` 는 RGBA8 이다 — RGB 가 멀티채널 거리장, A 가 일반 SDF 다. MSDF 는 큰 글자의 모서리가 날카롭지만
     *          글리프당 CPU 비용이 더 들고 페이지 메모리가 네 배다. 셰이더가 알도록 글리프 사각형에 `CanvasQuadFlag::kMultiChannelGlyph` 가 붙는다.
     */
    ENUM()
    enum class GlyphFieldKind : uint8
    {
        Sdf,
        Msdf
    };

    /** @brief 거리장 한 종류의 텍셀당 바이트 수입니다(`Sdf` 1, `Msdf` 4). */
    inline constexpr uint32 getFieldBytesPerTexel( GlyphFieldKind kind )
    {
        return kind == GlyphFieldKind::Msdf ? 4u : 1u;
    }
} // namespace sw

namespace sw
{
    /** @brief SDF 래스터화 인자입니다. */
    struct SdfRasterParams
    {
        uint32 _pixelSize{ 48 }; ///< em 하나의 래스터 픽셀 수입니다.
        uint32 _spreadPx{ 6 };   ///< 윤곽에서 이 픽셀 거리까지 거리값을 담는다(0.5 ± 0.5 로 정규화)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 거리장 글리프 비트맵입니다(SDF 는 1 채널 8 비트, MSDF 는 RGBA 4 채널 8 비트 — 128 = 윤곽, 큰 값 = 안쪽).
     * @details 비트맵은 윤곽 둘레로 `_spreadPx` 만큼 넓다. 빈 글리프(공백)는 크기 0 이고 `_advancePx` 만 있다.
     */
    struct SdfGlyphBitmap
    {
        vector<uint8> _bytes{};           ///< 행 우선이며 빈틈없는 행(`_width` × 텍셀당 바이트)입니다.
        float2        _bearingPx{};       ///< 원점에서 비트맵 왼쪽 위까지(y 는 기준선 위가 +, 래스터 픽셀)
        float32       _advancePx{ 0.0f }; ///< 래스터 픽셀 단위 전진입니다.
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw
