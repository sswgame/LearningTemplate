/**
 * @file Dismemberment.h
 * @brief 신체 절단 형상 — 몸 영역(`FitTables` 의 영역 표)을 잘라 남은 몸 · 떨어진 조각을 나누고, 두 쪽의 구멍을 캡으로 막습니다.
 * @details 잘라 낸 삼각형 마스크는 잘라 내기와 같은 보임 마스크 길(`FitPartResult::hideTriangles`)로 조립된 메시에서 빠집니다. 떨어진 조각은
 *          자기 형상(+ 캡)으로 나와 나중의 파괴 · 물리 단계가 강체로 띄웁니다. 자르기 자체는 `GeometryCutUtil`(형상 입력 → 형상 출력)이라 미리
 *          쪼갠 파괴 가능 메시도 같은 도우미를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

namespace sw
{
    /** @brief 절단 결과입니다. 네 형상은 모두 같은 바인드 공간입니다. */
    struct DismembermentResult
    {
        AppearanceGeometry _remaining{};           ///< 남은 몸(잘린 삼각형 뺌)
        AppearanceGeometry _remainingCap{};        ///< 남은 몸의 구멍 캡
        AppearanceGeometry _severed{};             ///< 떨어진 조각
        AppearanceGeometry _severedCap{};          ///< 떨어진 조각의 구멍 캡
        vector<uint8>      _listTriangleSevered{}; ///< 원본 삼각형마다 잘렸는가 — 보임 마스크에 그대로 씀
    };
} // namespace sw

namespace sw
{
    /** @brief 신체 절단입니다(전부 static). */
    struct SW_API DismembermentUtil
    {
        /**
         * @brief 영역들을 잘라 냅니다. 삼각형은 정점 둘 이상이 잘라 낼 영역에 있으면 떨어진 조각 쪽입니다.
         * @param listVertexRegion 정점마다 영역 번호(`FitTables::assignRegions` 의 것)입니다.
         * @param listSeveredRegion 잘라 낼 영역 번호들입니다.
         * @return 잘린 삼각형이 없으면 false 입니다(결과는 남은 몸 = 원본).
         */
        static bool severRegions( const AppearanceGeometry& body, vector_reference<const uint16> listVertexRegion, vector_reference<const uint16> listSeveredRegion,
                                  DismembermentResult& outResult );
    };
} // namespace sw
