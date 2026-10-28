#include "pch.h"

#include "Editor/Viewport/EditorColliderHandleTool.h"

#include "Core/Container/GameObjectHandle.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorColliderHandleMath.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorColliderHandleToolInternal
        {
            /** @brief 손잡이 사각형의 반 변(픽셀)입니다. 마우스가 올라오면 키운다. */
            static constexpr float32 kHandleHalfSize = 4.0f;
            /** @brief 손잡이를 집을 수 있는 마우스 거리(픽셀)입니다. */
            static constexpr float32 kPickRadius = 9.0f;

            struct State
            {
                GameObjectHandle       _object{};
                ObjectSnapshot         _undoBefore{};
                ColliderHandle         _active{};
                vector<ColliderHandle> _listHandle{}; ///< 프레임마다 지우고 다시 채우는 손잡이 재사용 버퍼(UI 스레드 하나만 쓴다)
                bool                   _bDragging{ false };
                bool                   _bChanged{ false };
                bool                   _bCapturing{ false };
                int32                  _frame{ -1 };
                uint32                 _commitCount{ 0 };
                uint32                 _drawnHandleCount{ 0 };
            };

            /** @brief 도구의 끌기 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
            static State& getState() { return getEditorState<State>(); }

            /** @brief 끌기를 정리합니다. 셰이프가 바뀌었고 오브젝트가 아직 있으면 끌기 전 상태로 되돌리기를 남깁니다. */
            static void endDrag( State& state, bool bCommit )
            {
                const bool bCommitDrag = bCommit && state._bChanged && EditorPlaySession::areSceneEditsAllowed();
                if ( bCommitDrag )
                {
                    GameObject* pObject = editor::findGameObject( state._object );
                    if ( pObject != nullptr && state._undoBefore._xml.empty() == false )
                    {
                        EditorSceneCommands::commitModify( pObject, state._undoBefore, "Resize Collider" );
                        ++state._commitCount;
                    }
                }
                state._object     = GameObjectHandle{};
                state._undoBefore = ObjectSnapshot{};
                state._bDragging  = false;
                state._bChanged   = false;
            }

            /** @brief 시나리오가 손잡이를 누를 수 있도록 화면 사각형을 기록합니다(`collider.handle.<셰이프>.<축><부호>`). */
            static void noteHandle( const ColliderHandle& handle, const ImVec2& screen )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static const utf8* const arrAxisName[3] = { "x", "y", "z" };
                const string             key            = "collider.handle." + to_string( handle._shapeIndex ) + "." + arrAxisName[handle._axis] + ( handle._sign > 0.0f ? "+" : "-" );
                EditorSelfTestMarks::noteRect( key.c_str(), float2{ screen.x - kHandleHalfSize, screen.y - kHandleHalfSize },
                                               float2{ screen.x + kHandleHalfSize, screen.y + kHandleHalfSize } );
            }

            /** @brief 지금 끌고 있는 손잡이로 셰이프를 고칩니다. 바뀌었으면 true 입니다. */
            static bool dragHandle( State& state, RigidBodyComponent& body, const EditorViewportVisualizerArgs& args, const float3& bodyScale )
            {
                const vector<PhysicsShapeDesc3D>& listShape = body.getShapes();
                if ( state._active._shapeIndex >= listShape.size() )
                    return false;
                EditorPickRay ray{};
                if ( EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray ) == false )
                    return false;
                const float32 distance = EditorColliderHandleMath::computeClosestAxisParameter( ray._origin, ray._direction, state._active._center, state._active._direction );
                // 축은 중심에서 손잡이 쪽을 향하므로 레이가 가리키는 점의 거리가 곧 새 가장자리까지의 거리다.
                const PhysicsShapeDesc3D& current         = listShape[state._active._shapeIndex];
                PhysicsShapeDesc3D        resized         = EditorColliderHandleMath::applyDrag( current, state._active, distance, bodyScale );
                const bool                bShapeUnchanged = resized._halfExtents._x == current._halfExtents._x && resized._halfExtents._y == current._halfExtents._y &&
                                             resized._halfExtents._z == current._halfExtents._z && resized._radius == current._radius && resized._halfHeight == current._halfHeight;
                if ( bShapeUnchanged )
                    return false;
                vector<PhysicsShapeDesc3D> listResized = listShape;
                listResized[state._active._shapeIndex] = resized;
                body.setShapes( listResized );
                return true;
            }

            /** @brief 탐침 `Editor.ColliderHalfExtentX` 가 읽는 값 — 단일 선택 강체의 첫 셰이프 반 크기 X 입니다. 없으면 0 입니다. */
            [[nodiscard]] static bool readHalfExtentX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue                           = 0.0;
                EditorContext*            pContext = editor::findSubsystem<EditorContext>();
                const GameObject*         pObject  = pContext != nullptr ? editor::getMainSceneSelection().getPrimaryObject() : nullptr;
                const RigidBodyComponent* pBody    = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
                if ( pBody != nullptr && pBody->getShapes().empty() == false )
                    outValue = static_cast<float64>( pBody->getShapes()[0]._halfExtents._x );
                return true;
            }

            [[nodiscard]] static bool readCommitCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getState()._commitCount );
                return true;
            }

            [[nodiscard]] static bool readDrawnHandleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getState()._drawnHandleCount );
                return true;
            }
        };
    } // namespace

    void EditorColliderHandleTool::process( const EditorViewportVisualizerArgs& args )
    {
        using Internal          = EditorColliderHandleToolInternal;
        Internal::State& state  = Internal::getState();
        state._bCapturing       = false;
        state._frame            = ImGui::GetFrameCount();
        state._drawnHandleCount = 0;

        // 끌고 있는 오브젝트가 있으면 그것, 아니면 단일 선택된 오브젝트입니다.
        GameObject* pObject = nullptr;
        if ( state._bDragging )
            pObject = editor::findGameObject( state._object );
        else if ( editor::getMainSceneSelection().getSelectedObjectCount() == 1 )
            pObject = editor::getMainSceneSelection().getPrimaryObject();
        RigidBodyComponent* pBody               = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
        const bool          bHandlesUnavailable = pBody == nullptr || args._pViewProj == nullptr || args._pDrawList == nullptr || EditorPlaySession::areSceneEditsAllowed() == false;
        if ( bHandlesUnavailable )
        {
            if ( state._bDragging )
                Internal::endDrag( state, false );
            return;
        }

        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *pBody, position, rotation, scale );

        if ( state._bDragging )
        {
            if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
            {
                Internal::endDrag( state, true );
                return;
            }
            state._bCapturing = true;
            state._bChanged   = Internal::dragHandle( state, *pBody, args, scale ) || state._bChanged;
        }

        vector<ColliderHandle>& listHandle = state._listHandle;
        listHandle.clear();
        const vector<PhysicsShapeDesc3D>& listShape = pBody->getShapes();
        for ( uint32 shapeIndex = 0; shapeIndex < listShape.size(); ++shapeIndex )
        {
            EditorColliderHandleMath::collectHandles( listShape[shapeIndex], shapeIndex, position, rotation, scale, listHandle );
        }

        const ImVec2 mouse        = ImGui::GetMousePos();
        const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                  mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
        const ColliderHandle* pHovered        = nullptr;
        float32               hoveredDistance = Internal::kPickRadius;
        for ( const ColliderHandle& handle : listHandle )
        {
            ImVec2 screen;
            if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._position, args._canvasPos, args._canvasSize, screen ) == false )
                continue;
            ++state._drawnHandleCount;
            Internal::noteHandle( handle, screen );
            const bool bActive = state._bDragging && handle._shapeIndex == state._active._shapeIndex && handle._axis == state._active._axis &&
                                 handle._sign == state._active._sign;
            const float32 dx                     = mouse.x - screen.x;
            const float32 dy                     = mouse.y - screen.y;
            const float32 distance               = ::sqrtf( dx * dx + dy * dy );
            const bool    bNearestHoverCandidate = state._bDragging == false && bMouseInside && args._bGizmoDragging == false && distance <= hoveredDistance;
            if ( bNearestHoverCandidate )
            {
                hoveredDistance = distance;
                pHovered        = &handle;
            }
            const bool   bHighlight = bActive || pHovered == &handle;
            const float2 half{ Internal::kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ), Internal::kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ) };
            const ImVec2 min{ screen.x - half._x, screen.y - half._y };
            const ImVec2 max{ screen.x + half._x, screen.y + half._y };
            args._pDrawList->AddRectFilled( min, max, EditorColorUtil::toImU32( bHighlight ? EditorThemeUtil::getSelectionOutlineColor() : EditorThemeUtil::getTextColor().withAlpha( 0.95f ) ) );
            args._pDrawList->AddRect( min, max, EditorColorUtil::toImU32( Color{ 0.05f, 0.05f, 0.05f, 1.0f } ) );
        }

        if ( state._bDragging )
            return;
        if ( pHovered != nullptr )
        {
            state._bCapturing = true;
            if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
            {
                state._active     = *pHovered;
                state._object     = pObject->getHandle();
                state._undoBefore = EditorTransaction::captureSnapshot( pObject );
                state._bDragging  = true;
                state._bChanged   = false;
            }
        }
    }

    bool EditorColliderHandleTool::isCapturingMouse()
    {
        const EditorColliderHandleToolInternal::State& state = EditorColliderHandleToolInternal::getState();
        return state._bCapturing && state._frame == ImGui::GetFrameCount();
    }

    uint32 EditorColliderHandleTool::getCommitCount()
    {
        return EditorColliderHandleToolInternal::getState()._commitCount;
    }

    SW_AUTOMATION_PROBE( editorColliderHalfExtentX, "Editor.ColliderHalfExtentX", "Half size X of the first shape of the selected RigidBody (0 when none)",
                         &EditorColliderHandleToolInternal::readHalfExtentX );
    SW_AUTOMATION_PROBE( editorColliderHandleCommitCount, "Editor.ColliderHandleCommitCount", "Collider handle drags that finished and left an undo entry",
                         &EditorColliderHandleToolInternal::readCommitCount );
    SW_AUTOMATION_PROBE( editorColliderHandleCount, "Editor.ColliderHandleCount", "Collider handles drawn in the last frame",
                         &EditorColliderHandleToolInternal::readDrawnHandleCount );
} // namespace sw::editor
