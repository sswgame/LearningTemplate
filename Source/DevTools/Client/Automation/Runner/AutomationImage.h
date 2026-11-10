/**
 * @file AutomationImage.h
 * @brief 시나리오 스크린샷(PPM)의 영역 지표를 정의합니다. `<ExpectImage>` 가 단언하는 값이며, 배경과 톤맵이 바뀌어도 버티는 상대 지표입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct AutomationImageRegion;
    struct AutomationImageDifference;
    struct AutomationImageThreshold;

    /** @struct SW_DEVTOOLS_API AutomationImage @brief 8 비트 RGB 이미지 한 장입니다(PPM P6 를 읽은 값). */
    struct AutomationImage
    {
        vector<uint8> _listRGB{}; ///< 행 우선이며 픽셀마다 R, G, B 순서입니다.
        uint32        _width{ 0 };
        uint32        _height{ 0 };

        // 시나리오 이미지 단언의 도구입니다 — PPM 읽기 · 쓰기, 영역 축소, 골든 비교. 시나리오는 배포본에서도 돌기 때문에 엔진에 둡니다.
        // 골든은 영역을 가로 `kGoldenMaxWidth` 이하로 줄인(상자 평균) 그림이다 — 저장소에 들어가는 크기를 작게 두고, 백엔드 사이의 화소 하나짜리 래스터 차를 흡수한다.
        // 어디부터 "다른 화소" · "깊은 화소" 로 칠지는 정책 데이터(`Config/Dev/GoldenImage.json`, `AutomationGoldenPolicy`)가 정한다 — `compare` 가 인자로 받는다.
        /// @brief 색 거리(R · G · B 차의 합)의 최대값입니다. 평균 거리는 이것으로 나눠 0..1 로 둔다.
        static constexpr uint32 kMaxColorDistance = 765;
        /// @brief 골든의 최대 가로 화소 수입니다(세로는 비율대로).
        static constexpr uint32 kGoldenMaxWidth = 160;

        /** @brief PPM(P6, 최대값 255) 파일을 읽습니다. 실패면 그 이유입니다(못 읽은 파일은 경로가 대상). */
        [[nodiscard]] static expected<AutomationImage> loadPpm( string_view path );
        /** @brief 메모리의 PPM(P6, 최대값 255)을 읽습니다. 신뢰 경계다 — 틀린 머리말 · 짧은 화소는 오류입니다. */
        [[nodiscard]] static expected<AutomationImage> parsePpm( const uint8* pData, size_t size );
        /** @brief 이미지를 PPM(P6) 바이트로 씁니다. */
        static void encodePpm( const AutomationImage& image, vector<uint8>& outBytes );
        /** @brief `x0,y0,x1,y1`(0..1, x0<x1, y0<y1)을 읽습니다. */
        [[nodiscard]] static bool tryParseRegion( string_view text, AutomationImageRegion& outRegion );
        /** @brief @p region 을 가로 `kGoldenMaxWidth` 이하로 상자 평균해 줄입니다. 영역이 화소 하나도 덮지 않으면 그 그림 크기가 대상인 오류입니다. */
        [[nodiscard]] static expected<AutomationImage> reduce( const AutomationImage& image, const AutomationImageRegion& region );
        /**
         * @brief 같은 크기의 두 그림을 @p measure 의 문턱으로 비교합니다. 크기가 다르면 두 크기가 대상인 오류입니다(영역 · 창 크기가 바뀌었다 — 골든을 다시 떠야 한다).
         * @details 두 축을 함께 잰다 — 넓고 얕은 차이(다른 화소 비율 · 평균 거리)와 좁고 깊은 차이(깊은 화소가 4 이웃으로 이어진 가장 큰 덩어리의 화소 수).
         *          비율 하나만 보면 작은 마젠타 덩어리(전체의 0.2 %)가 1 % 문턱 아래로 지나간다.
         */
        [[nodiscard]] static expected<AutomationImageDifference> compare( const AutomationImage& actual, const AutomationImage& golden, const AutomationImageThreshold& measure );
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
    /** @struct AutomationImageThreshold @brief 비교가 화소 하나를 어떻게 셀지입니다(색 거리 = R · G · B 차의 합, 0..765). 값은 정책 데이터에서 온다. */
    struct AutomationImageThreshold
    {
        uint32 _differingMinDistance{ 0 }; ///< 이 거리 이상이면 "다른 화소" — 축소 뒤 반올림 흔들림(1~2)을 넘는 값
        uint32 _deepMinDistance{ 0 };      ///< 이 거리 이상이면 "깊은 화소" — 덩어리(좁고 깊은 차이)를 이 화소로 센다
    };

    /** @struct AutomationImageDifference @brief 골든 비교 결과입니다. 판정은 `AutomationGoldenUtil::judge` 한 곳이 한다. */
    struct AutomationImageDifference
    {
        float64 _differingFraction{ 0.0 };     ///< 색 거리가 `_differingMinDistance` 이상인 화소의 비율(0..1)
        float64 _meanDistance{ 0.0 };          ///< 평균 색 거리(0..1, 거리 합 / (화소 수 × `kMaxColorDistance`))
        uint32  _largestDeepRegionPixels{ 0 }; ///< 색 거리가 `_deepMinDistance` 이상인 화소가 4 이웃으로 이어진 가장 큰 덩어리의 화소 수
    };

} // namespace sw
