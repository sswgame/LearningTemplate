#include "pch.h"

#include "Editor/Viewport/Visualizers/RigTargetGizmoVisualizer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Asset/RigSaveParams.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    RigTargetEdit& RigTargetEdit::get()
    {
        return getEditorState<RigTargetEdit>();
    }

    namespace
    {
        struct RigTargetGizmoVisualizerInternal
        {
            /** @brief 놓은 값이 파일에서 다시 읽힌 값과 같다고 보는 위치 차이(월드 단위)입니다. */
            static constexpr float32 kReloadDistance = 1.0e-4f;
            /** @brief 놓은 값이 파일에서 다시 읽힌 값과 같다고 보는 회전 내적의 여유입니다. */
            static constexpr float32 kReloadDotSlack = 1.0e-5f;
            /** @brief ImGuizmo 가 주고받는 행렬 배열의 칸 수(4 x 4)입니다. */
            static constexpr uint32 kGizmoMatrixFloatCount = 16;
            /** @brief 워크스페이스 기즈모 조작의 회전 값입니다(`EditorWorkspace::getGizmoOperation`). */
            static constexpr int32 kRotateOperation = 1;
            /** @brief 시각화 목록에서의 차례입니다(리그 디버그 창에 있을 때와 같다). */
            static constexpr int32 kVisualizerOrder = 900;

            /** @brief 오브젝트 id 의 리그 컴포넌트입니다(없으면 nullptr). */
            static PoseModifierComponent* findModifier( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<PoseModifierComponent>() : nullptr;
            }

            /**
             * @brief 자기 유닛 대상의 부모 공간(데이터 오프셋을 곱하기 전, 소켓 로컬 × 본 × 유닛)의 월드 행렬입니다. 바깥 대상이면 false 입니다.
             * @details 대상 월드 = 데이터 오프셋 × 소켓 로컬 × 본 모델 × 유닛 월드(행 벡터). 인스턴스는 앞의 둘을 합친 값만 가지므로 데이터 오프셋의 역을 앞에 곱해 되짚는다.
             */
            static bool computeTargetParentWorld( const PoseModifierComponent& modifier, const SkeletalMeshComponent& unit, uint32 targetIndex, float4x4& outParentWorld )
            {
                const RigInstance& instance = modifier.getRigInstance();
                optional<uint32>   bone;
                BoneTransform      combined{};
                if ( instance.tryGetOwnTargetBinding( targetIndex, bone, combined ) == false )
                    return false;
                const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
                const bool              bOnBone   = bone.has_value() && *bone < listModel.size();
                const float4x4          boneWorld = bOnBone ? listModel[*bone] * unit.getWorldMatrix() : unit.getWorldMatrix();
                const RigTargetDef&     def       = instance.getTargetDef( targetIndex );
                outParentWorld                    = def._offset.toMatrix().invert() * combined.toMatrix() * boneWorld;
                return true;
            }

            /**
             * @brief 창이 고른 자기 유닛 대상에 뷰포트 기즈모를 그리고, 끌기를 놓으면 그 대상의 `translation` · `rotation` 을 리그 파일에 되써 넣습니다(RigEditing R2).
             * @details 고른 대상이 있는 동안 뷰포트 마우스를 가져가(`EditorViewportMouseClaim`) 선택 기즈모 · 클릭 선택이 비킨다. 조작은 워크스페이스의 이동 · 회전을 따른다(크기는 이동).
             */
            static void drawTargetGizmo( const EditorViewportVisualizerArgs& args )
            {
                RigTargetEdit&         edit      = RigTargetEdit::get();
                PoseModifierComponent* pModifier = edit._targetIndex.has_value() ? findModifier( edit._objectID ) : nullptr;
                GameObject*            pObject   = pModifier != nullptr ? pModifier->getOwner() : nullptr;
                SkeletalMeshComponent* pUnit     = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
                const bool             bReady    = pUnit != nullptr && args._pActiveCamera != nullptr && pModifier->getRigInstance().isInitialized() &&
                                  *edit._targetIndex < pModifier->getRigInstance().getTargetCount();
                float4x4 parentWorld{};
                if ( bReady == false || computeTargetParentWorld( *pModifier, *pUnit, *edit._targetIndex, parentWorld ) == false )
                {
                    EditorViewportMouseClaim::release( &edit );
                    return;
                }
                const RigTargetDef& def = pModifier->getRigInstance().getTargetDef( *edit._targetIndex );
                // 놓은 뒤 파일이 다시 읽혀 인스턴스가 같은 값이 되면 남겨 둔 값을 버린다.
                const bool bReloaded = edit._bDragging == SW_FALSE && edit._bPending == SW_TRUE &&
                                       float3::getDistance( edit._pending._translation, def._offset._translation ) < kReloadDistance &&
                                       MathUtil::abs( edit._pending._rotation.dot( def._offset._rotation ) ) > 1.0f - kReloadDotSlack;
                if ( bReloaded )
                    edit._bPending = SW_FALSE;
                const BoneTransform offset = edit._bPending == SW_TRUE ? edit._pending : def._offset;

                EditorViewportMouseClaim::acquire( &edit );
                const float32 aspect = args._canvasSize._x / ( args._canvasSize._y > 0.0f ? args._canvasSize._y : 1.0f );
                float32       arrView[kGizmoMatrixFloatCount];
                float32       arrProj[kGizmoMatrixFloatCount];
                float32       arrMatrix[kGizmoMatrixFloatCount];
                EditorViewportProjectionUtil::storeGizmoMatrix( arrView, args._pActiveCamera->getViewMatrix() );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, args._pActiveCamera->getProjectionMatrix( aspect ) );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, offset.toMatrix() * parentWorld );
                ImGuizmo::SetDrawlist( args._pDrawList );
                ImGuizmo::SetRect( args._canvasPos._x, args._canvasPos._y, args._canvasSize._x, args._canvasSize._y );
                ImGuizmo::Enable( true );
                const bool                bRotate   = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getGizmoOperation() == kRotateOperation;
                const ImGuizmo::OPERATION operation = bRotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
                if ( ImGuizmo::Manipulate( arrView, arrProj, operation, ImGuizmo::LOCAL, arrMatrix, nullptr, nullptr ) )
                {
                    float4x4 newWorld{};
                    EditorViewportProjectionUtil::loadGizmoMatrix( newWorld, arrMatrix );
                    BoneTransform moved = BoneTransform::makeFromMatrix( newWorld * parentWorld.invert() );
                    moved._scale        = float3{ 1.0f, 1.0f, 1.0f };
                    edit._pending       = moved;
                    edit._bPending      = SW_TRUE;
                    edit._bDragging     = SW_TRUE;
                }
                const bool bReleased = edit._bDragging == SW_TRUE && ImGuizmo::IsUsing() == false;
                if ( bReleased )
                {
                    edit._bDragging = SW_FALSE;
                    RigSaveParams params;
                    params._listTarget.push_back( RigTargetSaveParams{ def._name, edit._pending._translation, edit._pending._rotation } );
                    edit._status = RigSaveParams::writeToRigFile( *pModifier, params, "Move Rig Target" );
                    // 저장하지 못했으면 파일이 다시 읽히지 않는다 — 남겨 둔 값을 버려 기즈모가 파일 값으로 돌아오게 한다.
                    if ( edit._status != RigSaveParams::kSavedStatus )
                        edit._bPending = SW_FALSE;
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( RigTargetGizmo, "rig_target_gizmo", RigTargetGizmoVisualizerInternal::kVisualizerOrder, ::sw::editor::EditorIconName{}, SW_LOCKEYREF( "EdVisualizer", "RigTargetGizmoLabel" ),
                          SW_LOCKEYREF( "EdVisualizer", "RigTargetGizmoTip" ), true,
                          &RigTargetGizmoVisualizerInternal::drawTargetGizmo );
} // namespace sw::editor
