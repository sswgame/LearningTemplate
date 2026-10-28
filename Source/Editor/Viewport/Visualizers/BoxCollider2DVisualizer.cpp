/**
 * @file BoxCollider2DVisualizer.cpp
 * @brief BoxCollider2D 사각형을 뷰포트에 와이어프레임으로 그립니다
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/BoxCollider2DComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @brief BoxCollider2D 사각형을 와이어프레임으로 그립니다 — 물리가 판정하는 그 상자(월드 회전 · 스케일을 받은 것)입니다.
         * @details 씬 전체를 훑지 않고 매니저의 콜라이더 레지스트리를 봅니다(`EditorViewportVisualizerArgs::_pListCollider`). 색은 테마 토큰 `_colliderWire` 입니다.
         */
        void drawBoxCollider2DWire( const EditorViewportVisualizerArgs& args )
        {
            if ( args._pListCollider == nullptr )
                return;
            const ImU32 colorWire = EditorColorUtil::toImU32( EditorThemeUtil::getColliderWireColor() );

            for ( const BoxCollider2DComponent* pBox : *args._pListCollider )
            {
                const GameObject* pOwner       = ( pBox != nullptr ) ? pBox->getOwner() : nullptr;
                const bool        bInactiveBox = pOwner == nullptr || pOwner->isActive() == false || pBox->isActive() == false;
                if ( bInactiveBox )
                    continue;

                float3 arrCorner[4];
                EditorVisualizerGeometryUtil::computeColliderCorners( *pBox, arrCorner );
                ImVec2 arrScreen[4];
                bool   bVisible = true;
                for ( uint32 cornerIndex = 0; cornerIndex < 4 && bVisible; ++cornerIndex )
                {
                    bVisible = EditorViewportProjectionUtil::projectPoint( *args._pViewProj, arrCorner[cornerIndex], args._canvasPos, args._canvasSize,
                                                                           arrScreen[cornerIndex] );
                }
                if ( bVisible == false )
                    continue;
                for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                {
                    args._pDrawList->AddLine( arrScreen[cornerIndex], arrScreen[( cornerIndex + 1 ) % 4], colorWire, 1.5f );
                }
            }
        }
    } // namespace

    SW_EDITOR_VISUALIZER( BoxCollider2D, "box_collider_2d", 100, editoricon::kCollider, "Col", "BoxCollider2D 사각형을 와이어프레임으로 표시합니다", true,
                          &drawBoxCollider2DWire );
} // namespace sw::editor
