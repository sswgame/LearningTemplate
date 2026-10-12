/**
 * @file EditorViewportAssetDrop.h
 * @brief 콘텐츠 브라우저에서 씬 뷰로 끌어 놓은 애셋을 마우스 아래 바닥에 놓습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct float2;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorViewportAssetDrop
     * @brief 씬 뷰 캔버스의 애셋 드롭입니다. 상태가 없어 정적 함수 하나입니다.
     * @details 종류마다 무엇을 만드는지는 `EditorAssetCommands::dropAt`(에셋 종류 동작)이 정합니다. 여기서는 놓을 자리만 고릅니다.
     */
    struct EditorViewportAssetDrop
    {
        /**
         * @brief 이번 프레임에 캔버스에 놓인 애셋을 받아 마우스 아래 바닥에 놓습니다(2D 는 Z = 0 평면, 3D 는 Y = 0).
         * @details 캔버스 밖이거나 평면과 평행하면 원점에 놓는다. 편집이 막혔으면(Play) 놓지 않는다. 캔버스 이미지를 그린 창 안에서 부른다.
         */
        static void processDrop( const float2& canvasPos, const float2& canvasSize, const float32* pView, const float32* pProj, bool b2DMode );
    };
} // namespace sw::editor
