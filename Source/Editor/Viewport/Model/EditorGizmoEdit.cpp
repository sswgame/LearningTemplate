#include "pch.h"

#include "Editor/Viewport/Model/EditorGizmoEdit.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw::editor
{
    /** @brief 드래그 하나의 시작 전 스냅샷(`_listObject` 와 같은 순서)과 드래그 동안 연 상호작용입니다. */
    struct EditorGizmoEditUndo
    {
        vector<ObjectSnapshot>       _listBefore{};
        CommandStackInteractionScope _interaction{}; ///< `endDrag` 가 닫고, 객체가 끌기 도중 사라져도 이 핸들이 닫는다
    };

    EditorGizmoEdit::EditorGizmoEdit()
        : _pUndo{ make_unique<EditorGizmoEditUndo>() }
        , _listObject{}
        , _pTarget{ nullptr }
        , _bDragged{ false }
    {
    }

    EditorGizmoEdit::~EditorGizmoEdit() = default;

    void EditorGizmoEdit::gather( const EditorSelection& selection, EditorGizmoViewState& outState )
    {
        outState._listTarget.clear();
        const EditorWorkspace& workspace = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        outState._operation              = workspace.getGizmoOperation();
        outState._bLocalSpace            = workspace.isGizmoLocalSpace();
        outState._bEditsAllowed          = EditorPlaySession::areSceneEditsAllowed();
        outState._bPreviewScope          = selection.isPreviewScope();
        vector<GameObject*> listSelected;
        selection.getTopLevelObjects( listSelected );
        for ( GameObject* pObject : listSelected )
        {
            SceneComponent* pScene = pObject->getPrimarySceneComponent();
            if ( pScene != nullptr )
                outState._listTarget.push_back( EditorGizmoTarget{ pObject->getHandle(), pScene->getWorldMatrix() } );
        }
    }

    void EditorGizmoEdit::apply( EditorSelection& selection, const vector<EditorGizmoCommand>& listCommand )
    {
        if ( listCommand.empty() )
            return;
        if ( _pTarget != &selection )
        {
            endDrag();
            _pTarget = &selection;
        }
        for ( const EditorGizmoCommand& command : listCommand )
        {
            switch ( command._kind )
            {
                case EditorGizmoCommandKind::Capture:
                    endDrag(); // 앞 드래그의 스냅샷을 새 대상에 커밋하지 않는다
                    _pTarget = &selection;
                    for ( const EditorGizmoTarget& target : command._listTarget )
                    {
                        GameObject* pObject = selection.findObject( target._object );
                        if ( pObject == nullptr )
                            continue;
                        _listObject.push_back( target._object );
                        _pUndo->_listBefore.push_back( EditorTransaction::captureSnapshot( pObject ) );
                    }
                    break;
                case EditorGizmoCommandKind::Drag:
                    _bDragged = true;
                    openStackInteraction();
                    applyWorlds( command );
                    break;
                case EditorGizmoCommandKind::End:
                    endDrag();
                    break;
            }
        }
    }

    void EditorGizmoEdit::applyWorlds( const EditorGizmoCommand& command ) const
    {
        if ( _pTarget == nullptr || EditorPlaySession::areSceneEditsAllowed() == false )
            return;
        // 대상 하나는 엔진이 분해한다(엔진의 오일러 규칙 — ImGuizmo 의 XYZ 분해는 여러 축이 섞인 회전을 틀어지게 한다). 표면 붙이기는 월드 위치로 한다.
        if ( command._listTarget.size() == 1 )
        {
            GameObject* pObject = _pTarget->findObject( command._listTarget[0]._object );
            if ( pObject == nullptr )
                return;
            float4x4 world = command._listTarget[0]._world;
            if ( command._bSurfaceSnap )
            {
                float3 translation = world.getTranslation();
                EditorSceneCommands::snapTranslationToSurface( pObject, translation );
                world.setTranslation( translation );
            }
            EditorSceneCommands::applyWorldTransform( pObject, world );
            return;
        }
        for ( const EditorGizmoTarget& target : command._listTarget )
        {
            GameObject*     pObject = _pTarget->findObject( target._object );
            SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene != nullptr )
                pScene->setWorldTransform( target._world );
        }
    }

    void EditorGizmoEdit::openStackInteraction()
    {
        if ( _pUndo->_interaction.isOpen() || _pTarget == nullptr )
            return;
        // 기록 범위와 같은 스택이다 — 미리보기 선택은 그 창의 스택, 메인 선택은 메인 스택(스택 없는 호스트는 열지 않는다). 끄는 동안 Ctrl+Z 가 그 스택을 움직이지 못한다(감사 E-2).
        CommandStack* const pStack = _pTarget->isPreviewScope() ? _pTarget->findPreviewCommandStack() : editor::findSubsystem<CommandStack>();
        if ( pStack != nullptr )
            _pUndo->_interaction = pStack->openInteraction();
    }

    void EditorGizmoEdit::endDrag()
    {
        // 상호작용을 먼저 닫는다 — 다음 Ctrl+Z 는 이 드래그의 단계를 되돌려야 한다.
        _pUndo->_interaction.reset();
        if ( _bDragged && _pTarget != nullptr && EditorPlaySession::areSceneEditsAllowed() )
        {
            const uint32 count = static_cast<uint32>( _listObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObject = _pTarget->findObject( _listObject[objectIndex] );
                if ( pObject == nullptr )
                    continue; // 끄는 중에 지워졌다
                const ObjectSnapshot& before = _pUndo->_listBefore[objectIndex];
                EditorTransaction     transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다
                if ( _pTarget->isPreviewScope() )
                    transaction.recordModify( pObject, before, EditorTransaction::captureSnapshot( pObject ), "Gizmo Transform" ); // 거래가 오브젝트의 씬으로 창의 스택을 고른다
                else
                    EditorSceneCommands::commitModify( transaction, pObject, before, "Gizmo Transform" );
            }
        }
        _listObject.clear();
        _pUndo->_listBefore.clear();
        _bDragged = false;
    }

    void EditorGizmoDragTracker::stepSingle( const EditorGizmoTarget& target, bool bPressed, bool bUsing, const float4x4* pMovedWorld, bool bSurfaceSnap,
                                             vector<EditorGizmoCommand>& outListCommand )
    {
        const bool bOtherTarget = ( _bTracking || _bCaptured ) && _tracked != target._object;
        if ( bOtherTarget )
        {
            // 끄는 중에 대상이 바뀌었다 — 앞 드래그를 닫고, 아직 끄는 중이면 새 대상을 잡아 이 끌기도 되돌리기에 남긴다.
            const bool bStillUsing = _bTracking && bUsing;
            end( outListCommand );
            bPressed = bPressed || bStillUsing;
        }
        if ( _bTracking == false && bPressed )
        {
            outListCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Capture, { target }, false } );
            _bCaptured = true;
        }
        _tracked = target._object;
        vector<EditorGizmoTarget> listMoved;
        if ( pMovedWorld != nullptr )
            listMoved.push_back( EditorGizmoTarget{ target._object, *pMovedWorld } );
        use( bUsing, std::move( listMoved ), bSurfaceSnap, outListCommand );
    }

    void EditorGizmoDragTracker::captureGroup( const vector<EditorGizmoTarget>& listTarget, vector<EditorGizmoCommand>& outListCommand )
    {
        end( outListCommand );
        outListCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Capture, listTarget, false } );
        _bCaptured = true;
        _tracked   = GameObjectHandle{};
    }

    void EditorGizmoDragTracker::stepGroup( bool bUsing, vector<EditorGizmoTarget>&& listMovedWorld, vector<EditorGizmoCommand>& outListCommand )
    {
        if ( ( _bTracking || _bCaptured ) && _tracked.isValid() )
            end( outListCommand ); // 대상 하나의 드래그였다
        use( bUsing, std::move( listMovedWorld ), false, outListCommand );
    }

    void EditorGizmoDragTracker::use( bool bUsing, vector<EditorGizmoTarget>&& listMovedWorld, bool bSurfaceSnap, vector<EditorGizmoCommand>& outListCommand )
    {
        if ( bUsing )
        {
            // 클릭 없이 시작된 드래그(잡기가 없다)는 Model 에 스냅샷이 없어 끝에서 기록하지 않는다.
            _bTracking = true;
            outListCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Drag, std::move( listMovedWorld ), bSurfaceSnap } );
            return;
        }
        if ( listMovedWorld.empty() == false )
            outListCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Drag, std::move( listMovedWorld ), bSurfaceSnap } );
        // 놓았거나, 눌렀지만 기즈모가 잡지 않았다 — 닫는다(끌기 없는 잡기는 Model 이 기록하지 않는다).
        if ( _bTracking || _bCaptured )
            end( outListCommand );
    }

    void EditorGizmoDragTracker::end( vector<EditorGizmoCommand>& outListCommand )
    {
        if ( _bTracking || _bCaptured )
            outListCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::End, {}, false } );
        _bTracking = false;
        _bCaptured = false;
        _tracked   = GameObjectHandle{};
    }
} // namespace sw::editor
