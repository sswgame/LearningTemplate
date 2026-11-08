/**
 * @file SceneComponentInspector.cpp
 * @brief SceneComponent 와 그 하위 타입 전부의 트랜스폼 · 기즈모 컨트롤
 */
#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief SceneComponent 와 그 하위 타입 전부의 트랜스폼 및 기즈모 컨트롤 */
        class SceneComponentInspector final : public IInspectorComponent
        {
        public:
            void drawSection( Component* pComponent ) override
            {
                if ( pComponent == nullptr )
                    return;

                ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Transform" ) );

                EditorWidgets::drawGizmoOperationControls( "inspector.gizmo" );

                TransformSectionValues values;
                InspectorComponentEdit::gatherTransform( *pComponent, values );
                if ( EditorWidgets::drawVec3Control( "Position", values._localPosition, 0.0f, 80.0f, 0.1f ) )
                    InspectorComponentEdit::setLocalPosition( *pComponent, values._localPosition );
                if ( EditorWidgets::drawVec3Control( "Rotation", values._localRotationDegrees, 0.0f, 80.0f, InspectorPropertyLayout::kAngleDragSpeed ) )
                    InspectorComponentEdit::setLocalRotationDegrees( *pComponent, values._localRotationDegrees );
                if ( EditorWidgets::drawVec3Control( "Scale", values._localScale, 1.0f, 80.0f, 0.01f ) )
                    InspectorComponentEdit::setLocalScale( *pComponent, values._localScale );

                const float3& world = values._worldPosition;
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "World", TextArgumentList().addDecimal( "worldX", static_cast<float64>( world._x ), 2 ).addDecimal( "worldY", static_cast<float64>( world._y ), 2 ).addDecimal( "worldZ", static_cast<float64>( world._z ), 2 ) ).c_str() );
            }

            void collectDrawnProperties( vector<hashed_string>& outListName ) const override
            {
                static const hashed_string s_arrName[] = { hashed_string( "_localPosition" ), hashed_string( "_localRotation" ),
                                                           hashed_string( "_localScale" ) };
                outListName.insert( outListName.end(), std::begin( s_arrName ), std::end( s_arrName ) );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR_FOR_TYPE( SceneComponent, &InspectorComponentEdit::getSceneComponentType, SceneComponentInspector );
} // namespace sw::editor
