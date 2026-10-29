/**
 * @file AutomationImageMetric.h
 * @brief 시나리오 스크린샷(PPM)의 영역 지표를 정의합니다. `<ExpectImage>` 가 단언하는 값이며, 배경과 톤맵이 바뀌어도 버티는 상대 지표입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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
    /** @struct AutomationImageDifference @brief 골든 비교 결과입니다. */
    struct AutomationImageDifference
    {
        float64 _differingFraction{ 0.0 }; ///< 색 거리가 `kDifferingMinDistance` 이상인 화소의 비율(0..1)
        float64 _meanDistance{ 0.0 };      ///< 평균 색 거리(0..1, RGB 차의 평균) — 보고용
    };

    /**
     * @class AutomationImageMetric
     * @brief 시나리오 이미지 단언의 도구입니다 — PPM 읽기 · 쓰기, 영역 축소, 골든 비교. 시나리오는 배포본에서도 돌기 때문에 엔진에 둡니다.
     * @details 골든은 영역을 가로 `kGoldenMaxWidth` 이하로 줄인(상자 평균) 그림이다 — 저장소에 들어가는 크기를 작게 두고, 백엔드 사이의 화소 하나짜리 래스터 차를 흡수한다.
     *          비교는 "다른 화소의 비율" 이다. 평균 차는 좁은 자리의 큰 변화(잘못 그린 위젯 · 빠진 그림자)를 넓이로 나눠 묻는다.
     */
    class SW_API AutomationImageMetric
    {
    public:
        /// @brief "다른 화소" 로 치는 색 거리(R · G · B 차의 합, 0..765)의 하한 — 축소 뒤 반올림 흔들림(1~2)을 넘는다.
        static constexpr uint32 kDifferingMinDistance = 12;
        /// @brief 골든의 최대 가로 화소 수입니다(세로는 비율대로).
        static constexpr uint32 kGoldenMaxWidth = 160;

        /** @brief PPM(P6, 최대값 255) 파일을 읽습니다. 실패면 false 와 이유입니다. */
        [[nodiscard]] static bool loadPpm( string_view path, AutomationImage& outImage, string& outError );
        /** @brief 메모리의 PPM(P6, 최대값 255)을 읽습니다. */
        [[nodiscard]] static bool parsePpm( const uint8* pData, size_t size, AutomationImage& outImage, string& outError );
        /** @brief 이미지를 PPM(P6) 바이트로 씁니다. */
        static void encodePpm( const AutomationImage& image, vector<uint8>& outBytes );
        /** @brief `x0,y0,x1,y1`(0..1, x0<x1, y0<y1)을 읽습니다. */
        [[nodiscard]] static bool tryParseRegion( string_view text, AutomationImageRegion& outRegion );
        /** @brief @p region 을 가로 `kGoldenMaxWidth` 이하로 상자 평균해 줄입니다. 영역이 화소 하나도 덮지 않으면 false 와 이유입니다. */
        [[nodiscard]] static bool reduce( const AutomationImage& image, const AutomationImageRegion& region, AutomationImage& outReduced, string& outError );
        /** @brief 같은 크기의 두 그림을 비교합니다. 크기가 다르면 false 와 이유입니다(영역 · 창 크기가 바뀌었다 — 골든을 다시 떠야 한다). */
        [[nodiscard]] static bool compare( const AutomationImage& actual, const AutomationImage& golden, AutomationImageDifference& outDifference, string& outError );
    };
} // namespace sw
