#include "pch.h"

#include "Editor/Viewport/EditorViewportGizmo.h"

#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportGizmoInternal
        {
            /// 가운데 마크의 반폭(글자 크기 비율) — ImGuizmo 의 가운데 사각형(화면 평면 이동)보다 작게, 누른 자리가 축 손잡이에 닿지 않는다
            static constexpr float32 kCenterMarkHalfFontRatio = 0.25f;

            /** @brief 이번 프레임의 기즈모 조작(이동 · 회전 · 크기), 좌표계, 스냅 값입니다. 모은 값 · 툴바 설정에서 한 번 만듭니다. */
            struct GizmoSetup
            {
                ImGuizmo::OPERATION _operation{ ImGuizmo::TRANSLATE };
                ImGuizmo::MODE      _mode{ ImGuizmo::WORLD };
                float32             _arrSnap[3]{ 0.0f, 0.0f, 0.0f };

                bool hasSnap() const { return _arrSnap[0] > 0.0f; }
            };

            /** @brief 조작 번호(0 이동 · 1 회전 · 2 크기)와 좌표계, 툴바의 스냅 설정으로 설정을 만듭니다. */
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

            /** @brief 대상들의 무게중심입니다. */
            static float3 computeCentroid( const vector<EditorGizmoTarget>& listTarget )
            {
                float3 center{};
                for ( const EditorGizmoTarget& target : listTarget )
                {
                    center = center + target._world.getTranslation();
                }
                const float32 invCount = 1.0f / static_cast<float32>( listTarget.size() );
                return float3{ center._x * invCount, center._y * invCount, center._z * invCount };
            }

            static bool wasGizmoPressed() { return ImGuizmo::IsOver() && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ); }
        };
    } // namespace

    EditorViewportGizmo::EditorViewportGizmo()
        : _listCommand{}
        , _listDragTarget{}
        , _arrGroupMatrix{}
        , _lastDrawFrame{ -1 }
        , _lastObjectCount{ 0 }
        , _bTracking{ SW_FALSE }
        , _bHovered{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorViewportGizmo::isMouseOverDrawnGizmo() const
    {
        const bool bDrawnLastFrame = _lastDrawFrame + 1 >= ImGui::GetFrameCount();
        return isDragging() || ( bDrawnLastFrame && ( ImGuizmo::IsOver() || ImGuizmo::IsUsing() ) );
    }

    void EditorViewportGizmo::draw( const EditorGizmoViewState& state, const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize,
                                    const ViewportToolbarSettings& settings, const utf8* pCenterMark )
    {
        _listCommand.clear();
        _bHovered = SW_FALSE;
        if ( state._bEditsAllowed == false || state._listTarget.empty() )
        {
            endDrag();
            return;
        }

        _lastDrawFrame   = ImGui::GetFrameCount();
        _lastObjectCount = static_cast<uint32>( state._listTarget.size() );
        if ( pCenterMark != nullptr && EditorSelfTestMarks::isEnabled() )
            recordCenterMark( pCenterMark, state._listTarget, pView, pProj, canvasPos, canvasSize );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( canvasPos._x, canvasPos._y, canvasSize._x, canvasSize._y );
        // beginFrame 이 프레임마다 끈다. 캔버스가 이번 프레임에 그려졌고 편집이 허용될 때(위에서 걸렀다)만 켠다. 꺼 둔 상태이면 그리기만 하고 조작을 받지 않는다.
        ImGuizmo::Enable( true );

        const EditorViewportGizmoInternal::GizmoSetup setup = EditorViewportGizmoInternal::makeSetup( state._operation, state._bLocalSpace, settings );
        const float32*                                pSnap = setup.hasSnap() ? setup._arrSnap : nullptr;
        const uint32                                  op    = static_cast<uint32>( setup._operation );
        const uint32                                  mode  = static_cast<uint32>( setup._mode );
        if ( state._listTarget.size() > 1 )
        {
            manipulateGroup( state, pView, pProj, op, mode, pSnap );
            return;
        }
        // 그룹 드래그였다면 대상 하나로 바뀌는 순간 정리한다 — 그 스냅샷을 이 대상에 커밋하지 않는다(Model 의 Capture 가 앞 드래그를 끝낸다).
        if ( _listDragTarget.empty() == false )
            endDrag();
        // 표면 붙이기는 이동에서만, 미리 보기 범위가 아닐 때만 한다.
        const bool bSurfaceSnap = setup._operation == ImGuizmo::TRANSLATE && settings._bSurfaceSnap && state._bPreviewScope == false;
        manipulateSingle( state._listTarget[0], pView, pProj, op, mode, pSnap, bSurfaceSnap );
    }

    void EditorViewportGizmo::manipulateSingle( const EditorGizmoTarget& target, const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                                const float32* pSnap, bool bSurfaceSnap )
    {
        float32 arrMatrix[16];
        EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, target._world );
        if ( _bTracking == SW_FALSE && EditorViewportGizmoInternal::wasGizmoPressed() )
            _listCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Capture, { target }, false } );

        vector<EditorGizmoTarget> listWorld;
        if ( ImGuizmo::Manipulate( pView, pProj, static_cast<ImGuizmo::OPERATION>( operation ), static_cast<ImGuizmo::MODE>( gizmoMode ), arrMatrix, nullptr, pSnap ) )
        {
            EditorGizmoTarget moved{ target._object, float4x4{} };
            EditorViewportProjectionUtil::loadGizmoMatrix( moved._world, arrMatrix );
            listWorld.push_back( moved );
        }
        _bHovered = ImGuizmo::IsOver() ? SW_TRUE : SW_FALSE;
        trackUse( std::move( listWorld ), bSurfaceSnap );
    }

    void EditorViewportGizmo::manipulateGroup( const EditorGizmoViewState& state, const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                               const float32* pSnap )
    {
        if ( _bTracking == SW_FALSE )
        {
            float4x4 groupWorld{};
            groupWorld.setTranslation( EditorViewportGizmoInternal::computeCentroid( state._listTarget ) );
            EditorViewportProjectionUtil::storeGizmoMatrix( _arrGroupMatrix, groupWorld );
            if ( EditorViewportGizmoInternal::wasGizmoPressed() )
            {
                // 누른 순간의 그룹 기준 상대 행렬 — 끄는 동안 대상의 월드 행렬은 이것 × 그룹 행렬이다.
                _listDragTarget.clear();
                const float4x4 invGroup = groupWorld.invert();
                for ( const EditorGizmoTarget& target : state._listTarget )
                {
                    _listDragTarget.push_back( EditorGizmoTarget{ target._object, target._world * invGroup } );
                }
                _listCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Capture, state._listTarget, false } );
            }
        }

        vector<EditorGizmoTarget> listWorld;
        if ( ImGuizmo::Manipulate( pView, pProj, static_cast<ImGuizmo::OPERATION>( operation ), static_cast<ImGuizmo::MODE>( gizmoMode ), _arrGroupMatrix, nullptr, pSnap ) )
        {
            float4x4 groupWorld{};
            EditorViewportProjectionUtil::loadGizmoMatrix( groupWorld, _arrGroupMatrix );
            for ( const EditorGizmoTarget& relative : _listDragTarget )
            {
                listWorld.push_back( EditorGizmoTarget{ relative._object, relative._world * groupWorld } );
            }
        }
        _bHovered = ImGuizmo::IsOver() ? SW_TRUE : SW_FALSE;
        trackUse( std::move( listWorld ), false );
    }

    void EditorViewportGizmo::trackUse( vector<EditorGizmoTarget>&& listWorld, bool bSurfaceSnap )
    {
        if ( ImGuizmo::IsUsing() )
        {
            // 클릭 없이 시작된 드래그(Capture 가 없다)는 Model 에 스냅샷이 없어 끝에서 기록하지 않는다.
            _bTracking = SW_TRUE;
            _listCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Drag, std::move( listWorld ), bSurfaceSnap } );
        }
        else
        {
            if ( listWorld.empty() == false )
                _listCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::Drag, std::move( listWorld ), bSurfaceSnap } );
            if ( _bTracking == SW_TRUE )
                endDrag();
        }
    }

    void EditorViewportGizmo::endDrag()
    {
        if ( _bTracking == SW_TRUE || _listDragTarget.empty() == false )
            _listCommand.push_back( EditorGizmoCommand{ EditorGizmoCommandKind::End, {}, false } );
        _listDragTarget.clear();
        _bTracking = SW_FALSE;
    }

    void EditorViewportGizmo::recordCenterMark( const utf8* pMark, const vector<EditorGizmoTarget>& listTarget, const float32* pView, const float32* pProj, const float2& canvasPos,
                                                const float2& canvasSize )
    {
        const float3 center = EditorViewportGizmoInternal::computeCentroid( listTarget );
        float4x4     viewProjection{};
        EditorViewportProjectionUtil::loadViewProjection( pView, pProj, viewProjection );
        ImVec2 screen{};
        if ( EditorViewportProjectionUtil::projectPoint( viewProjection, center, canvasPos, canvasSize, screen ) == false )
            return;
        const float32 half = ImGui::GetFontSize() * EditorViewportGizmoInternal::kCenterMarkHalfFontRatio;
        EditorSelfTestMarks::recordRect( pMark, float2{ screen.x - half, screen.y - half }, float2{ screen.x + half, screen.y + half } );
    }
} // namespace sw::editor
