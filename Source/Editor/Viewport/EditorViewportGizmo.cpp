#include "pch.h"

#include "Editor/Viewport/EditorViewportGizmo.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    EditorViewportGizmo::EditorViewportGizmo()
        : _undoBefore{}
        , _object{}
        , _listObject{}
        , _listUndo{}
        , _listRelativeWorld{}
        , _arrGroupMatrix{}
        , _lastDrawFrame{ -1 }
        , _lastObjectCount{ 0 }
        , _bTracking{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void EditorViewportGizmo::draw( const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize, const ViewportToolbarSettings& settings )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
        {
            endDrag();
            return;
        }

        vector<GameObject*> listSelected;
        pContext->getSubsystem<EditorSelection>().getSelectedObjects( listSelected );
        vector<GameObject*> listGizmo;
        listGizmo.reserve( listSelected.size() );
        for ( GameObject* pRaw : listSelected )
        {
            if ( pRaw->getPrimarySceneComponent() == nullptr )
                continue;
            listGizmo.push_back( pRaw );
        }
        if ( listGizmo.empty() )
        {
            endDrag();
            return;
        }

        _lastDrawFrame   = ImGui::GetFrameCount();
        _lastObjectCount = static_cast<uint32>( listGizmo.size() );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );
        // beginFrame 이 프레임마다 끈다. 캔버스가 이번 프레임에 그려졌고 편집이 허용될 때(위에서 걸렀다)만 켠다. 꺼 둔 상태이면 그리기만 하고 조작을 받지 않는다.
        ImGuizmo::Enable( true );

        EditorWorkspace&    ws    = pContext->getSubsystem<EditorWorkspace>();
        const int32         opInt = ws.getGizmoOperation();
        ImGuizmo::OPERATION op    = ImGuizmo::TRANSLATE;
        if ( opInt == 1 )
            op = ImGuizmo::ROTATE;
        else if ( opInt == 2 )
            op = ImGuizmo::SCALE;

        const ImGuizmo::MODE mode = ws.isGizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        float32 arrSnap[3] = { 0.0f, 0.0f, 0.0f };
        if ( op == ImGuizmo::TRANSLATE && settings._bGridSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = settings._gridSnapValue;
        else if ( op == ImGuizmo::ROTATE && settings._bRotationSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = settings._rotationSnapValue;
        else if ( op == ImGuizmo::SCALE && settings._bScaleSnap )
            arrSnap[0] = arrSnap[1] = arrSnap[2] = settings._scaleSnapValue;
        const bool bUseSnap = ( arrSnap[0] > 0.0f );

        const bool bGroup = ( listGizmo.size() > 1 );
        if ( bGroup )
        {
            manipulateGroup( pView, pProj, static_cast<uint32>( op ), static_cast<uint32>( mode ), listGizmo, bUseSnap, bUseSnap ? arrSnap : nullptr );
            return;
        }

        GameObject*     pRaw       = listGizmo[0];
        SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;
        // 추적 중인 드래그가 이 오브젝트의 것이 아니면(선택이 바뀌었거나 그룹 드래그였다) 먼저 정리한다. 그 스냅샷을 이 오브젝트에 커밋하지 않는다.
        if ( _bTracking == SW_TRUE && _object != pRaw->getHandle() )
            endDrag();

        float32 arrMatrix[16];
        EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, pSceneComp->getWorldMatrix() );

        const bool bGizmoDragStarted = _bTracking == SW_FALSE && ImGuizmo::IsOver() && ImGui::IsMouseClicked( ImGuiMouseButton_Left );
        if ( bGizmoDragStarted )
        {
            _undoBefore = EditorTransaction::captureSnapshot( pRaw );
            _object     = pRaw->getHandle();
        }

        if ( ImGuizmo::Manipulate( pView, pProj, op, mode, arrMatrix, nullptr, bUseSnap ? arrSnap : nullptr ) )
        {
            float4x4 newWorldMat{};
            EditorViewportProjectionUtil::loadGizmoMatrix( newWorldMat, arrMatrix );

            // 표면 붙이기는 월드 위치와 월드 Y 스케일로 한다. 로컬로 분해한 값을 넘기면 부모가 있을 때 다른 오브젝트의 월드 윗면과
            // 로컬 높이를 비교하게 된다.
            if ( op == ImGuizmo::TRANSLATE && settings._bSurfaceSnap )
            {
                float3 worldTranslation = newWorldMat.getTranslation();
                EditorSceneCommands::snapTranslationToSurface( pRaw, worldTranslation );
                newWorldMat.setTranslation( worldTranslation );
            }

            // 분해는 엔진이 한다. 엔진의 오일러 규칙을 따라야 한다. ImGuizmo 의 XYZ 분해는 엔진의 요, 피치, 롤 순서와 달라서 여러 축이 섞인 회전이 틀어진다.
            EditorSceneCommands::applyWorldTransform( pRaw, newWorldMat );
        }

        if ( ImGuizmo::IsUsing() )
        {
            // 클릭 없이 시작된 드래그(스냅샷이 없다)는 대상만 기억한다. 커밋할 "이전" 값이 없으므로 끝에서 버린다.
            if ( _bTracking == SW_FALSE && _object != pRaw->getHandle() )
            {
                _undoBefore = ObjectSnapshot{};
                _object     = pRaw->getHandle();
            }
            _bTracking = SW_TRUE;
        }
        else if ( _bTracking == SW_TRUE )
        {
            endDrag();
        }
    }

    void EditorViewportGizmo::endDrag()
    {
        if ( _bTracking == SW_TRUE && EditorPlaySession::areSceneEditsAllowed() )
        {
            // 단일 드래그는 스냅샷을 찍은 그 오브젝트에만 커밋한다. 핸들로 다시 찾으며, 지워졌으면 버린다.
            GameObject* pTracked = editor::findGameObject( _object );
            if ( pTracked != nullptr && _undoBefore._xml.empty() == false )
                EditorSceneCommands::commitModify( pTracked, _undoBefore, "Gizmo Transform" );
            // 그룹 드래그: 살아 있는 대상마다 커밋한다.
            const uint32 count = static_cast<uint32>( _listObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObj = editor::findGameObject( _listObject[objectIndex] );
                if ( pObj != nullptr )
                    EditorSceneCommands::commitModify( pObj, _listUndo[objectIndex], "Gizmo Transform" );
            }
        }
        _undoBefore = ObjectSnapshot{};
        _object     = GameObjectHandle{};
        _listObject.clear();
        _listUndo.clear();
        _listRelativeWorld.clear();
        _bTracking = SW_FALSE;
    }

    void EditorViewportGizmo::manipulateGroup( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode, const vector<GameObject*>& listGizmo,
                                               bool bUseSnap, const float32* pSnap )
    {
        // 헤더가 ImGuizmo 를 알 필요는 없다. 열거형은 여기서만 변환한다.
        const ImGuizmo::OPERATION op   = static_cast<ImGuizmo::OPERATION>( operation );
        const ImGuizmo::MODE      mode = static_cast<ImGuizmo::MODE>( gizmoMode );

        if ( _bTracking == SW_FALSE )
        {
            float3 centroid{};
            for ( GameObject* pObj : listGizmo )
            {
                centroid = centroid + pObj->getPrimarySceneComponent()->getWorldPosition();
            }
            const float32 invCount = 1.0f / static_cast<float32>( listGizmo.size() );
            centroid               = float3{ centroid._x * invCount, centroid._y * invCount, centroid._z * invCount };
            float4x4 groupWorld{};
            groupWorld._41 = centroid._x;
            groupWorld._42 = centroid._y;
            groupWorld._43 = centroid._z;
            EditorViewportProjectionUtil::storeGizmoMatrix( _arrGroupMatrix, groupWorld );

            if ( ImGuizmo::IsOver() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                _listObject.clear();
                _listUndo.clear();
                _listRelativeWorld.clear();
                const float4x4 invGroup = groupWorld.invert();
                for ( GameObject* pObj : listGizmo )
                {
                    _listObject.push_back( pObj->getHandle() );
                    _listUndo.push_back( EditorTransaction::captureSnapshot( pObj ) );
                    _listRelativeWorld.push_back( pObj->getPrimarySceneComponent()->getWorldMatrix() * invGroup );
                }
            }
        }

        if ( ImGuizmo::Manipulate( pView, pProj, op, mode, _arrGroupMatrix, nullptr, bUseSnap ? pSnap : nullptr ) )
        {
            float4x4 groupWorld{};
            EditorViewportProjectionUtil::loadGizmoMatrix( groupWorld, _arrGroupMatrix );
            const uint32 count = static_cast<uint32>( _listObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObj = editor::findGameObject( _listObject[objectIndex] );
                if ( pObj == nullptr )
                    continue;
                SceneComponent* pSc = pObj->getPrimarySceneComponent();
                if ( pSc == nullptr )
                    continue;
                pSc->setWorldTransform( _listRelativeWorld[objectIndex] * groupWorld );
            }
        }

        if ( ImGuizmo::IsUsing() )
            _bTracking = SW_TRUE;
        else if ( _bTracking == SW_TRUE )
            endDrag();
    }
} // namespace sw::editor
