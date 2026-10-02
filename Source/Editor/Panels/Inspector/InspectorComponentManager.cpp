#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

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
            static void drawTransformInspector( SceneComponent* pSceneComp )
            {
                if ( pSceneComp == nullptr )
                    return;

                ImGui::SeparatorText( "Transform" );

                EditorWidgets::drawGizmoOperationControls();

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
            }

            /** @brief SceneComponent 와 그 하위 타입 전부의 트랜스폼 및 기즈모 컨트롤 */
            class SceneComponentInspector : public IInspectorComponent
            {
            public:
                void drawSection( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    drawTransformInspector( static_cast<SceneComponent*>( pComponent ) );
                }

                void collectDrawnProperties( vector<hashed_string>& outListName ) const override
                {
                    static const hashed_string s_arrName[] = { hashed_string( "_localPosition" ), hashed_string( "_localRotation" ),
                                                               hashed_string( "_localScale" ) };
                    outListName.insert( outListName.end(), std::begin( s_arrName ), std::end( s_arrName ) );
                }
            };

            /** @brief CameraComponent 의 투영 컨트롤(트랜스폼은 SceneComponent 단계가 그린다) */
            class CameraComponentInspector : public IInspectorComponent
            {
            public:
                void collectDrawnProperties( vector<hashed_string>& outListName ) const override
                {
                    static const hashed_string s_arrName[] = { hashed_string( "_fovY" ), hashed_string( "_nearZ" ), hashed_string( "_farZ" ),
                                                               hashed_string( "_bOrthographic" ), hashed_string( "_orthoHeight" ) };
                    outListName.insert( outListName.end(), std::begin( s_arrName ), std::end( s_arrName ) );
                }

                void drawSection( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
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
                    EditorContext* pContext = EditorContext::get();
                    if ( pContext == nullptr )
                        return;

                    if ( ImGui::SmallButton( "Open Sprite Clip Tool" ) )
                    {
                        pContext->getWorkspace().requestOpenPanel(
                            EditorAssetTypeRegistry::getPanelTitle( EditorAssetKind::SpriteClip ) );
                    }
                }
            };

            /** @brief MeshComponent 의 가시성(반사 프로퍼티가 아니다 — 트랜스폼은 SceneComponent 단계가, 메시 칸은 반사 프로퍼티가 그린다) */
            class MeshComponentInspector : public IInspectorComponent
            {
            public:
                void drawSection( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
                {
                    MeshComponent* pMeshComp = static_cast<MeshComponent*>( pComponent );
                    bool           bVisible  = pMeshComp->isVisible();
                    if ( ImGui::Checkbox( "Visible", &bVisible ) )
                        pMeshComp->setVisible( bVisible );
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

    void InspectorComponentManager::collectForType( const TypeInfo& type, vector<IInspectorComponent*>& outListInspector ) const
    {
        outListInspector.clear();
        vector<const TypeInfo*> listType;
        InspectorPropertyLayout::collectTypeChain( type, listType );
        for ( const TypeInfo* pType : listType )
        {
            IInspectorComponent* pInspector = find( pType->_name.c_str() );
            if ( pInspector != nullptr )
                outListInspector.push_back( pInspector );
        }
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
