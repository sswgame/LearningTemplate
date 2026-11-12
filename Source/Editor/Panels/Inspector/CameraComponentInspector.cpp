/**
 * @file CameraComponentInspector.cpp
 * @brief CameraComponent 의 투영 컨트롤(트랜스폼은 SceneComponent 단계가 그린다). 값은 `InspectorComponentEdit` 가 모으고 넣는다(MVVM 계약).
 */
#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentEdit.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief CameraComponent 의 투영 컨트롤(트랜스폼은 SceneComponent 단계가 그린다) */
        class CameraComponentInspector final : public IInspectorComponent
        {
        public:
            void collectDrawnProperties( vector<hashed_string>& outListName ) const override
            {
                static const hashed_string s_arrName[] = { hashed_string( "_fovY" ), hashed_string( "_nearZ" ), hashed_string( "_farZ" ),
                                                           hashed_string( "_bOrthographic" ), hashed_string( "_orthoHeight" ) };
                outListName.insert( outListName.end(), std::begin( s_arrName ), std::end( s_arrName ) );
            }

            void drawSection( Component* pComponent ) override
            {
                if ( pComponent == nullptr )
                    return;
                CameraSectionValues values;
                InspectorComponentEdit::gatherCamera( *pComponent, values );
                bool bChanged = false;

                ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Camera" ) );
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "FOVDeg" ) );
                bChanged |= EditorWidgets::sliderFloat( "##FOVDeg", &values._fovYDegrees, 10.0f, 140.0f, "%.1f" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraFOV" ) );

                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NearPlane" ) );
                bChanged |= EditorWidgets::dragFloat( "##NearPlane", &values._nearPlane, 0.01f, 0.001f, 10.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraNear" ) );

                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "FarPlane" ) );
                bChanged |= EditorWidgets::dragFloat( "##FarPlane", &values._farPlane, 1.0f, 1.0f, 10000.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraFar" ) );

                bool bOrtho = values._bOrthographic != SW_FALSE;
                if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Orthographic" ), &bOrtho ) )
                {
                    values._bOrthographic = bOrtho ? SW_TRUE : SW_FALSE;
                    bChanged              = true;
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraOrthographic" ) );

                if ( bOrtho )
                {
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "OrthoHeight" ) );
                    bChanged |= EditorWidgets::dragFloat( "##OrthoHeight", &values._orthoHeight, 0.1f, 0.1f, 100.0f );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraOrthoHeight" ) );
                }
                if ( bChanged )
                    InspectorComponentEdit::applyCamera( *pComponent, values );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR_FOR_TYPE( CameraComponent, &InspectorComponentEdit::getCameraComponentType, CameraComponentInspector );
} // namespace sw::editor
