/**
 * @file SceneComponentInspector.cpp
 * @brief SceneComponent 와 그 하위 타입 전부의 트랜스폼 · 기즈모 컨트롤
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Object/Component/SceneComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief SceneComponent 와 그 하위 타입 전부의 트랜스폼 및 기즈모 컨트롤 */
        class SceneComponentInspector final : public IInspectorComponent
        {
        public:
            void drawSection( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
            {
                SceneComponent* pSceneComp = static_cast<SceneComponent*>( pComponent );
                if ( pSceneComp == nullptr )
                    return;

                ImGui::SeparatorText( "Transform" );

                EditorWidgets::drawGizmoOperationControls();

                float3 pos = pSceneComp->getLocalPosition();
                // 회전은 라디안으로 저장하고 도로 보이고 고친다(언리얼 FRotator · 유니티 localEulerAngles).
                float3 rotDegree = pSceneComp->getLocalRotation() * MathUtil::kRadianToDegree;
                float3 scl       = pSceneComp->getLocalScale();

                if ( EditorWidgets::drawVec3Control( "Position", pos, 0.0f, 80.0f, 0.1f ) )
                    pSceneComp->setLocalPosition( pos );
                if ( EditorWidgets::drawVec3Control( "Rotation", rotDegree, 0.0f, 80.0f, InspectorPropertyLayout::kAngleDragSpeed ) )
                    pSceneComp->setLocalRotation( rotDegree * MathUtil::kDegreeToRadian );
                if ( EditorWidgets::drawVec3Control( "Scale", scl, 1.0f, 80.0f, 0.01f ) )
                    pSceneComp->setLocalScale( scl );

                const float3 world = pSceneComp->getWorldPosition();
                ImGui::TextDisabled( "World: %.2f, %.2f, %.2f",
                                     static_cast<float64>( world._x ),
                                     static_cast<float64>( world._y ),
                                     static_cast<float64>( world._z ) );
            }

            void collectDrawnProperties( vector<hashed_string>& outListName ) const override
            {
                static const hashed_string s_arrName[] = { hashed_string( "_localPosition" ), hashed_string( "_localRotation" ),
                                                           hashed_string( "_localScale" ) };
                outListName.insert( outListName.end(), std::begin( s_arrName ), std::end( s_arrName ) );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( SceneComponent, SceneComponentInspector );
} // namespace sw::editor
