#include "pch.h"

#include "Editor/Viewport/Model/EditorColliderHandleEdit.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"
#include "Editor/Viewport/Model/EditorColliderHandleMath.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw::editor
{
    namespace
    {
        struct EditorColliderHandleEditInternal
        {
            /** @brief 셰이프 크기 값이 같으면 true 입니다(바뀌지 않은 끌기는 쓰지 않는다). */
            static bool isSameSize( const PhysicsShapeDesc3D& a, const PhysicsShapeDesc3D& b )
            {
                return a._halfExtents._x == b._halfExtents._x && a._halfExtents._y == b._halfExtents._y && a._halfExtents._z == b._halfExtents._z && a._radius == b._radius &&
                       a._halfHeight == b._halfHeight;
            }

            /** @brief 감시 값 `Editor.ColliderHalfExtentX` — 단일 선택 강체의 첫 셰이프 반 크기 X 입니다. 없으면 0 입니다. */
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
                outValue = static_cast<float64>( EditorColliderHandleSession::get()._edit.getCommitCount() );
                return true;
            }

            [[nodiscard]] static bool readDrawnHandleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( EditorColliderHandleSession::get()._drawnHandleCount );
                return true;
            }
        };
    } // namespace

    bool EditorColliderHandleDragTracker::step( bool bAvailable, bool bMouseDown, const Ray* pRay, vector<EditorColliderHandleCommand>& outListCommand )
    {
        if ( _bDragging == false )
            return bAvailable;
        const bool bEnd = bAvailable == false || bMouseDown == false;
        if ( bEnd )
        {
            EditorColliderHandleCommand command{};
            command._kind    = EditorColliderHandleCommandKind::End;
            command._object  = _object;
            command._bCommit = bAvailable; // 놓았으면 남기고, 대상이 사라졌거나 편집이 막혔으면 버린다
            outListCommand.push_back( command );
            _bDragging = false;
            _object    = GameObjectHandle{};
            return false;
        }
        if ( pRay != nullptr )
        {
            EditorColliderHandleCommand command{};
            command._kind   = EditorColliderHandleCommandKind::Drag;
            command._object = _object;
            command._handle = _active;
            command._ray    = *pRay;
            outListCommand.push_back( command );
        }
        return true;
    }

    void EditorColliderHandleDragTracker::press( GameObjectHandle object, const ColliderHandle& handle, vector<EditorColliderHandleCommand>& outListCommand )
    {
        EditorColliderHandleCommand command{};
        command._kind   = EditorColliderHandleCommandKind::Capture;
        command._object = object;
        command._handle = handle;
        outListCommand.push_back( command );
        _object    = object;
        _active    = handle;
        _bDragging = true;
    }

    EditorColliderHandleEdit::EditorColliderHandleEdit()
        : _pBefore{}
        , _object{}
        , _commitCount{ 0 }
        , _bChanged{ false }
    {
    }

    EditorColliderHandleEdit::~EditorColliderHandleEdit() = default;

    void EditorColliderHandleEdit::gather( GameObjectHandle dragged, EditorColliderHandleViewState& outState )
    {
        outState._object     = GameObjectHandle{};
        outState._bAvailable = false;
        outState._listHandle.clear();
        GameObject* pObject = nullptr;
        if ( dragged.isValid() )
            pObject = editor::findGameObject( dragged );
        else if ( editor::getMainSceneSelection().getSelectedObjectCount() == 1 )
            pObject = editor::getMainSceneSelection().getPrimaryObject();
        const RigidBodyComponent* pBody = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
        if ( pBody == nullptr || EditorPlaySession::areSceneEditsAllowed() == false )
            return;
        float3     position{};
        quaternion rotation{};
        float3     scale{};
        if ( PhysicsComponent::readWorldPose( *pBody, position, rotation, scale ) == false )
            return; // 배율 0 — 잡을 셰이프가 없다
        const vector<PhysicsShapeDesc3D>& listShape = pBody->getShapes();
        for ( uint32 shapeIndex = 0; shapeIndex < listShape.size(); ++shapeIndex )
        {
            EditorColliderHandleMath::collectHandles( listShape[shapeIndex], shapeIndex, position, rotation, scale, outState._listHandle );
        }
        outState._object     = pObject->getHandle();
        outState._bAvailable = true;
    }

    void EditorColliderHandleEdit::apply( const vector<EditorColliderHandleCommand>& listCommand )
    {
        for ( const EditorColliderHandleCommand& command : listCommand )
        {
            switch ( command._kind )
            {
                case EditorColliderHandleCommandKind::Capture:
                    capture( command._object );
                    break;
                case EditorColliderHandleCommandKind::Drag:
                    drag( command );
                    break;
                case EditorColliderHandleCommandKind::End:
                    end( command._bCommit );
                    break;
            }
        }
    }

    void EditorColliderHandleEdit::capture( GameObjectHandle object )
    {
        GameObject* pObject = editor::findGameObject( object );
        _object             = object;
        _pBefore            = pObject != nullptr ? make_unique<ObjectSnapshot>( EditorTransaction::captureSnapshot( pObject ) ) : unique_ptr<ObjectSnapshot>{};
        _bChanged           = false;
    }

    void EditorColliderHandleEdit::drag( const EditorColliderHandleCommand& command )
    {
        GameObject*         pObject = editor::findGameObject( command._object );
        RigidBodyComponent* pBody   = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
        if ( pBody == nullptr )
            return;
        float3     position{};
        quaternion rotation{};
        float3     scale{};
        if ( PhysicsComponent::readWorldPose( *pBody, position, rotation, scale ) == false )
            return;
        const ColliderHandle&             handle    = command._handle;
        const vector<PhysicsShapeDesc3D>& listShape = pBody->getShapes();
        if ( handle._shapeIndex >= listShape.size() )
            return;
        // 축은 중심에서 손잡이 쪽을 향하므로 레이가 가리키는 점의 거리가 곧 새 가장자리까지의 거리다.
        const float32             distance = EditorColliderHandleMath::computeClosestAxisParameter( command._ray._origin, command._ray._direction, handle._center, handle._direction );
        const PhysicsShapeDesc3D& current  = listShape[handle._shapeIndex];
        const PhysicsShapeDesc3D  resized  = EditorColliderHandleMath::applyDrag( current, handle, distance, scale );
        if ( EditorColliderHandleEditInternal::isSameSize( resized, current ) )
            return;
        vector<PhysicsShapeDesc3D> listResized = listShape;
        listResized[handle._shapeIndex]        = resized;
        pBody->setShapes( listResized );
        _bChanged = true;
    }

    void EditorColliderHandleEdit::end( bool bCommit )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        const bool        bCommitDrag = bCommit && _bChanged && _pBefore != nullptr && _pBefore->_xml.empty() == false && EditorPlaySession::areSceneEditsAllowed();
        if ( bCommitDrag )
        {
            GameObject* pObject = editor::findGameObject( _object );
            if ( pObject != nullptr )
            {
                EditorSceneCommands::commitModify( transaction, pObject, *_pBefore, "Resize Collider" );
                ++_commitCount;
            }
        }
        _object = GameObjectHandle{};
        _pBefore.reset();
        _bChanged = false;
    }

    EditorColliderHandleSession& EditorColliderHandleSession::get()
    {
        return getEditorState<EditorColliderHandleSession>();
    }

    SW_AUTOMATION_WATCH( editorColliderHalfExtentX, "Editor.ColliderHalfExtentX", "Half size X of the first shape of the selected RigidBody (0 when none)",
                         &EditorColliderHandleEditInternal::readHalfExtentX );
    SW_AUTOMATION_WATCH( editorColliderHandleCommitCount, "Editor.ColliderHandleCommitCount", "Collider handle drags that finished and left an undo entry",
                         &EditorColliderHandleEditInternal::readCommitCount );
    SW_AUTOMATION_WATCH( editorColliderHandleCount, "Editor.ColliderHandleCount", "Collider handles drawn in the last frame",
                         &EditorColliderHandleEditInternal::readDrawnHandleCount );
} // namespace sw::editor
