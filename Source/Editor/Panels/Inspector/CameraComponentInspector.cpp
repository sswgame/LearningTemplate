/**
 * @file CameraComponentInspector.cpp
 * @brief CameraComponent 의 투영 컨트롤(트랜스폼은 SceneComponent 단계가 그린다)
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"

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

            void drawSection( Component* pComponent, IRHIDevice* /*pRHIDevice*/ ) override
            {
                auto* pCameraComp = static_cast<CameraComponent*>( pComponent );
                ImGui::SeparatorText( "Camera" );
                float32 fovDeg = MathUtil::toDegree( pCameraComp->getFieldOfViewY() );
                if ( ImGui::SliderFloat( "FOV (Deg)", &fovDeg, 10.0f, 140.0f, "%.1f" ) )
                    pCameraComp->setFieldOfViewY( MathUtil::toRadian( fovDeg ) );

                float32 nearZ = pCameraComp->getNearPlane();
                if ( ImGui::DragFloat( "Near Plane", &nearZ, 0.01f, 0.001f, 10.0f ) )
                    pCameraComp->setNearPlane( nearZ );

                float32 farZ = pCameraComp->getFarPlane();
                if ( ImGui::DragFloat( "Far Plane", &farZ, 1.0f, 1.0f, 10000.0f ) )
                    pCameraComp->setFarPlane( farZ );

                bool bOrtho = pCameraComp->isOrthographic();
                if ( ImGui::Checkbox( "Orthographic", &bOrtho ) )
                    pCameraComp->setOrthographic( bOrtho );

                if ( bOrtho )
                {
                    float32 orthoH = pCameraComp->getOrthoHeight();
                    if ( ImGui::DragFloat( "Ortho Height", &orthoH, 0.1f, 0.1f, 100.0f ) )
                        pCameraComp->setOrthoHeight( orthoH );
                }
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( CameraComponent, CameraComponentInspector );
} // namespace sw::editor
