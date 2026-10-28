/**
 * @file AutomationImageMetric.h
 * @brief 시나리오 스크린샷(PPM)의 영역 지표를 정의합니다. `<ExpectImage>` 가 단언하는 값이며, 배경과 톤맵이 바뀌어도 버티는 상대 지표입니다.
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
        DarkFraction,     ///< 영역에서 휘도가 영역 중앙값 × ratio 보다 어두운 픽셀 비율입니다. 그림자와 실루엣을 보며, 조명과 톤맵이 바뀌어도 상대값이라 버팁니다.
        MeanRedMinusBlue, ///< 평균 (R − B)(−1..1)입니다. 배경 대비 색을 봅니다.
        DifferentFrom,    ///< 기준 이미지와의 평균 절대 차(0..1, RGB)입니다. 섞임 정도 같은 "양" 을 `near` 로만 단언한다 — "다르다 · 같다" 는 `DifferingFraction` 이며, 러너 검사가 `atLeast` · `atMost` 를 거절한다.
        /**
         * 기준 이미지와 색 거리(R · G · B 차의 합, 0..765)가 `AutomationImageMetric::kDifferingMinDistance` 이상인 화소의 비율(0..1)입니다.
         * "같은 그림" 과 일부 화소만 바뀌는 변화(브러시 한 점 · 그림자 · 잘못 그린 위젯)는 이것으로 봅니다 — 평균 차는 좁은 자리의 큰 변화를 넓이로 나눠 묻습니다.
         */
        DifferingFraction,
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationImage @brief 8 비트 RGB 이미지 한 장입니다(PPM P6 를 읽은 값). */
    struct AutomationImage
    {
        vector<uint8> _listRGB{}; ///< 행 우선이며 픽셀마다 R, G, B 순서입니다.
        uint32        _width{ 0 };
        uint32        _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationImageRegion @brief 이미지 크기에 대한 비율(0..1)로 적은 사각형입니다. */
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
     * @brief PPM 읽기와 영역 지표 계산을 담당합니다. 테스트 바이너리의 PPM 도구(`RHITestImage`)와 따로 엔진에 둡니다. 시나리오는 배포본에서도 돌기 때문입니다.
     */
    class SW_API AutomationImageMetric
    {
    public:
        /// @brief `DifferingFraction` 이 "다른 화소" 로 치는 색 거리의 하한 — 같은 경로의 반올림 흔들림(1~2)을 넘는다(GPU 테스트의 `RHITestImage::compare` 와 같은 값).
        static constexpr uint32 kDifferingMinDistance = 6;

        /** @brief 이 지표가 기준 이미지(`reference`)를 비교하는가 — `DifferentFrom` · `DifferingFraction`. */
        static bool usesReference( AutomationImageMetricKind kind );
        /** @brief PPM(P6, 최대값 255) 파일을 읽습니다. 실패면 false 와 이유입니다. */
        [[nodiscard]] static bool loadPpm( string_view path, AutomationImage& outImage, string& outError );
        /** @brief 메모리의 PPM(P6, 최대값 255)을 읽습니다. */
        [[nodiscard]] static bool parsePpm( const uint8* pData, size_t size, AutomationImage& outImage, string& outError );
        /** @brief 지표 이름(`meanLuma`, `darkFraction`, `meanRedMinusBlue`, `differentFrom`, `differingFraction`)을 읽습니다. */
        [[nodiscard]] static bool tryParseKind( string_view text, AutomationImageMetricKind& outKind );
        /** @brief `x0,y0,x1,y1`(0..1, x0<x1, y0<y1)을 읽습니다. */
        [[nodiscard]] static bool tryParseRegion( string_view text, AutomationImageRegion& outRegion );
        /**
         * @brief @p region 의 지표 값을 구합니다. 영역이 픽셀 하나도 덮지 않거나 기준을 비교하는 지표(`usesReference`)의 기준이 없거나 크기가 다르면 false 를 반환하고 이유를 남깁니다.
         * @param darkRatio `DarkFraction` 의 임계값(중앙값에 곱하는 수)
         */
        [[nodiscard]] static bool measure( const AutomationImage& image, const AutomationImageRegion& region, AutomationImageMetricKind kind, float32 darkRatio,
                                           const AutomationImage* pReference, float64& outValue, string& outError );
    };
} // namespace sw
