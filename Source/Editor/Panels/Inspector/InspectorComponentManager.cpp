#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorComponentManagerInternal
        {
            static bool drawTransformInspector( SceneComponent* pSceneComp )
            {
                if ( pSceneComp == nullptr )
                    return false;

                ImGui::SeparatorText( "Transform" );

                int32 op = EditorContext::get()->getWorkspace().getGizmoOperation();
                ImGui::RadioButton( "Translate", &op, 0 );
                ImGui::SameLine();
                ImGui::RadioButton( "Rotate", &op, 1 );
                ImGui::SameLine();
                ImGui::RadioButton( "Scale", &op, 2 );
                ImGui::SameLine();
                EditorContext::get()->getWorkspace().setGizmoOperation( op );

                ImGui::SameLine();
                bool bLocal = EditorContext::get()->getWorkspace().isGizmoLocalSpace();
                if ( ImGui::Checkbox( "Local", &bLocal ) )
                    EditorContext::get()->getWorkspace().setGizmoLocalSpace( bLocal );

                float3 pos = pSceneComp->getLocalPosition();
                float3 rot = pSceneComp->getLocalRotation();
                float3 scl = pSceneComp->getLocalScale();

                if ( EditorWidgets::drawVec3Control( "Position", pos, 0.0f, 80.0f, 0.1f ) )
                    pSceneComp->setLocalPosition( pos );
                if ( EditorWidgets::drawVec3Control( "Rotation", rot, 0.0f, 80.0f, 0.5f ) )
                    pSceneComp->setLocalRotation( rot );
                if ( EditorWidgets::drawVec3Control( "Scale", scl, 1.0f, 80.0f, 0.01f ) )
                    pSceneComp->setLocalScale( scl );

                const float3 world = pSceneComp->getWorldPosition();
                ImGui::TextDisabled( "World: %.2f, %.2f, %.2f",
                                     static_cast<float64>( world._x ),
                                     static_cast<float64>( world._y ),
                                     static_cast<float64>( world._z ) );
                return true;
            }

            /** @brief SceneComponent 전용 트랜스폼 및 기즈모 컨트롤 */
            class SceneComponentInspector : public IInspectorComponent
            {
            public:
                bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    return drawTransformInspector( static_cast<SceneComponent*>( pComponent ) );
                }
            };

            /** @brief CameraComponent 전용 트랜스폼 및 카메라 투영 컨트롤 */
            class CameraComponentInspector : public IInspectorComponent
            {
            public:
                bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    auto* pCameraComp = static_cast<CameraComponent*>( pComponent );
                    if ( drawTransformInspector( pCameraComp ) == false )
                        return false;

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
                    return true;
                }
            };

            /** @brief TagComponent 전용 칩 스타일 */
            class TagComponentInspector : public IInspectorComponent
            {
            public:
                void drawFooter( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    auto* pTagComp = static_cast<TagComponent*>( pComponent );
                    if ( pTagComp == nullptr )
                        return;

                    const vector<TagID>& listTag = pTagComp->getTags().getTags();
                    for ( const TagID& tag : listTag )
                    {
                        if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                        {
                            ImGui::SameLine();
                            EditorWidgets::drawChip( tag._pString, editor::style::kOk );
                        }
                    }
                }
            };

            /** @brief SpriteComponent 전용 프리뷰 및 애셋 슬롯 */
            class SpriteComponentInspector : public IInspectorComponent
            {
            public:
                void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    if ( ImGui::SmallButton( "Open Sprite Clip Tool" ) )
                    {
                        EditorContext::get()->getWorkspace().requestOpenPanel(
                            EditorAssetTypeRegistry::getPanelTitle( EditorAssetKind::SpriteClip ) );
                    }
                }
            };

            /** @brief MeshComponent 전용 트랜스폼 + 가시성 */
            class MeshComponentInspector : public IInspectorComponent
            {
            public:
                bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    MeshComponent* pMeshComp = static_cast<MeshComponent*>( pComponent );
                    if ( drawTransformInspector( pMeshComp ) == false )
                        return false;

                    bool bVisible = pMeshComp->isVisible();
                    if ( ImGui::Checkbox( "Visible", &bVisible ) )
                        pMeshComp->setVisible( bVisible );
                    return true;
                }
            };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void InspectorComponentManager::registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector )
    {
        _mapInspector[string{ typeName }] = std::move( pInspector );
    }

    IInspectorComponent* InspectorComponentManager::find( string_view typeName ) const
    {
        const auto it = _mapInspector.find( string{ typeName } );
        if ( it != _mapInspector.end() )
            return it->second.get();
        return nullptr;
    }

    void InspectorComponentManager::registerDefaults()
    {
        registerComponent<SceneComponent, InspectorComponentManagerInternal::SceneComponentInspector>();
        registerComponent<CameraComponent, InspectorComponentManagerInternal::CameraComponentInspector>();
        registerComponent<TagComponent, InspectorComponentManagerInternal::TagComponentInspector>();
        registerComponent<SpriteComponent, InspectorComponentManagerInternal::SpriteComponentInspector>();
        registerComponent<MeshComponent, InspectorComponentManagerInternal::MeshComponentInspector>();
    }
} // namespace sw::editor
