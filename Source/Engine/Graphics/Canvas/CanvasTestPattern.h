/**
 * @file CanvasTestPattern.h
 * @brief 캔버스 개발 시험 그림입니다 — 위젯(UI 단계)이 생기기 전에 Canvas 패스 · 셰이더 · 아틀라스를 네 백엔드에서 눈으로 보는 길(`-gv_canvasTestPattern=1`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct CanvasDrawList;

    class FontSystem;

    /**
     * @struct CanvasTestPattern
     * @brief 둥근 사각형 · 테두리 · 반투명 겹침(프리멀티플라이) · 가위 자르기 · 둥근 자르기 · 그림자 · 글자를 칠합니다(텍스처 그림은 없다 — 에셋 없이 돈다).
     */
    struct SW_API CanvasTestPattern
    {
        /**
         * @brief 그리기 목록에 시험 그림을 칠합니다(물리 픽셀 = UI 단위). 글꼴 시스템이 있으면 기본 가족 · 문화권 대체 사슬로 글을 쓴다(한글은 시스템 글꼴).
         * @param targetSize 대상 픽셀 크기(가위를 그 안으로 자른다)
         */
        static void paint( CanvasDrawList& outCanvas, const float2& targetSize, FontSystem* pFontSystem, uint64 frameIndex );
    };
} // namespace sw
