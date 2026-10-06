/**
 * @file AutomationImageMetric.h
 * @brief 시나리오 스크린샷(PPM) 의 영역 지표 — `<ExpectImage>` 가 단언하는 값입니다(배경 · 톤맵이 바뀌어도 버티는 상대 지표).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 영역 지표 종류입니다. */
    enum class AutomationImageMetricKind : uint8
    {
        MeanLuma = 0,     ///< 영역 평균 휘도(0..1, Rec.709)
        DarkFraction,     ///< 영역에서 휘도가 **영역 중앙값 × ratio** 보다 어두운 픽셀 비율 — 그림자 · 실루엣(조명 · 톤맵이 바뀌어도 상대값)
        MeanRedMinusBlue, ///< 평균 (R − B)(−1..1) — 배경 대비 색
        DifferentFrom,    ///< 기준 그림과의 평균 절대 차(0..1, RGB) — 백엔드 일치 · 움직임
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationImage @brief 8 비트 RGB 그림 한 장입니다(PPM P6 를 읽은 값). */
    struct AutomationImage
    {
        vector<uint8> _listRgb{}; ///< 행 우선, 픽셀마다 R · G · B
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationImageRegion @brief 그림 크기에 대한 비율(0..1)로 적은 사각형입니다. */
    struct AutomationImageRegion
    {
        float32 _x0{ 0.0f };
        float32 _y0{ 0.0f };
        float32 _x1{ 1.0f };
        float32 _y1{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationImageMetric
     * @brief PPM 읽기와 영역 지표 계산입니다. 시험 바이너리의 PPM 도구(`RHITestImage`)와 따로 엔진에 둡니다 — 시나리오는 배포본에서도 돈다.
     */
    class SW_API AutomationImageMetric
    {
    public:
        /** @brief PPM(P6, 최대값 255) 파일을 읽습니다. 실패면 false 와 이유입니다. */
        [[nodiscard]] static bool loadPpm( string_view path, AutomationImage& outImage, string& outError );
        /** @brief 메모리의 PPM(P6, 최대값 255)을 읽습니다. */
        [[nodiscard]] static bool parsePpm( const uint8* pData, size_t size, AutomationImage& outImage, string& outError );
        /** @brief 지표 이름(`meanLuma` · `darkFraction` · `meanRedMinusBlue` · `differentFrom`)을 읽습니다. */
        [[nodiscard]] static bool tryParseKind( string_view text, AutomationImageMetricKind& outKind );
        /** @brief `x0,y0,x1,y1`(0..1, x0<x1 · y0<y1)을 읽습니다. */
        [[nodiscard]] static bool tryParseRegion( string_view text, AutomationImageRegion& outRegion );
        /**
         * @brief @p region 의 지표 값입니다. 영역이 픽셀 하나도 덮지 않거나 `DifferentFrom` 의 기준이 없거나 크기가 다르면 false 와 이유입니다.
         * @param darkRatio `DarkFraction` 의 문턱(중앙값에 곱하는 수)
         */
        [[nodiscard]] static bool measure( const AutomationImage& image, const AutomationImageRegion& region, AutomationImageMetricKind kind, float32 darkRatio,
                                           const AutomationImage* pReference, float64& outValue, string& outError );
    };
} // namespace sw
