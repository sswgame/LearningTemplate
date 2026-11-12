#include "pch.h"

#include "Editor/Viewport/EditorViewportPicker.h"

#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorViewportBillboardItem.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPickerInternal
        {
            static constexpr float64 kSlowHoverPickSeconds  = 0.002; ///< 이보다 오래 걸린 호버 피킹은 비싸다고 판단한다
            static constexpr float64 kSlowHoverPickInterval = 0.1;   ///< 비싸면 이만큼 간격을 둔다(초)
            static constexpr uint32  kBoxCornerCount        = 8;     ///< 상자 모서리 수(비트 0 · 1 · 2 가 X · Y · Z 의 최소 · 최대)

            /** @brief 누를 때의 수정 키로 고르는 방식을 정합니다. Ctrl 넣고 빼기 · Shift 더하기(언리얼 뷰포트 · 아웃라이너와 같다). */
            static SelectionMode readSelectionMode()
            {
                const uint8 modifierMask = EditorInputGUI::getModifierMask();
                if ( ( modifierMask & ModifierKey::Ctrl ) != 0 )
                    return SelectionMode::Toggle;
                if ( ( modifierMask & ModifierKey::Shift ) != 0 )
                    return SelectionMode::Add;
                return SelectionMode::Replace;
            }

            /**
             * @brief 마키 사각형이 오브젝트에 걸치는지 봅니다. 크기를 선언한 오브젝트(`GameObject::getWorldBox`)는 상자 여덟 모서리의 화면 사각형이 마키와 겹치면, 크기가 없으면 피벗이 마키 안이면 걸친 것입니다.
             * @details 피벗 한 점만 보면 큰 물체를 반쯤 덮어도 고르지 못한다(감사 E-10). 언리얼 박스 선택의 기본(걸치면 고름)과 유니티 `HandleUtility.PickRectObjects` 가 같다.
             *          shortcut: 카메라 뒤 모서리는 빼고 앞 모서리만으로 사각형을 만든다 — 카메라를 품은 큰 물체는 화면 사각형이 작게 잡힌다. id 버퍼 피킹이 들어오면 사각형 안 픽셀 id 로 바꾼다.
             */
            static bool isTouchedByMarquee( const GameObject& object, const SceneComponent& root, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize,
                                            const float2& minPos, const float2& maxPos )
            {
                AABB box{};
                if ( object.getWorldBox( box ) == false )
                {
                    ImVec2 screenPt{};
                    if ( EditorViewportProjectionUtil::projectPoint( viewProj, root.getWorldPosition(), canvasPos, canvasSize, screenPt ) == false )
                        return false;
                    return minPos._x <= screenPt.x && screenPt.x <= maxPos._x && minPos._y <= screenPt.y && screenPt.y <= maxPos._y;
                }
                float2 screenMin{ MathUtil::kMaxFloat, MathUtil::kMaxFloat };
                float2 screenMax{ -MathUtil::kMaxFloat, -MathUtil::kMaxFloat };
                bool   bAnyCorner = false;
                for ( uint32 corner = 0; corner < kBoxCornerCount; ++corner )
                {
                    const float3 point{ ( corner & 1u ) != 0 ? box._max._x : box._min._x, ( corner & 2u ) != 0 ? box._max._y : box._min._y,
                                        ( corner & 4u ) != 0 ? box._max._z : box._min._z };
                    ImVec2 screenPt{};
                    if ( EditorViewportProjectionUtil::projectPoint( viewProj, point, canvasPos, canvasSize, screenPt ) == false )
                        continue;
                    screenMin  = float2{ MathUtil::min( screenMin._x, screenPt.x ), MathUtil::min( screenMin._y, screenPt.y ) };
                    screenMax  = float2{ MathUtil::max( screenMax._x, screenPt.x ), MathUtil::max( screenMax._y, screenPt.y ) };
                    bAnyCorner = true;
                }
                return bAnyCorner && screenMin._x <= maxPos._x && minPos._x <= screenMax._x && screenMin._y <= maxPos._y && minPos._y <= screenMax._y;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportPicker::EditorViewportPicker()
        : _hoveredObjectID{ 0 }
        , _lastHoverPickSeconds{ 0.0 }
        , _lastHoverPickTime{ 0.0 }
        , _hoverMousePos{ -1.0f, -1.0f }
        , _marqueeStart{ 0.0f, 0.0f }
        , _marqueeMode{ static_cast<uint8>( SelectionMode::Replace ) }
        , _bMarqueeArmed{ SW_FALSE }
    {
    }

    void EditorViewportPicker::processClick( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasClicked, bool bGizmoBusy,
                                             const ViewportToolbarSettings& settings )
    {
        if ( pCamera == nullptr )
            return;
        if ( bCanvasClicked == false )
            return;
        if ( bGizmoBusy )
            return;
        if ( ( EditorInputGUI::getModifierMask() & ModifierKey::Alt ) != 0 )
            return;
        GameObject*         pObject    = nullptr;
        Component*          pComponent = nullptr;
        EditorWorkspace&    workspace  = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        const SelectionMode mode       = EditorViewportPickerInternal::readSelectionMode();
        if ( findObjectUnderMouse( canvasPos, canvasSize, pCamera, settings, pObject, pComponent ) )
        {
            if ( mode == SelectionMode::Replace )
                workspace.selectComponent( pObject, pComponent );
            else
                workspace.selectGameObject( pObject, mode );
            return;
        }
        if ( mode == SelectionMode::Replace )
            workspace.clearSelection();
        _bMarqueeArmed = SW_TRUE;
        _marqueeStart  = EditorInputGUI::readPointer()._position;
        _marqueeMode   = static_cast<uint8>( mode );
    }

    void EditorViewportPicker::processMarquee( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, ImDrawList* pDrawList )
    {
        if ( _bMarqueeArmed == SW_FALSE )
            return;
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        const float2             minPos{ MathUtil::min( _marqueeStart._x, pointer._position._x ), MathUtil::min( _marqueeStart._y, pointer._position._y ) };
        const float2             maxPos{ MathUtil::max( _marqueeStart._x, pointer._position._x ), MathUtil::max( _marqueeStart._y, pointer._position._y ) };
        if ( pointer.isDown( EditorMouseButton::Left ) )
        {
            // 끌기 문턱 안이면 아직 클릭이다 — 상자를 보이지 않는다(누를 때의 손떨림이 상자로 보이지 않게).
            if ( pointer.isDragging( EditorMouseButton::Left ) )
            {
                const Color accent = SW_THEME_COLOR( "Base", "Accent" ); // 선택을 나타내는 상자라 강조색이다(앱 화면 규칙 — 강조색은 선택에)
                pDrawList->AddRectFilled( ImVec2{ minPos._x, minPos._y }, ImVec2{ maxPos._x, maxPos._y }, EditorColorUtil::toImU32( Color{ accent._r, accent._g, accent._b, 0.15f } ) );
                pDrawList->AddRect( ImVec2{ minPos._x, minPos._y }, ImVec2{ maxPos._x, maxPos._y }, EditorColorUtil::toImU32( accent ) );
            }
            return;
        }
        _bMarqueeArmed         = SW_FALSE;
        const bool bWasDragged = pointer.wasClickedWithoutDrag( EditorMouseButton::Left ) == false;
        Scene*     pScene      = editor::getActiveScene();
        if ( bWasDragged == false || pCamera == nullptr || pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms();
        const float32          aspect        = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
        const float4x4         viewProj      = pCamera->getViewProjectionMatrix( aspect );
        const EditorWorkspace& workspace     = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        const GameObject*      pEditorCamera = pCamera->getOwner();
        vector<GameObject*>    listInside;
        pManager->forEachGameObject(
            [&]( GameObject* pObj )
        {
            const SceneComponent* pRoot    = pObj->getPrimarySceneComponent();
            const bool            bSkipped = pObj == pEditorCamera || pRoot == nullptr || pObj->isRenderHidden() || workspace.isObjectLocked( pObj->getObjectID() );
            if ( bSkipped )
                return;
            if ( EditorViewportPickerInternal::isTouchedByMarquee( *pObj, *pRoot, viewProj, canvasPos, canvasSize, minPos, maxPos ) )
                listInside.push_back( pObj );
        } );
        if ( listInside.empty() == false )
            editor::getMainSceneSelection().selectObjects( listInside, static_cast<SelectionMode>( _marqueeMode ) );
    }

    void EditorViewportPicker::processHover( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered, bool bGizmoBusy,
                                             const ViewportToolbarSettings& settings )
    {
        // 호버는 마우스가 씬 뷰 위에만 있고(다른 패널이나 팝업이 마우스를 가지고 있지 않고) 드래그, 비행, 궤도, 기즈모 위가 아닐 때만 처리한다(언리얼과 유니티의 호버 강조).
        const EditorPointerState pointer     = EditorInputGUI::readPointer();
        const bool               bAnyButton  = pointer.isDown( EditorMouseButton::Left ) || pointer.isDown( EditorMouseButton::Right ) || pointer.isDown( EditorMouseButton::Middle );
        const bool               bNavigating = pointer.hasModifier( ModifierKey::Alt ) || bAnyButton;
        const bool               bBlocked    = bCanvasHovered == false || pCamera == nullptr || bNavigating || ImGui::IsAnyItemActive() || bGizmoBusy;
        if ( bBlocked )
        {
            _hoveredObjectID = 0;
            _hoverMousePos   = float2{ -1.0f, -1.0f };
            return;
        }
        // 마우스가 그대로면 지난 결과를 쓴다. 프레임당 많아야 한 번이고 대개는 0번이다. 카메라를 움직이는 동안은 위에서 막힌다.
        const float2 mouse = pointer._position;
        if ( mouse._x == _hoverMousePos._x && mouse._y == _hoverMousePos._y )
            return;
        // 피킹이 비싼 씬은 마우스가 움직이는 동안 0.1초에 한 번만 찾는다. 레이 피킹은 오브젝트 수에 비례하며, 8000개이면 Debug 에서 약 20 ms 가 걸린다.
        // 이때 마우스 위치를 기록하지 않고 반환하므로, 마우스가 멈춘 뒤 다음 기회에 마지막 위치로 다시 찾는다.
        const float64 now = ImGui::GetTime();
        if ( _lastHoverPickSeconds > EditorViewportPickerInternal::kSlowHoverPickSeconds && now - _lastHoverPickTime < EditorViewportPickerInternal::kSlowHoverPickInterval )
            return;
        _hoverMousePos = mouse;
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.hoverPick" );
        const int64 startNanos = MonotonicClock::nowNanoseconds();
        GameObject* pObject    = nullptr;
        Component*  pComponent = nullptr;
        _hoveredObjectID       = findObjectUnderMouse( canvasPos, canvasSize, pCamera, settings, pObject, pComponent ) ? pObject->getObjectID() : 0;
        // 이미 선택한 오브젝트는 호버로 취급하지 않는다. 선택 상자와 기즈모가 이미 있다. 그 위의 기즈모 핸들과 겹치는 판정도 여기서 없어진다.
        const bool bHoverAlreadySelected = pObject != nullptr && editor::getMainSceneSelection().hasObject( pObject );
        if ( bHoverAlreadySelected )
            _hoveredObjectID = 0;
        _lastHoverPickSeconds = static_cast<float64>( MonotonicClock::nowNanoseconds() - startNanos ) * 1e-9;
        _lastHoverPickTime    = now;
    }

    bool EditorViewportPicker::findObjectUnderMouse( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, const ViewportToolbarSettings& settings,
                                                     GameObject*& pOutObject, Component*& pOutComponent ) const
    {
        pOutObject                     = nullptr;
        pOutComponent                  = nullptr;
        Scene*     pScene              = editor::getActiveScene();
        const bool bPickContextMissing = pCamera == nullptr || pScene == nullptr || pScene->getObjectManager() == nullptr;
        if ( bPickContextMissing )
            return false;
        EditorWorkspace& workspace   = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        const float32    aspect      = canvasSize._x / ( canvasSize._y > 0.0f ? canvasSize._y : 1.0f );
        const float4x4   viewProj    = pCamera->getViewProjectionMatrix( aspect );
        const float4x4   invViewProj = viewProj.invert();
        Ray              pickRay{};
        if ( EditorViewportProjectionUtil::makeMouseRay( invViewProj, canvasPos, canvasSize, pickRay ) == false )
            return false;
        GameObjectManager* pManager = pScene->getObjectManager();
        pManager->flushSceneTransforms();

        // 빌보드(라이트, 카메라, 오디오 아이콘)를 레이 피킹보다 먼저 검사한다. 메시가 없어 레이로는 집히지 않기 때문이다(언리얼 빌보드, 유니티 Gizmo 아이콘 클릭).
        // 잠근 오브젝트의 빌보드가 맞으면 그 위치는 "빈 곳" 으로 처리한다. 뒤의 레이 피킹으로 넘기지 않는다.
        const EditorVisualizerRegistration* pBillboard = EditorExtensionRegistry<EditorVisualizerRegistration>::find( EditorViewportBillboardItem::kVisualizerID );
        if ( pBillboard != nullptr && settings._visualizerToggles.isOn( *pBillboard ) )
        {
            vector<EditorViewportBillboardItem> listItem;
            EditorViewportBillboardItem::collect( *pManager, viewProj, canvasPos, canvasSize, pCamera, listItem );
            const ImVec2 mouse = ImGui::GetMousePos();
            const uint32 index = EditorViewportBillboardItem::findAt( listItem, float2{ mouse.x, mouse.y } );
            if ( index != constant::invalid_index::kUint32 )
            {
                if ( workspace.isObjectLocked( listItem[index]._pObject->getObjectID() ) )
                    return false;
                pOutObject    = listItem[index]._pObject;
                pOutComponent = listItem[index]._pComponent;
                return true;
            }
        }

        // 어떤 컴포넌트 종류를 집을 수 있는지는 EditorViewportRayPick 의 테이블이 정한다. ImGui 없이 테스트한다.
        EditorPickResult pickResult{};
        const bool       bNothingPicked = EditorViewportRayPick::pick( pManager, pickRay, settings._bIs2DMode, pickResult ) == false || pickResult._pObject == nullptr ||
                                    workspace.isObjectLocked( pickResult._pObject->getObjectID() ) || pickResult._pObject->isRenderHidden();
        if ( bNothingPicked )
            return false;
        pOutObject    = pickResult._pObject;
        pOutComponent = pickResult._pComponent;
        return true;
    }
} // namespace sw::editor
