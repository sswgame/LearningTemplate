/**
 * @file NetSimWorldLayout.h
 * @brief 한 프로세스 재생의 월드 뷰포트 칸 크기입니다(ImGui 없음 — In process 탭이 그릴 때 읽는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw::editor
{
    /**
     * @struct NetSimWorldLayout
     * @brief 월드 뷰포트를 가로로 나란히 놓을 때의 칸 크기입니다.
     */
    struct NetSimWorldLayout
    {
        /** @brief 한 칸의 크기입니다. 머리글 두 줄 몫을 뺀 높이이고, 좁아도 폭 1 · 높이 @p minHeight 아래로 내려가지 않습니다. 월드가 없으면 한 칸으로 봅니다. */
        static float2 computeViewSize( float2 available, int32 worldCount, float32 itemSpacingX, float32 lineHeight, float32 minHeight );
    };
} // namespace sw::editor
