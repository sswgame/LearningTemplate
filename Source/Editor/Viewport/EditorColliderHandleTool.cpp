#include "pch.h"

#include "Editor/Viewport/EditorColliderHandleTool.h"

#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorColliderHandleMath.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @class EditorColliderHandleToolState
         * @brief 충돌체 손잡이 도구의 끌기 상태와 그 상태를 바꾸는 일(손잡이 그리기 · 집기 · 끌기 · 끝내기)입니다. 에디터 범위의 상태 저장소가 소유한다(`get`).
         */
        class EditorColliderHandleToolState
        {
        public:
            /** @brief 손잡이 사각형의 반 변(픽셀)입니다. 마우스가 올라오면 키운다. */
            static constexpr float32 kHandleHalfSize = 4.0f;
            /** @brief 손잡이를 집을 수 있는 마우스 거리(픽셀)입니다. */
            static constexpr float32 kPickRadius = 9.0f;

            EditorColliderHandleToolState();

            /** @brief 지금 에디터 범위의 도구 상태입니다. */
            static EditorColliderHandleToolState& get() { return getEditorState<EditorColliderHandleToolState>(); }

            /** @brief 선택된 강체의 손잡이를 그리고 끌기를 처리합니다(`EditorColliderHandleTool::process`). */
            void process( const EditorViewportVisualizerArgs& args )
            {
                _bCapturing       = SW_FALSE;
                _frame            = ImGui::GetFrameCount();
                _drawnHandleCount = 0;

                // 끌고 있는 오브젝트가 있으면 그것, 아니면 단일 선택된 오브젝트입니다.
                GameObject* pObject = nullptr;
                if ( _bDragging )
                    pObject = editor::findGameObject( _object );
                else if ( editor::getMainSceneSelection().getSelectedObjectCount() == 1 )
                    pObject = editor::getMainSceneSelection().getPrimaryObject();
                RigidBodyComponent* pBody               = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
                const bool          bHandlesUnavailable = pBody == nullptr || args._pViewProj == nullptr || args._pDrawList == nullptr || EditorPlaySession::areSceneEditsAllowed() == false;
                if ( bHandlesUnavailable )
                {
                    if ( _bDragging )
                        endDrag( false );
                    return;
                }

                float3     position{};
                quaternion rotation{};
                float3     scale{};
                if ( PhysicsComponent::readWorldPose( *pBody, position, rotation, scale ) == false )
                {
                    if ( _bDragging )
                        endDrag( false ); // 배율 0 — 잡을 셰이프가 없다
                    return;
                }

                if ( _bDragging )
                {
                    if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
                    {
                        endDrag( true );
                        return;
                    }
                    _bCapturing = SW_TRUE;
                    if ( dragHandle( *pBody, args, scale ) )
                        _bChanged = SW_TRUE;
                }

                _listHandle.clear();
                const vector<PhysicsShapeDesc3D>& listShape = pBody->getShapes();
                for ( uint32 shapeIndex = 0; shapeIndex < listShape.size(); ++shapeIndex )
                {
                    EditorColliderHandleMath::collectHandles( listShape[shapeIndex], shapeIndex, position, rotation, scale, _listHandle );
                }

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                const ColliderHandle* pHovered        = nullptr;
                float32               hoveredDistance = kPickRadius;
                for ( const ColliderHandle& handle : _listHandle )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    ++_drawnHandleCount;
                    recordHandle( handle, screen );
                    const bool    bActive                = _bDragging && handle._shapeIndex == _active._shapeIndex && handle._axis == _active._axis && handle._sign == _active._sign;
                    const float32 dx                     = mouse.x - screen.x;
                    const float32 dy                     = mouse.y - screen.y;
                    const float32 distance               = ::sqrtf( dx * dx + dy * dy );
                    const bool    bNearestHoverCandidate = _bDragging == false && bMouseInside && args._bGizmoDragging == false && distance <= hoveredDistance;
                    if ( bNearestHoverCandidate )
                    {
                        hoveredDistance = distance;
                        pHovered        = &handle;
                    }
                    const bool   bHighlight = bActive || pHovered == &handle;
                    const float2 half{ kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ), kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ) };
                    const ImVec2 min{ screen.x - half._x, screen.y - half._y };
                    const ImVec2 max{ screen.x + half._x, screen.y + half._y };
                    args._pDrawList->AddRectFilled( min, max, EditorColorUtil::toImU32( bHighlight ? SW_THEME_COLOR( "Viewport", "SelectionOutline" ) : SW_THEME_COLOR( "Base", "Text" ).withAlpha( 0.95f ) ) );
                    args._pDrawList->AddRect( min, max, EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "HandleOutline" ) ) );
                }

                if ( _bDragging || pHovered == nullptr )
                    return;
                _bCapturing = SW_TRUE;
                if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                {
                    _active     = *pHovered;
                    _object     = pObject->getHandle();
                    _undoBefore = EditorTransaction::captureSnapshot( pObject );
                    _bDragging  = SW_TRUE;
                    _bChanged   = SW_FALSE;
                }
            }

            /** @brief 이번 프레임에 손잡이가 마우스를 가져갔는지입니다. */
            bool   isCapturingMouse() const { return _bCapturing && _frame == ImGui::GetFrameCount(); }
            uint32 getCommitCount() const { return _commitCount; }

            /** @brief 감시 값 `Editor.ColliderHalfExtentX` 가 읽는 값 — 단일 선택 강체의 첫 셰이프 반 크기 X 입니다. 없으면 0 입니다. */
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
                outValue = static_cast<float64>( get()._commitCount );
                return true;
            }

            [[nodiscard]] static bool readDrawnHandleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( get()._drawnHandleCount );
                return true;
            }

        private:
            /** @brief 끌기를 정리합니다. 셰이프가 바뀌었고 오브젝트가 아직 있으면 끌기 전 상태로 되돌리기를 남깁니다. */
            void endDrag( bool bCommit )
            {
                EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
                const bool        bCommitDrag = bCommit && _bChanged && EditorPlaySession::areSceneEditsAllowed();
                if ( bCommitDrag )
                {
                    GameObject* pObject = editor::findGameObject( _object );
                    if ( pObject != nullptr && _undoBefore._xml.empty() == false )
                    {
                        EditorSceneCommands::commitModify( transaction, pObject, _undoBefore, "Resize Collider" );
                        ++_commitCount;
                    }
                }
                _object     = GameObjectHandle{};
                _undoBefore = ObjectSnapshot{};
                _bDragging  = SW_FALSE;
                _bChanged   = SW_FALSE;
            }

            /** @brief 시나리오가 손잡이를 누를 수 있도록 화면 사각형을 기록합니다(`collider.handle.<셰이프>.<축><부호>`). */
            static void recordHandle( const ColliderHandle& handle, const ImVec2& screen )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static const utf8* const                 arrAxisName[3] = { "x", "y", "z" };
                StringBuilder<constant::buffer::kMax256> keyBuilder;
                keyBuilder.appendFormat( "collider.handle.%#.%#%#", handle._shapeIndex, arrAxisName[handle._axis], ( handle._sign > 0.0f ? "+" : "-" ) );
                const string key{ keyBuilder.view() };
                EditorSelfTestMarks::recordRect( key.c_str(), float2{ screen.x - kHandleHalfSize, screen.y - kHandleHalfSize },
                                                 float2{ screen.x + kHandleHalfSize, screen.y + kHandleHalfSize } );
            }

            /** @brief 지금 끌고 있는 손잡이로 셰이프를 고칩니다. 바뀌었으면 true 입니다. */
            bool dragHandle( RigidBodyComponent& body, const EditorViewportVisualizerArgs& args, const float3& bodyScale ) const
            {
                const vector<PhysicsShapeDesc3D>& listShape = body.getShapes();
                if ( _active._shapeIndex >= listShape.size() )
                    return false;
                Ray ray{};
                if ( EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray ) == false )
                    return false;
                const float32 distance = EditorColliderHandleMath::computeClosestAxisParameter( ray._origin, ray._direction, _active._center, _active._direction );
                // 축은 중심에서 손잡이 쪽을 향하므로 레이가 가리키는 점의 거리가 곧 새 가장자리까지의 거리다.
                const PhysicsShapeDesc3D& current         = listShape[_active._shapeIndex];
                PhysicsShapeDesc3D        resized         = EditorColliderHandleMath::applyDrag( current, _active, distance, bodyScale );
                const bool                bShapeUnchanged = resized._halfExtents._x == current._halfExtents._x && resized._halfExtents._y == current._halfExtents._y &&
                                             resized._halfExtents._z == current._halfExtents._z && resized._radius == current._radius && resized._halfHeight == current._halfHeight;
                if ( bShapeUnchanged )
                    return false;
                vector<PhysicsShapeDesc3D> listResized = listShape;
                listResized[_active._shapeIndex]       = resized;
                body.setShapes( listResized );
                return true;
            }

        private:
            GameObjectHandle       _object;
            ObjectSnapshot         _undoBefore;
            ColliderHandle         _active;
            vector<ColliderHandle> _listHandle; ///< 프레임마다 지우고 다시 채우는 손잡이 재사용 버퍼(UI 스레드 하나만 쓴다)
            int32                  _frame;
            uint32                 _commitCount;
            uint32                 _drawnHandleCount;
            uint8                  _bDragging  : 1;
            uint8                  _bChanged   : 1;
            uint8                  _bCapturing : 1;
        };

        EditorColliderHandleToolState::EditorColliderHandleToolState()
            : _object{}
            , _undoBefore{}
            , _active{}
            , _listHandle{}
            , _frame{ -1 }
            , _commitCount{ 0 }
            , _drawnHandleCount{ 0 }
            , _bDragging{ SW_FALSE }
            , _bChanged{ SW_FALSE }
            , _bCapturing{ SW_FALSE }
        {
        }
    } // namespace

    void EditorColliderHandleTool::process( const EditorViewportVisualizerArgs& args )
    {
        EditorColliderHandleToolState::get().process( args );
    }

    bool EditorColliderHandleTool::isCapturingMouse()
    {
        return EditorColliderHandleToolState::get().isCapturingMouse();
    }

    uint32 EditorColliderHandleTool::getCommitCount()
    {
        return EditorColliderHandleToolState::get().getCommitCount();
    }

    SW_AUTOMATION_WATCH( editorColliderHalfExtentX, "Editor.ColliderHalfExtentX", "Half size X of the first shape of the selected RigidBody (0 when none)",
                         &EditorColliderHandleToolState::readHalfExtentX );
    SW_AUTOMATION_WATCH( editorColliderHandleCommitCount, "Editor.ColliderHandleCommitCount", "Collider handle drags that finished and left an undo entry",
                         &EditorColliderHandleToolState::readCommitCount );
    SW_AUTOMATION_WATCH( editorColliderHandleCount, "Editor.ColliderHandleCount", "Collider handles drawn in the last frame",
                         &EditorColliderHandleToolState::readDrawnHandleCount );
} // namespace sw::editor
