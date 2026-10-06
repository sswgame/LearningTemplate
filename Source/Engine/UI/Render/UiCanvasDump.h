/**
 * @file UiCanvasDump.h
 * @brief 캔버스 그리기 목록을 글로 씁니다 — 결정성 골든(`UiDeterminismTest`)이 견주는 형식입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

namespace sw
{
    struct CanvasDrawList;

    /**
     * @struct UiCanvasDump
     * @brief 그리기 목록을 일괄 · 사각형 순서로 한 줄씩 씁니다(소수 둘째 자리, 물리 픽셀).
     * @details 첫 줄 `target 너비 높이`, 일괄마다 `batch 사각형수 scissor x y 너비 높이`(가위 없으면 `none`) + ` tex 이름…`, 그 아래 사각형마다
     *          `  종류 x y 너비 높이 R G B A` 와 텍스처를 쓰는 사각형은 ` tex 이름`. 텍스처 이름은 경로 · `atlas페이지` · `object` 입니다.
     *          글리프 UV(아틀라스 안 자리)는 쓰지 않습니다 — 래스터화 순서가 아니라 배치 · 색이 견줄 대상입니다.
     */
    struct SW_API UiCanvasDump
    {
        static string makeDump( const CanvasDrawList& list );
    };
} // namespace sw
