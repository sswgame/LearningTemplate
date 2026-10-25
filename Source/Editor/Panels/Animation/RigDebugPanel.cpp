#include "pch.h"

#include "Editor/Panels/Animation/RigDebugPanel.h"

#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RigDebugPanel, RigDebugPanel::kPanelID, EditorPanelCategory::Tool, 1660 );

    namespace
    {
        struct RigDebugPanelInternal
        {
            static constexpr float4  kMarkerColor{ 1.0f, 0.45f, 0.2f, 1.0f };
            static constexpr float32 kMarkerRadius = 0.03f;
            /** @brief 디버그 드로우 카테고리입니다(`debugdraw.category RigDebug off` 로 끈다). */
            static constexpr const utf8* kDrawCategory = "RigDebug";

            /** @brief 오브젝트 id 의 리그 컴포넌트입니다(없으면 nullptr). */
            static PoseModifierComponent* findModifier( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = ( pManager != nullptr && objectID != 0 ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<PoseModifierComponent>() : nullptr;
            }

            /** @brief 오브젝트 id 와 노드 번호의 노드입니다(되돌리기 때 다시 찾는다, 없으면 nullptr). */
            static RigNode* findNode( uint64 objectID, uint32 nodeIndex )
            {
                PoseModifierComponent* pModifier = findModifier( objectID );
                if ( pModifier == nullptr || nodeIndex >= pModifier->getRigInstance().getNodeCount() )
                    return nullptr;
                return &pModifier->getRigInstance().getNode( nodeIndex );
            }

            static void applyWeight( uint64 objectID, uint32 nodeIndex, float32 weight )
            {
                if ( RigNode* pNode = findNode( objectID, nodeIndex ) )
                    pNode->setWeight( weight );
            }

            static void applyEnabled( uint64 objectID, uint32 nodeIndex, bool bEnabled )
            {
                if ( RigNode* pNode = findNode( objectID, nodeIndex ) )
                    pNode->setEnabled( bEnabled );
            }

            static void recordWeight( uint64 objectID, uint32 nodeIndex, float32 before, float32 after )
            {
                EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, before]()
                { applyWeight( objectID, nodeIndex, before ); } ),
                                         SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, after]()
                { applyWeight( objectID, nodeIndex, after ); } ),
                                         "Edit Rig Node Weight" );
            }

            static void recordEnabled( uint64 objectID, uint32 nodeIndex, bool bAfter )
            {
                const bool bBefore = bAfter == false;
                EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, bBefore]()
                { applyEnabled( objectID, nodeIndex, bBefore ); } ),
                                         SW_DELEGATE_LAMBDA( Delegate<void()>, [objectID, nodeIndex, bAfter]()
                { applyEnabled( objectID, nodeIndex, bAfter ); } ),
                                         "Toggle Rig Node" );
            }

            /**
             * @brief 대상 오프셋 기즈모의 상태입니다. 창이 대상을 고르고(`_objectID` · `_targetIndex`), 뷰포트 시각화(`drawTargetGizmo`)가 그 대상에 기즈모를 그린다.
             * @details 창과 시각화가 한 파일에 있어 에디터 범위의 상태 저장소가 소유한 값 하나를 함께 본다(에디터는 한 스레드). 끄는 동안의 값은 `_pending` 이고, 놓으면 리그 파일에 되써 넣는다 —
             *          파일 감시가 다시 읽어 인스턴스에 들어올 때까지 기즈모는 `_pending` 에 남는다.
             */
            struct TargetEdit
            {
                BoneTransform _pending{};
                string        _status{};
                uint64        _objectID{ 0 };
                int32         _targetIndex{ -1 };
                uint8         _bPending{ SW_FALSE };
                uint8         _bDragging{ SW_FALSE };
            };

            static TargetEdit& getTargetEdit() { return getEditorState<TargetEdit>(); }

            /** @brief 디스크의 글을 쓰는 되돌리기 절차 하나입니다(되돌리기 · 다시 하기가 그 글을 다시 쓴다 — 파일 감시가 리그를 다시 읽는다). */
            static void writeRigText( const string& writePath, const string& text )
            {
                if ( FileUtil::writeTextFile( writePath, text ) == false )
                    SW_LOG_ERROR( "Rig file '%#' could not be written", writePath.c_str() );
            }

            /**
             * @brief 리그 파일에 @p params 를 되써 넣고 상태 줄 글을 돌려줍니다. 다른 키는 그대로 둡니다(`RigAsset::rewriteSettings`).
             * @details 파일 감시가 쓴 파일을 다시 읽으면 같은 리그를 쓰는 모든 인스턴스가 새 값으로 다시 바인딩된다. @p pUndoLabel 이 있으면 파일 글 전후를
             *          되돌리기로 기록한다(대상 오프셋 — 인스턴스 값이 아니라 파일이 바뀐다).
             */
            static string saveRig( const PoseModifierComponent& modifier, const RigSaveParams& params, const utf8* pUndoLabel )
            {
                const string_view rigPath  = modifier.getRigPath();
                const string      readPath = ResourcePaths::findLooseFile( rigPath ).getDiskPath();
                string            text;
                if ( readPath.empty() || FileUtil::readTextFile( readPath, text ) == false )
                    return string( "The rig file could not be read" );
                string rewritten;
                if ( RigAsset::rewriteSettings( text, rigPath, params, rewritten ) == false )
                    return string( "The rig was not saved - see the log" );
                const string writePath = ResourcePaths::getWritePath( rigPath );
                if ( writePath.empty() || FileUtil::writeTextFile( writePath, rewritten ) == false )
                    return string( "The rig file could not be written" );
                if ( pUndoLabel != nullptr )
                {
                    EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [writePath, text]()
                    { writeRigText( writePath, text ); } ),
                                             SW_DELEGATE_LAMBDA( Delegate<void()>, [writePath, rewritten]()
                    { writeRigText( writePath, rewritten ); } ),
                                             pUndoLabel );
                }
                SW_LOG_INFO( "Rig settings saved to %#", writePath.c_str() );
                return string( "Saved" );
            }

            /** @brief 인스턴스 노드의 가중치 · 켜짐을 리그 파일에 되써 넣습니다("Save to rig"). */
            static string saveNodeSettings( const PoseModifierComponent& modifier )
            {
                const RigInstance& instance = modifier.getRigInstance();
                RigSaveParams      params;
                for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
                {
                    const RigNode& node = instance.getNode( nodeIndex );
                    params._listNode.push_back( RigNodeSaveParams{ node.getName(), node.getWeight(), node.isEnabled() } );
                }
                return saveRig( modifier, params, nullptr );
            }

            /**
             * @brief 자기 유닛 대상의 부모 공간(데이터 오프셋을 곱하기 전, 소켓 로컬 × 본 × 유닛)의 월드 행렬입니다. 바깥 대상이면 false 입니다.
             * @details 대상 월드 = 데이터 오프셋 × 소켓 로컬 × 본 모델 × 유닛 월드(행 벡터). 인스턴스는 앞의 둘을 합친 값만 가지므로 데이터 오프셋의 역을 앞에 곱해 되짚는다.
             */
            static bool computeTargetParentWorld( const PoseModifierComponent& modifier, const SkeletalMeshComponent& unit, uint32 targetIndex, float4x4& outParentWorld )
            {
                const RigInstance& instance = modifier.getRigInstance();
                int32              bone     = -1;
                BoneTransform      combined{};
                if ( instance.tryGetOwnTargetBinding( targetIndex, bone, combined ) == false )
                    return false;
                const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
                const bool              bOnBone   = 0 <= bone && static_cast<size_t>( bone ) < listModel.size();
                const float4x4          boneWorld = bOnBone ? listModel[static_cast<size_t>( bone )] * unit.getWorldMatrix() : unit.getWorldMatrix();
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
                TargetEdit&            edit      = getTargetEdit();
                PoseModifierComponent* pModifier = edit._targetIndex >= 0 ? findModifier( edit._objectID ) : nullptr;
                GameObject*            pObject   = pModifier != nullptr ? pModifier->getOwner() : nullptr;
                SkeletalMeshComponent* pUnit     = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
                const bool             bReady    = pUnit != nullptr && args._pActiveCamera != nullptr && pModifier->getRigInstance().isInitialized() &&
                                    static_cast<uint32>( edit._targetIndex ) < pModifier->getRigInstance().getTargetCount();
                float4x4 parentWorld{};
                if ( bReady == false || computeTargetParentWorld( *pModifier, *pUnit, static_cast<uint32>( edit._targetIndex ), parentWorld ) == false )
                {
                    EditorViewportMouseClaim::release( &edit );
                    return;
                }
                const RigTargetDef& def = pModifier->getRigInstance().getTargetDef( static_cast<uint32>( edit._targetIndex ) );
                // 놓은 뒤 파일이 다시 읽혀 인스턴스가 같은 값이 되면 남겨 둔 값을 버린다.
                const bool bReloaded = edit._bDragging == SW_FALSE && edit._bPending == SW_TRUE &&
                                       float3::getDistance( edit._pending._translation, def._offset._translation ) < 1.0e-4f &&
                                       MathUtil::abs( edit._pending._rotation.dot( def._offset._rotation ) ) > 1.0f - 1.0e-5f;
                if ( bReloaded )
                    edit._bPending = SW_FALSE;
                const BoneTransform offset = edit._bPending == SW_TRUE ? edit._pending : def._offset;

                EditorViewportMouseClaim::acquire( &edit );
                const float32 aspect = args._canvasSize._x / ( args._canvasSize._y > 0.0f ? args._canvasSize._y : 1.0f );
                float32       arrView[16];
                float32       arrProj[16];
                float32       arrMatrix[16];
                EditorViewportProjectionUtil::storeGizmoMatrix( arrView, args._pActiveCamera->getViewMatrix() );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrProj, args._pActiveCamera->getProjectionMatrix( aspect ) );
                EditorViewportProjectionUtil::storeGizmoMatrix( arrMatrix, offset.toMatrix() * parentWorld );
                ImGuizmo::SetDrawlist( args._pDrawList );
                ImGuizmo::SetRect( args._canvasPos._x, args._canvasPos._y, args._canvasSize._x, args._canvasSize._y );
                ImGuizmo::Enable( true );
                EditorContext*            pContext  = EditorContext::get();
                const bool                bRotate   = pContext != nullptr && pContext->getSubsystem<EditorWorkspace>().getGizmoOperation() == 1;
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
                    edit._status = saveRig( *pModifier, params, "Move Rig Target" );
                    // 저장하지 못했으면 파일이 다시 읽히지 않는다 — 남겨 둔 값을 버려 기즈모가 파일 값으로 돌아오게 한다.
                    if ( edit._status != "Saved" )
                        edit._bPending = SW_FALSE;
                }
            }

            /** @brief 노드가 쓰는 본 이름을 "첫 본 (+n)" 꼴로 줄입니다. */
            static string describeBones( const Skeleton& skeleton, const vector<uint32>& listBone )
            {
                if ( listBone.empty() )
                    return string{ "-" };
                string text = listBone[0] < skeleton.getBoneCount() ? string{ skeleton.getBone( listBone[0] )._name.c_str() } : string{ "?" };
                if ( listBone.size() > 1 )
                    text += " (+" + to_string( listBone.size() - 1 ) + ")";
                return text;
            }

            /** @brief 고른 노드가 쓰는 본마다 구와 이름을 그립니다(이번 프레임만). */
            static void drawBoneMarkers( DebugDrawQueue& queue, const SkeletalMeshComponent& unit, const vector<uint32>& listBone )
            {
                const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
                const Skeleton&         skeleton  = unit.getSkeleton();
                const float4x4          world     = unit.getWorldMatrix();
                const hashed_string     category( kDrawCategory );
                for ( const uint32 boneIndex : listBone )
                {
                    if ( boneIndex >= listModel.size() || boneIndex >= skeleton.getBoneCount() )
                        continue;
                    const float3 position = float3::transform( listModel[boneIndex].getTranslation(), world );
                    queue.drawSphere( position, kMarkerRadius, kMarkerColor, 0.0f, category );
                    queue.drawText( position, skeleton.getBone( boneIndex )._name.c_str(), kMarkerColor, 0.0f, category );
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( RigTargetGizmo, "rig_target_gizmo", 900, nullptr, "Rig Target",
                          "Gizmo on the rig target picked in the Rig Debug window; releasing a drag writes the target offset into the rig asset", true,
                          &RigDebugPanelInternal::drawTargetGizmo );

    RigDebugPanel::RigDebugPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _status{}
        , _selectedObjectID{ 0 }
        , _selectedNode{ -1 }
        , _weightBeforeEdit{ 0.0f }
        , _bDrawMarkers{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void RigDebugPanel::drawNodeTable( uint64 objectID, RigInstance& instance, const SkeletalMeshComponent* pUnit, int32 tableFlags, vector<uint32>& outListSelectedBone )
    {
        if ( ImGui::BeginTable( "##rig_nodes", 6, tableFlags ) == false )
            return;
        ImGui::TableSetupColumn( "Node" );
        ImGui::TableSetupColumn( "Type" );
        ImGui::TableSetupColumn( "Bones" );
        ImGui::TableSetupColumn( "Weight" );
        ImGui::TableSetupColumn( "Effective" );
        ImGui::TableSetupColumn( "On" );
        ImGui::TableHeadersRow();
        vector<uint32> listBone;
        const uint32   count = instance.getNodeCount();
        for ( uint32 nodeIndex = 0; nodeIndex < count; ++nodeIndex )
        {
            RigNode& node = instance.getNode( nodeIndex );
            listBone.clear();
            node.collectWrittenBones( listBone );
            ImGui::PushID( static_cast<int32>( nodeIndex ) );
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool bSelected = _selectedNode == static_cast<int32>( nodeIndex );
            if ( ImGui::Selectable( node.getName().empty() ? "(unnamed)" : node.getName().c_str(), bSelected ) )
                _selectedNode = bSelected ? -1 : static_cast<int32>( nodeIndex );
            if ( bSelected )
                outListSelectedBone = listBone;

            ImGui::TableNextColumn();
            ImGui::TextUnformatted( node.getTypeName() );

            ImGui::TableNextColumn();
            const string bones = pUnit != nullptr ? RigDebugPanelInternal::describeBones( pUnit->getSkeleton(), listBone ) : string{ "?" };
            ImGui::TextUnformatted( bones.c_str() );

            ImGui::TableNextColumn();
            float32 weight = node.getWeight();
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::SliderFloat( "##weight", &weight, 0.0f, 1.0f, "%.2f" ) )
                node.setWeight( weight );
            if ( ImGui::IsItemActivated() )
                _weightBeforeEdit = node.getWeight();
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                RigDebugPanelInternal::recordWeight( objectID, nodeIndex, _weightBeforeEdit, node.getWeight() );

            ImGui::TableNextColumn();
            // 커브 · 슬롯 · LOD 를 모두 곱한 이번 프레임 값 — 시퀀서 리그 슬롯 트랙이 움직이는 것이 여기 보인다.
            const float32 effective = instance.isNodeLODOff( nodeIndex ) ? 0.0f : instance.computeNodeWeight( nodeIndex );
            ImGui::Text( "%.2f", static_cast<float64>( effective ) );
            if ( node.getWeightSlot().empty() == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "[%s]", node.getWeightSlot().c_str() );
            }

            ImGui::TableNextColumn();
            bool bEnabled = node.isEnabled();
            if ( ImGui::Checkbox( "##on", &bEnabled ) )
            {
                node.setEnabled( bEnabled );
                RigDebugPanelInternal::recordEnabled( objectID, nodeIndex, bEnabled );
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void RigDebugPanel::drawTargetTable( uint64 objectID, const PoseModifierComponent& modifier, RigInstance& instance, int32 tableFlags )
    {
        RigDebugPanelInternal::TargetEdit& targetEdit = RigDebugPanelInternal::getTargetEdit();
        // 대상 — 자기 유닛 대상(본 · 소켓)을 고르면 뷰포트에 기즈모가 뜬다(시각화 "Rig Target"). 끌기를 놓으면 그 대상의 오프셋을 리그 파일에 되써 넣는다.
        // 바깥 대상(다른 유닛 · 오브젝트)은 부모 공간이 호스트 쪽에 있어 여기서 옮기지 않는다.
        const uint32 targetCount = instance.getTargetCount();
        if ( targetCount > 0 && ImGui::BeginTable( "##rig_targets", 3, tableFlags ) )
        {
            ImGui::TableSetupColumn( "Target" );
            ImGui::TableSetupColumn( "Translation" );
            ImGui::TableSetupColumn( "Gizmo" );
            ImGui::TableHeadersRow();
            for ( uint32 targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                const RigTargetDef&  def  = instance.getTargetDef( targetIndex );
                int32                bone = -1;
                BoneTransform        combined{};
                const bool           bOwnUnit     = instance.tryGetOwnTargetBinding( targetIndex, bone, combined );
                const bool           bEditingThis = targetEdit._objectID == objectID && targetEdit._targetIndex == static_cast<int32>( targetIndex );
                const BoneTransform& shownOffset  = bEditingThis && targetEdit._bPending == SW_TRUE ? targetEdit._pending : def._offset;
                ImGui::PushID( static_cast<int32>( targetIndex ) );
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( def._name.c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f, %.3f, %.3f", static_cast<float64>( shownOffset._translation._x ), static_cast<float64>( shownOffset._translation._y ),
                             static_cast<float64>( shownOffset._translation._z ) );
                ImGui::TableNextColumn();
                ImGui::BeginDisabled( bOwnUnit == false || modifier.getRigPath().empty() );
                bool bEdit = bEditingThis;
                if ( ImGui::Checkbox( "##gizmo", &bEdit ) )
                {
                    targetEdit              = RigDebugPanelInternal::TargetEdit{};
                    targetEdit._objectID    = bEdit ? objectID : 0;
                    targetEdit._targetIndex = bEdit ? static_cast<int32>( targetIndex ) : -1;
                }
                ImGui::EndDisabled();
                if ( bOwnUnit == false )
                    EditorWidgets::drawTooltip( "Targets on other units or objects are placed by the host - move that object instead" );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        EditorSelfTestMarks::note( "rigdebug.targets" );
        if ( targetEdit._status.empty() == false )
            ImGui::TextDisabled( "Target: %s", targetEdit._status.c_str() );
    }

    void RigDebugPanel::drawContent()
    {
        EditorContext*                     pContext   = EditorContext::get();
        const uint64                       objectID   = pContext != nullptr ? pContext->getSubsystem<EditorWorkspace>().getSelectedObjectID() : 0;
        RigDebugPanelInternal::TargetEdit& targetEdit = RigDebugPanelInternal::getTargetEdit();
        if ( objectID != _selectedObjectID )
        {
            _selectedObjectID = objectID;
            _selectedNode     = -1;
            targetEdit        = RigDebugPanelInternal::TargetEdit{};
        }
        PoseModifierComponent* pModifier = RigDebugPanelInternal::findModifier( objectID );
        if ( pModifier == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "SelectRig" ) );
            return;
        }
        RigInstance& instance = pModifier->getRigInstance();
        if ( instance.isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRigDebug", "RigNotReady" ) );
            return;
        }

        bool bDrawMarkers = _bDrawMarkers == SW_TRUE;
        if ( ImGui::Checkbox( "Bone Markers", &bDrawMarkers ) )
            _bDrawMarkers = bDrawMarkers ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRigDebug", "BoneMarkersTip" ) );
        ImGui::SameLine();
        ImGui::BeginDisabled( pModifier->getRigPath().empty() );
        if ( ImGui::Button( "Save to rig" ) )
            _status = RigDebugPanelInternal::saveNodeSettings( *pModifier );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "rigdebug.save" );
        EditorWidgets::drawTooltip( "Write the weights and on/off of this instance's nodes into the rig asset (*.rig.xml); other keys stay as they are" );

        GameObject*                  pObject = pModifier->getOwner();
        const SkeletalMeshComponent* pUnit   = pObject != nullptr ? pObject->getComponent<SkeletalMeshComponent>() : nullptr;
        vector<uint32>               listSelectedBone;
        const ImGuiTableFlags        flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        drawNodeTable( objectID, instance, pUnit, flags, listSelectedBone );
        drawTargetTable( objectID, *pModifier, instance, flags );

        EditorWidgets::drawPanelStatus( _status.c_str() );
        DebugDrawQueue* pQueue = findSubsystem<DebugDrawQueue>(); // 필수 서비스: 없으면 버그
        if ( pQueue == nullptr )
            SW_LOG_ERROR( "[RigDebug] DebugDrawQueue is missing - the bone markers are not drawn" );
        const bool bCanDrawBoneMarkers = _bDrawMarkers == SW_TRUE && pQueue != nullptr && pUnit != nullptr && listSelectedBone.empty() == false;
        if ( bCanDrawBoneMarkers )
            RigDebugPanelInternal::drawBoneMarkers( *pQueue, *pUnit, listSelectedBone );
    }
} // namespace sw::editor
