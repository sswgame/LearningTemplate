/**
 * @file BoxCollider2DVisualizer.cpp
 * @brief BoxCollider2D 사각형을 뷰포트 와이어프레임 도형 값으로 모읍니다(그리기는 `EditorViewportVisualizer::drawAll`).
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/BoxCollider2DComponent.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief BoxCollider2D 사각형을 모읍니다 — 물리가 판정하는 그 상자(월드 회전 · 스케일을 받은 것)입니다.
         * @details 씬 전체를 훑지 않고 매니저의 콜라이더 레지스트리를 봅니다(`EditorViewportVisualizerArgs::_pListCollider`). 색은 테마 토큰 `ColliderWire` 입니다.
         */
        void gatherBoxCollider2DWire( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
        {
            if ( args._pListCollider == nullptr )
                return;
            EditorVisualizerLine prototype{};
            prototype._thickness = 1.5f;
            prototype._colorRole = EditorVisualizerColorRole::ColliderWire;
            for ( const BoxCollider2DComponent* pBox : *args._pListCollider )
            {
                const GameObject* pOwner       = ( pBox != nullptr ) ? pBox->getOwner() : nullptr;
                const bool        bInactiveBox = pOwner == nullptr || pOwner->isActive() == false || pBox->isActive() == false;
                if ( bInactiveBox )
                    continue;
                float3 arrCorner[4];
                EditorVisualizerGeometryUtil::computeColliderCorners( *pBox, arrCorner );
                outDrawList.addLoop( arrCorner, 4, prototype );
            }
        }
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( BoxCollider2D, "box_collider_2d", 100, SW_EDITOR_ICON_NAME( "Collider" ), SW_LOCKEYREF( "EdVisualizer", "BoxCollider2DLabel" ),
                                 SW_LOCKEYREF( "EdVisualizer", "BoxCollider2DTip" ), true, nullptr, &gatherBoxCollider2DWire );
} // namespace sw::editor
