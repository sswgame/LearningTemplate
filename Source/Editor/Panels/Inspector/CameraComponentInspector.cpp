/**
 * @file CameraComponentInspector.cpp
 * @brief CameraComponent 의 투영 컨트롤(트랜스폼은 SceneComponent 단계가 그린다)
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"

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
                auto* pCameraComp = static_cast<CameraComponent*>( pComponent );
                ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Camera" ) );
                float32 fovDeg = MathUtil::toDegree( pCameraComp->getFieldOfViewY() );
                EditorWidgets::drawPropertyRowLabel( "FOV (Deg)" );
                if ( ImGui::SliderFloat( "##FOVDeg", &fovDeg, 10.0f, 140.0f, "%.1f" ) )
                    pCameraComp->setFieldOfViewY( MathUtil::toRadian( fovDeg ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraFOV" ) );

                float32 nearZ = pCameraComp->getNearPlane();
                EditorWidgets::drawPropertyRowLabel( "Near Plane" );
                if ( ImGui::DragFloat( "##NearPlane", &nearZ, 0.01f, 0.001f, 10.0f ) )
                    pCameraComp->setNearPlane( nearZ );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraNear" ) );

                float32 farZ = pCameraComp->getFarPlane();
                EditorWidgets::drawPropertyRowLabel( "Far Plane" );
                if ( ImGui::DragFloat( "##FarPlane", &farZ, 1.0f, 1.0f, 10000.0f ) )
                    pCameraComp->setFarPlane( farZ );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraFar" ) );

                bool bOrtho = pCameraComp->isOrthographic();
                if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Orthographic" ), &bOrtho ) )
                    pCameraComp->setOrthographic( bOrtho );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraOrthographic" ) );

                if ( bOrtho )
                {
                    float32 orthoH = pCameraComp->getOrthoHeight();
                    EditorWidgets::drawPropertyRowLabel( "Ortho Height" );
                    if ( ImGui::DragFloat( "##OrthoHeight", &orthoH, 0.1f, 0.1f, 100.0f ) )
                        pCameraComp->setOrthoHeight( orthoH );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "CameraOrthoHeight" ) );
                }
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( CameraComponent, CameraComponentInspector );
} // namespace sw::editor
