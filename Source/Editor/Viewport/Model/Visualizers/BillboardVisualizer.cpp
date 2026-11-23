/**
 * @file BillboardVisualizer.cpp
 * @brief 메시가 없는 컴포넌트(빛 · 카메라 · 바람)의 아이콘 배지를 도형 값(`EditorVisualizerBillboard`)으로 모읍니다(툴바 "Icons").
 * @details 언리얼 UBillboardComponent(에디터 전용 스프라이트) · 유니티 Gizmo 아이콘 · Godot 3D 기즈모 아이콘 자리다. 그리기는 `EditorViewportVisualizer::drawAll` 이 값만 보고 하고,
 *          그 화면 자리(`EditorVisualizerScreenBillboard`)로 클릭 선택(`EditorViewportPicker`)이 레이 피킹보다 먼저 배지를 집는다.
 */
#include "pch.h"

#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorCanvasProjection.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/LightComponent.h"

namespace sw::editor
{
    namespace
    {
        struct BillboardVisualizerInternal
        {
            /** @brief 컴포넌트의 월드 위치입니다. 씬 컴포넌트면 그것, 아니면 오브젝트의 주 씬 컴포넌트입니다. */
            [[nodiscard]] static bool findWorldPosition( GameObject& object, Component& component, float3& outPosition )
            {
                const SceneComponent* pScene = isA<SceneComponent>( &component ) ? static_cast<const SceneComponent*>( &component ) : object.getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return false;
                outPosition = pScene->getWorldPosition();
                return true;
            }

            /** @brief 컴포넌트가 배지를 가질 수 있고 캔버스 안에 보이면 더합니다. 오브젝트마다 하나만 둔다(빛과 카메라가 같이 있어도 배지가 겹치지 않게). */
            static void addComponent( const EditorViewportVisualizerArgs& args, Component* pComponent, EditorVisualizerDrawList& outDrawList )
            {
                if ( pComponent == nullptr || pComponent == args._pActiveCamera )
                    return;
                GameObject* pObject           = pComponent->getOwner();
                const bool  bObjectNotVisible = pObject == nullptr || pObject->isPendingDestroy() || pObject->isActiveInHierarchy() == false;
                if ( bObjectNotVisible )
                    return;
                const uint64 objectID = pObject->getObjectID();
                for ( const EditorVisualizerBillboard& badge : outDrawList._listBillboard )
                {
                    if ( badge._objectID == objectID )
                        return;
                }
                const EditorComponentIconRow row = EditorComponentIconRow::findRow( pComponent->getTypeInfo() );
                float3                       position{};
                float2                       screen{};
                const bool                   bBillboardNotProjectable =
                    row._bBillboard == false || findWorldPosition( *pObject, *pComponent, position ) == false ||
                    EditorCanvasProjection::projectPoint( *args._pViewProj, position, args._canvasPos, args._canvasSize, screen ) == false;
                if ( bBillboardNotProjectable || EditorCanvasProjection::isInsideCanvas( screen, args._canvasPos, args._canvasSize ) == false )
                    return;
                EditorVisualizerBillboard badge{};
                badge._objectID    = objectID;
                badge._componentID = pComponent->getComponentID();
                badge._row         = row;
                badge._position    = position;
                badge._name        = pObject->getName().c_str();
                outDrawList._listBillboard.push_back( badge );
            }

            /** @brief 레지스트리의 빌보드 종류 컴포넌트 가운데 화면에 들어오는 것을 모읍니다. 씬 전체를 훑지 않고 레지스트리만 본다(그 종류의 수만큼 든다). */
            static void gather( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.billboards" );
                const GameObjectManager* pManager = args._pObjectManager;
                if ( pManager == nullptr || args._pViewProj == nullptr )
                    return;
                for ( CameraComponent* pCamera : pManager->getCameraRegistry().getAll() )
                {
                    addComponent( args, pCamera, outDrawList );
                }
                const ComponentRegistry& registry = pManager->getComponentRegistry();
                for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
                {
                    for ( LightComponent* pLight : registry.getAll<LightComponent>( lightType ) )
                    {
                        addComponent( args, pLight, outDrawList );
                    }
                }
                for ( WindComponent* pWind : registry.getAll<WindComponent>() )
                {
                    addComponent( args, pWind, outDrawList );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( Billboard, "billboard", 50, SW_EDITOR_ICON_NAME( "LightPoint" ), SW_LOCKEYREF( "EdVisualizer", "BillboardLabel" ),
                                 SW_LOCKEYREF( "EdVisualizer", "BillboardTip" ), true, &BillboardVisualizerInternal::gather );
} // namespace sw::editor
