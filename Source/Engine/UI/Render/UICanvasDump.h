/**
 * @file UICanvasDump.h
 * @brief 캔버스 그리기 목록을 텍스트로 씁니다. 결정성 골든(`UIDeterminismTest`)이 비교하는 형식입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    struct CanvasDrawList;

    /**
     * @struct UICanvasDump
     * @brief 그리기 목록을 배치와 사각형 순서로 한 줄씩 씁니다(소수 둘째 자리, 물리 픽셀).
     * @details 첫 줄은 `target 너비 높이` 이고, 배치마다 `batch 사각형수 scissor x y 너비 높이`(가위가 없으면 `none`)와 ` tex 이름…` 을 쓴다. 그 아래 사각형마다
     *          `  종류 x y 너비 높이 R G B A` 를 쓰고, 텍스처를 쓰는 사각형은 ` tex 이름` 을, 화면 앵커 기준 사각형(화면 마커)은 ` anchor 번호` 를 붙인다. 텍스처 이름은 경로, `atlas페이지`, `object` 중 하나다.
     *          글리프 UV(아틀라스 안 위치)는 쓰지 않는다. 래스터화 순서가 아니라 배치와 색을 비교하기 위해서다.
     */
    struct SW_API UICanvasDump
    {
        static string makeDump( const CanvasDrawList& list );
    };
} // namespace sw
