/**
 * @file RigTargetGizmoVisualizer.cpp
 * @brief 리그 대상 오프셋 기즈모 시각화(MVVM View)입니다 — 값(`RigTargetEdit::gather`)으로 기즈모를 그리고 ImGuizmo 조작을 명령으로 냅니다.
 * @details 모으기 · 적용과 리그 파일 되쓰기(되돌리기 하나)는 Model `RigTargetEdit`, 명령 순서는 `RigTargetGizmoCommand::step`. 편집 상태는 리그 디버그 창과 함께 보는 `RigTargetEdit` 다.
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/RigTargetGizmoEdit.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    namespace
    {
        struct RigTargetGizmoVisualizerInternal
        {
            /** @brief ImGuizmo 가 주고받는 행렬 배열의 칸 수(4 x 4)입니다. */
            static constexpr uint32 kGizmoMatrixFloatCount = 16;
            /** @brief 시각화 목록에서의 차례입니다(리그 디버그 창에 있을 때와 같다). */
            static constexpr int32 kVisualizerOrder = 900;

            /**
             * @brief 창이 고른 자기 유닛 대상에 뷰포트 기즈모를 그리고, 끌기 · 놓기를 명령으로 내 Model 에 적용합니다(RigEditing R2).
             * @details 조작은 워크스페이스의 이동 · 회전을 따른다(크기는 이동). 마우스 양보는 모으기가 한다.
             */
            static void process( const EditorViewportVisualizerArgs& args )
            {
                const float32           aspect = args._canvasSize._x / ( args._canvasSize._y > 0.0f ? args._canvasSize._y : 1.0f );
                RigTargetGizmoViewState state;
                RigTargetEdit::get().gather( args._pActiveCamera, aspect, state );
                if ( state._bAvailable == false )
                    return;

                float32 arrView[kGizmoMatrixFloatCount];
                float32 arrProj[kGizmoMatrixFloatCount];
                float32 arrMatrix[kGizmoMatrixFloatCount];
                EditorViewportProjectionUtil::storeGizmoMatrix( arrView, state._view );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, state._projection );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, state._world );
                ImGuizmo::SetDrawlist( args._pDrawList );
                ImGuizmo::SetRect( args._canvasPos._x, args._canvasPos._y, args._canvasSize._x, args._canvasSize._y );
                ImGuizmo::Enable( true );
                const ImGuizmo::OPERATION operation = state._bRotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
                float4x4                  movedWorld{};
                const bool                bMoved = ImGuizmo::Manipulate( arrView, arrProj, operation, ImGuizmo::LOCAL, arrMatrix, nullptr, nullptr );
                if ( bMoved )
                    EditorViewportProjectionUtil::loadGizmoMatrix( movedWorld, arrMatrix );
                vector<RigTargetGizmoCommand> listCommand;
                RigTargetGizmoCommand::step( state._bDragging, bMoved ? &movedWorld : nullptr, ImGuizmo::IsUsing(), listCommand );
                if ( listCommand.empty() == false )
                    RigTargetEdit::get().apply( listCommand );
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER_TOOL( RigTargetGizmo, "rig_target_gizmo", RigTargetGizmoVisualizerInternal::kVisualizerOrder, ::sw::editor::EditorIconName{}, SW_LOCKEYREF( "EdVisualizer", "RigTargetGizmoLabel" ),
                               SW_LOCKEYREF( "EdVisualizer", "RigTargetGizmoTip" ), true,
                               &RigTargetGizmoVisualizerInternal::process, nullptr );
} // namespace sw::editor
