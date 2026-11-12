#include "pch.h"

#include "Editor/Viewport/EditorViewportGizmo.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    /** @brief 기즈모 드래그 하나의 시작 전 스냅샷입니다(단일은 `_before`, 그룹은 `_listBefore` — `_listObject` 와 같은 순서). */
    struct EditorViewportGizmoUndo
    {
        ObjectSnapshot         _before{};
        vector<ObjectSnapshot> _listBefore{};
    };

    namespace
    {
        struct EditorViewportGizmoInternal
        {
            /// 가운데 마크의 반폭(글자 크기 비율) — ImGuizmo 의 가운데 사각형(화면 평면 이동)보다 작게, 누른 자리가 축 손잡이에 닿지 않는다
            static constexpr float32 kCenterMarkHalfFontRatio = 0.25f;

            /** @brief 이번 프레임의 기즈모 조작(이동 · 회전 · 크기), 좌표계, 스냅 값입니다. 워크스페이스 · 툴바 설정에서 한 번 만듭니다. */
            struct GizmoSetup
            {
                ImGuizmo::OPERATION _operation{ ImGuizmo::TRANSLATE };
                ImGuizmo::MODE      _mode{ ImGuizmo::WORLD };
                float32             _arrSnap[3]{ 0.0f, 0.0f, 0.0f };

                bool hasSnap() const { return _arrSnap[0] > 0.0f; }
            };

            /** @brief 워크스페이스의 조작 번호(0 이동 · 1 회전 · 2 크기)와 좌표계, 툴바의 스냅 설정으로 설정을 만듭니다. */
            static GizmoSetup makeSetup( const int32 operationIndex, const bool bLocalSpace, const ViewportToolbarSettings& settings )
            {
                GizmoSetup setup;
                setup._mode       = bLocalSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
                float32 snapValue = 0.0f;
                if ( operationIndex == 1 )
                {
                    setup._operation = ImGuizmo::ROTATE;
                    snapValue        = settings._bRotationSnap ? settings._rotationSnapValue : 0.0f;
                }
                else if ( operationIndex == 2 )
                {
                    setup._operation = ImGuizmo::SCALE;
                    snapValue        = settings._bScaleSnap ? settings._scaleSnapValue : 0.0f;
                }
                else
                {
                    snapValue = settings._bGridSnap ? settings._gridSnapValue : 0.0f;
                }
                setup._arrSnap[0] = setup._arrSnap[1] = setup._arrSnap[2] = snapValue;
                return setup;
            }
        };
    } // namespace
    EditorViewportGizmo::EditorViewportGizmo()
        : _pUndo{ make_unique<EditorViewportGizmoUndo>() }
        , _object{}
        , _listObject{}
        , _listRelativeWorld{}
        , _pTarget{ nullptr }
        , _arrGroupMatrix{}
        , _lastDrawFrame{ -1 }
        , _lastObjectCount{ 0 }
        , _bTracking{ SW_FALSE }
        , _bHovered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorViewportGizmo::~EditorViewportGizmo() = default;

    bool EditorViewportGizmo::isMouseOverDrawnGizmo() const
    {
        const bool bDrawnLastFrame = _lastDrawFrame + 1 >= ImGui::GetFrameCount();
        return isDragging() || ( bDrawnLastFrame && ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() ) );
    }

    void EditorViewportGizmo::draw( EditorSelection& selection, const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize,
                                    const ViewportToolbarSettings& settings, const utf8* pCenterMark )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
        {
            endDrag();
            return;
        }

        // 다른 선택으로 넘어왔으면 앞 드래그를 그 선택의 범위에 정리한다.
        if ( _pTarget != &selection )
        {
            endDrag();
            _pTarget = &selection;
        }
        _bHovered = SW_FALSE;
        // 최상위 집합만 옮긴다. 자식은 부모를 따라 움직이므로, 자식을 따로 놓으면 선택 순서에 따라 부모가 한 번 더 끈다(유니티 `SelectionMode.TopLevel` · 언리얼과 같다).
        vector<GameObject*> listSelected;
        selection.getTopLevelObjects( listSelected );
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
        if ( pCenterMark != nullptr && EditorSelfTestMarks::isEnabled() )
            recordCenterMark( pCenterMark, listGizmo, pView, pProj, canvasPos, canvasSize );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );
        // beginFrame 이 프레임마다 끈다. 캔버스가 이번 프레임에 그려졌고 편집이 허용될 때(위에서 걸렀다)만 켠다. 꺼 둔 상태이면 그리기만 하고 조작을 받지 않는다.
        ImGuizmo::Enable( true );

        const EditorWorkspace&                        ws    = editorContext.getSubsystem<EditorWorkspace>();
        const EditorViewportGizmoInternal::GizmoSetup setup = EditorViewportGizmoInternal::makeSetup( ws.getGizmoOperation(), ws.isGizmoLocalSpace(), settings );
        const float32*                                pSnap = setup.hasSnap() ? setup._arrSnap : nullptr;
        const uint32                                  op    = static_cast<uint32>( setup._operation );
        const uint32                                  mode  = static_cast<uint32>( setup._mode );
        if ( listGizmo.size() > 1 )
        {
            manipulateGroup( pView, pProj, op, mode, listGizmo, setup.hasSnap(), pSnap );
            return;
        }
        // 표면 붙이기는 이동에서만, 미리 보기 범위가 아닐 때만 한다.
        const bool bSurfaceSnap = setup._operation == ImGuizmo::TRANSLATE && settings._bSurfaceSnap && selection.isPreviewScope() == false;
        manipulateSingle( *listGizmo[0], pView, pProj, op, mode, pSnap, bSurfaceSnap );
    }

    void EditorViewportGizmo::manipulateSingle( GameObject& object, const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                                const float32* pSnap, bool bSurfaceSnap )
    {
        SceneComponent* pSceneComp = object.getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;
        // 추적 중인 드래그가 이 오브젝트의 것이 아니면(선택이 바뀌었거나 그룹 드래그였다) 먼저 정리한다. 그 스냅샷을 이 오브젝트에 커밋하지 않는다.
        if ( _bTracking == SW_TRUE && _object != object.getHandle() )
            endDrag();

        float32 arrMatrix[16];
        EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, pSceneComp->getWorldMatrix() );

        const bool bGizmoDragStarted = _bTracking == SW_FALSE && ImGuizmo::IsOver() && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left );
        if ( bGizmoDragStarted )
        {
            _pUndo->_before = EditorTransaction::captureSnapshot( &object );
            _object         = object.getHandle();
        }

        if ( ImGuizmo::Manipulate( pView, pProj, static_cast<ImGuizmo::OPERATION>( operation ), static_cast<ImGuizmo::MODE>( gizmoMode ), arrMatrix, nullptr, pSnap ) )
        {
            float4x4 newWorldMat{};
            EditorViewportProjectionUtil::loadGizmoMatrix( newWorldMat, arrMatrix );

            // 표면 붙이기는 월드 위치와 월드 Y 스케일로 한다. 로컬로 분해한 값을 넘기면 부모가 있을 때 다른 오브젝트의 월드 윗면과
            // 로컬 높이를 비교하게 된다.
            if ( bSurfaceSnap )
            {
                float3 worldTranslation = newWorldMat.getTranslation();
                EditorSceneCommands::snapTranslationToSurface( &object, worldTranslation );
                newWorldMat.setTranslation( worldTranslation );
            }

            // 분해는 엔진이 한다. 엔진의 오일러 규칙을 따라야 한다. ImGuizmo 의 XYZ 분해는 엔진의 요, 피치, 롤 순서와 달라서 여러 축이 섞인 회전이 틀어진다.
            EditorSceneCommands::applyWorldTransform( &object, newWorldMat );
        }

        _bHovered = ImGuizmo::IsOver() ? SW_TRUE : SW_FALSE;
        if ( ImGuizmo::IsUsing() )
        {
            // 클릭 없이 시작된 드래그(스냅샷이 없다)는 대상만 기억한다. 커밋할 "이전" 값이 없으므로 끝에서 버린다.
            if ( _bTracking == SW_FALSE && _object != object.getHandle() )
            {
                _pUndo->_before = ObjectSnapshot{};
                _object         = object.getHandle();
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
            GameObject* pTracked = findTracked( _object );
            if ( pTracked != nullptr && _pUndo->_before._xml.empty() == false )
                commitDrag( *pTracked, _pUndo->_before );
            // 그룹 드래그: 살아 있는 대상마다 커밋한다.
            const uint32 count = static_cast<uint32>( _listObject.size() );
            for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
            {
                GameObject* pObj = findTracked( _listObject[objectIndex] );
                if ( pObj != nullptr )
                    commitDrag( *pObj, _pUndo->_listBefore[objectIndex] );
            }
        }
        _pUndo->_before = ObjectSnapshot{};
        _object         = GameObjectHandle{};
        _listObject.clear();
        _pUndo->_listBefore.clear();
        _listRelativeWorld.clear();
        _bTracking = SW_FALSE;
    }

    GameObject* EditorViewportGizmo::findTracked( GameObjectHandle handle ) const
    {
        return _pTarget != nullptr ? _pTarget->findObject( handle ) : nullptr;
    }

    void EditorViewportGizmo::commitDrag( GameObject& obj, const ObjectSnapshot& before ) const
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        // 메인 선택은 씬 명령(되돌리기 + 씬 dirty), 미리보기 선택은 그 창의 스택이다.
        if ( _pTarget != nullptr && _pTarget->isPreviewScope() )
            _pTarget->recordModify( obj, before, EditorTransaction::captureSnapshot( &obj ), "Gizmo Transform" );
        else
            EditorSceneCommands::commitModify( transaction, &obj, before, "Gizmo Transform" );
    }

    void EditorViewportGizmo::recordCenterMark( const utf8* pMark, const vector<GameObject*>& listGizmo, const float32* pView, const float32* pProj, const float2& canvasPos,
                                                const float2& canvasSize )
    {
        float3 center{};
        for ( const GameObject* pObj : listGizmo )
        {
            center = center + pObj->getPrimarySceneComponent()->getWorldPosition(); // 부른 쪽이 씬 컴포넌트가 있는 것만 넘긴다
        }
        const float32 invCount = 1.0f / static_cast<float32>( listGizmo.size() );
        center                 = float3{ center._x * invCount, center._y * invCount, center._z * invCount };
        float4x4 viewProjection{};
        EditorViewportProjectionUtil::loadViewProjection( pView, pProj, viewProjection );
        ImVec2 screen{};
        if ( EditorViewportProjectionUtil::projectPoint( viewProjection, center, canvasPos, canvasSize, screen ) == false )
            return;
        const float32 half = ImGui::GetFontSize() * EditorViewportGizmoInternal::kCenterMarkHalfFontRatio;
        EditorSelfTestMarks::recordRect( pMark, float2{ screen.x - half, screen.y - half }, float2{ screen.x + half, screen.y + half } );
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

            if ( ImGuizmo::IsOver() && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
            {
                _listObject.clear();
                _pUndo->_listBefore.clear();
                _listRelativeWorld.clear();
                const float4x4 invGroup = groupWorld.invert();
                for ( GameObject* pObj : listGizmo )
                {
                    _listObject.push_back( pObj->getHandle() );
                    _pUndo->_listBefore.push_back( EditorTransaction::captureSnapshot( pObj ) );
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
                GameObject* pObj = findTracked( _listObject[objectIndex] );
                if ( pObj == nullptr )
                    continue;
                SceneComponent* pSc = pObj->getPrimarySceneComponent();
                if ( pSc == nullptr )
                    continue;
                pSc->setWorldTransform( _listRelativeWorld[objectIndex] * groupWorld );
            }
        }

        _bHovered = ImGuizmo::IsOver() ? SW_TRUE : SW_FALSE;
        if ( ImGuizmo::IsUsing() )
            _bTracking = SW_TRUE;
        else if ( _bTracking == SW_TRUE )
            endDrag();
    }
} // namespace sw::editor
