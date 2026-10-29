#include "pch.h"

#include "Editor/Panels/Physics/RagdollPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct RagdollPreviewPanelInternal
        {
            /// @brief 물리 에셋 파일의 접미사입니다(이 창이 문서로 받는 `Data` 종류의 파일).
            static constexpr string_view kDocumentSuffix = ".physics.xml";
            /// @brief 몸을 중심으로 화면에 담을 높이(미터)입니다 — 사람 크기 몸 하나가 들어간다.
            static constexpr float32 kFrameCenterHeight = 1.0f;
            /// @brief 화면에 담을 반지름(미터)입니다.
            static constexpr float32 kFrameRadius = 2.5f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다. 창이 낮아도 이만큼은 그린다.
            static constexpr float32 kMinViewportHeight = 220.0f;
            /// @brief 바디 표의 높이(96 DPI 기준 픽셀)입니다.
            static constexpr float32 kTableHeight = 120.0f;
            /// @brief 충격 슬라이더의 위쪽 끝(N*s)입니다 — 사람 크기 몸이 눈에 띄게 날아가는 값이다.
            static constexpr float32 kMaxImpulse = 400.0f;
            /// @brief 물리의 고정 스텝(초)입니다 — 엔진 기본보다 촘촘해 관절 한계가 덜 뚫린다(정책 `_fixedStepOverride`).
            static constexpr float32 kFixedStepSeconds = 1.0f / 120.0f;
            /// @brief 겹침 선 색입니다.
            static constexpr float4 kSkeletonColor{ 0.75f, 0.75f, 0.75f, 1.0f };
            static constexpr float4 kJointColor{ 1.0f, 0.85f, 0.2f, 1.0f };
            static constexpr float4 kSelectedColor{ 0.2f, 1.0f, 0.9f, 1.0f };
            /// @brief 관절 표식(십자)의 한 변 절반(미터)입니다.
            static constexpr float32 kJointMarkRadius = 0.03f;

            /** @brief 열린 래그돌 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const RagdollPreviewPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const RagdollPreviewPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<RagdollPreviewPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readDocumentBodies( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RagdollPreviewPanel* pPanel = findOpenPanel();
                uint32                     count{ 0 };
                if ( pPanel == nullptr || pPanel->findDocumentBodyCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }

            [[nodiscard]] static bool readBuiltBodies( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RagdollPreviewPanel* pPanel = findOpenPanel();
                uint32                     count{ 0 };
                if ( pPanel == nullptr || pPanel->findBuiltBodyCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }

            [[nodiscard]] static bool readRagdollState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RagdollPreviewPanel* pPanel = findOpenPanel();
                uint32                     state{ 0 };
                if ( pPanel == nullptr || pPanel->findRagdollState( state ) == false )
                    return false;
                outValue = static_cast<float64>( state );
                return true;
            }

            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RagdollPreviewPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getSubmittedFrameCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "RagdollPreviewPanel" );
    SW_EDITOR_PANEL( RagdollPreviewPanel, RagdollPreviewPanel::kPanelID, EditorPanelCategory::Tool, 1340 );
    SW_AUTOMATION_WATCH( editorRagdollPreviewDocumentBodies, "Editor.RagdollPreviewDocumentBodies",
                         "Bodies of the physics asset open in the Ragdoll Preview panel (fails while the panel is closed or the asset was not read)",
                         &RagdollPreviewPanelInternal::readDocumentBodies );
    SW_AUTOMATION_WATCH( editorRagdollPreviewBuiltBodies, "Editor.RagdollPreviewBuiltBodies",
                         "Bodies the preview body's ragdoll built (fails while the panel is closed, there is no body or the ragdoll is not up yet)",
                         &RagdollPreviewPanelInternal::readBuiltBodies );
    SW_AUTOMATION_WATCH( editorRagdollPreviewState, "Editor.RagdollPreviewState", "RagdollState number of the preview body (0 Animated, 1 Ragdoll, 2 Partial, 3 BlendingBack; fails while there is no body)",
                         &RagdollPreviewPanelInternal::readRagdollState );
    SW_AUTOMATION_WATCH( editorRagdollPreviewSceneFrames, "Editor.RagdollPreviewSceneFrames", "Frames the Ragdoll Preview panel asked the renderer to draw its preview scene",
                         &RagdollPreviewPanelInternal::readPreviewSceneFrames );

    RagdollPreviewPanel::RagdollPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
    {
    }

    ScenePreviewPolicy RagdollPreviewPanel::getPreviewPolicy() const
    {
        ScenePreviewPolicy policy = EditorPreviewPanel::getPreviewPolicy();
        policy._bPhysics          = true;
        policy._bPlay             = true; // 래그돌 · 강체의 onBeginPlay 가 돌아야 바디가 선다
        policy._fixedStepOverride = RagdollPreviewPanelInternal::kFixedStepSeconds;
        return policy;
    }

    EditorPreviewEnvironment RagdollPreviewPanel::getPreviewEnvironment() const
    {
        EditorPreviewEnvironment environment;
        environment._floorHeight = 0.0f; // 보이는 바닥이 정적 바디의 위 면과 같은 높이다
        return environment;
    }

    void RagdollPreviewPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "ragdoll" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .physics.xml physics asset, pick a body prefab and knock it down here." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Reload" ) )
        {
            reloadDocument();
            _model.requestRespawn(); // 다시 읽은 에셋으로 몸을 다시 놓는다
        }
        EditorSelfTestMarks::note( "ragdoll.reload" );
        EditorWidgets::drawTooltip( "Read the physics asset file again (after saving it elsewhere) and place the body anew" );

        ImGui::TextUnformatted( "Body prefab (skeletal mesh on its root)" );
        (void)EditorWidgets::drawTextField( "##ragdollPreview.body", _model.editBodyPrefabPath(), -1.0f );
        EditorSelfTestMarks::note( "ragdoll.bodyField" );
        if ( ImGui::Button( "Spawn" ) )
            _model.spawnFromField();
        EditorSelfTestMarks::note( "ragdoll.spawn" );
        EditorWidgets::drawTooltip( "Place the body in the preview scene and hang the physics asset on it as a hitbox ragdoll" );

        if ( isDocumentLoadFailed() == false ) // 에셋을 읽지 못했으면 몸을 놓지 않는다
            _model.syncBody( getPreviewScene(), getLoadedAssetPath() );
        RagdollComponent* pRagdoll = _model.findRagdoll( getPreviewScene() );
        if ( pRagdoll != nullptr )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Ragdoll" ) );
            if ( ImGui::Button( "Ragdoll" ) )
                pRagdoll->startRagdoll( float3{}, -1 );
            EditorSelfTestMarks::note( "ragdoll.drop" );
            EditorWidgets::drawTooltip( "Turn every body dynamic - the body falls and settles by gravity" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Impulse" ) );
            if ( ImGui::Button( "Impulse" ) )
                _model.applyImpulse( *pRagdoll );
            EditorSelfTestMarks::note( "ragdoll.impulse" );
            EditorWidgets::drawTooltip( "Go ragdoll and push the picked body (the table row; the first body when none) with the impulse below" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Partial" ) );
            if ( ImGui::Button( "Partial" ) )
                pRagdoll->startPartialRagdoll( 1.0f );
            EditorSelfTestMarks::note( "ragdoll.partial" );
            EditorWidgets::drawTooltip( "Only the bodies under the ragdoll's Partial Root Bone go limp (the prefab's RagdollComponent names that bone)" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Release" ) );
            if ( ImGui::Button( "Release" ) )
                pRagdoll->stopPartialRagdoll();
            EditorSelfTestMarks::note( "ragdoll.release" );
            EditorWidgets::drawTooltip( "End the partial ragdoll and blend back to the animation pose" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset" ) );
            if ( ImGui::Button( "Reset" ) )
                _model.requestRespawn(); // 다음 그리기가 몸을 처음 자세로 다시 놓는다
            EditorSelfTestMarks::note( "ragdoll.reset" );
            EditorWidgets::drawTooltip( "Place the body again in its start pose" );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
            (void)ImGui::SliderFloat( "Impulse (N*s)", &_model.editImpulse(), 0.0f, RagdollPreviewPanelInternal::kMaxImpulse );
            ImGui::Text( "State %s  Built bodies %u  Settled %s", getEnumName( pRagdoll->getState() ), static_cast<uint32>( pRagdoll->getRagdoll()._listBody.size() ),
                         pRagdoll->isSettled() ? "yes" : "no" );
        }
        else
        {
            ImGui::TextDisabled( "No body yet - enter a prefab path and press Spawn" );
        }

        (void)ImGui::Checkbox( "Skeleton", &_model.editDrawSkeleton() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Bodies" ) );
        (void)ImGui::Checkbox( "Bodies", &_model.editDrawBodies() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Joints" ) );
        (void)ImGui::Checkbox( "Joints", &_model.editDrawJoints() );
        drawBodyTable( pRagdoll );

        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = RagdollPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        drawOverlay( pRagdoll );
        EditorSelfTestMarks::note( "ragdoll.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void RagdollPreviewPanel::drawBodyTable( const RagdollComponent* pRagdoll )
    {
        const vector<RagdollPreviewBodyRow>& listBodyRow = _model.getBodyRows();
        if ( listBodyRow.empty() )
            return;
        const float32 height = RagdollPreviewPanelInternal::kTableHeight * EditorThemeUtil::getDPIScale();
        if ( ImGui::BeginTable( "##ragdollPreview.bodies", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                                ImVec2( 0.0f, height ) ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( "Bone" );
            ImGui::TableSetupColumn( "Shapes" );
            ImGui::TableSetupColumn( "Mass" );
            ImGui::TableSetupColumn( "Joint" );
            ImGui::TableSetupColumn( "Hit zone" );
            ImGui::TableSetupColumn( "Physics" );
            ImGui::TableHeadersRow();
            for ( size_t rowIndex = 0; rowIndex < listBodyRow.size(); ++rowIndex )
            {
                const RagdollPreviewBodyRow& row = listBodyRow[rowIndex];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::PushID( static_cast<int>( rowIndex ) );
                const bool bSelected = _model.getSelectedBody() == static_cast<int32>( rowIndex );
                if ( ImGui::Selectable( row._bone.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns ) )
                    _model.toggleSelectedBody( static_cast<int32>( rowIndex ) );
                ImGui::PopID();
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._shapes.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                if ( row._mass > 0.0f )
                    ImGui::Text( "%.1f kg", row._mass );
                else
                    ImGui::TextDisabled( "auto" );
                ImGui::TableSetColumnIndex( 3 );
                // 첫 바디는 부모 쪽 바디가 없어 관절이 쓰이지 않는다(에셋 머리말).
                ImGui::TextUnformatted( getEnumName( row._jointType ) );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::TextUnformatted( row._hitZone.empty() ? "-" : row._hitZone.c_str() );
                ImGui::TableSetColumnIndex( 5 );
                const bool bBuilt = pRagdoll != nullptr && rowIndex < pRagdoll->getRagdoll()._listBody.size();
                if ( bBuilt )
                    ImGui::Text( "%.2f %s", pRagdoll->getBodyWeight( static_cast<uint32>( rowIndex ) ), pRagdoll->isBodyDynamic( static_cast<uint32>( rowIndex ) ) ? "dynamic" : "kinematic" );
                else
                    ImGui::TextDisabled( "-" );
            }
            ImGui::EndTable();
        }
        EditorSelfTestMarks::note( "ragdoll.bodyTable" );
    }

    void RagdollPreviewPanel::drawOverlay( const RagdollComponent* pRagdoll )
    {
        const float2 imagePosition = getPreviewViewport().getImagePosition();
        const float2 imageSize     = getPreviewViewport().getImageSize();
        Scene*       pScene        = getPreviewScene();
        if ( pRagdoll == nullptr || imageSize._x <= 0.0f || imageSize._y <= 0.0f || pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObject*            pOwner        = pRagdoll->getOwner();
        SkeletalMeshComponent* pUnit         = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        IPhysicsScene3D*       pPhysicsScene = pScene->getObjectManager()->getSubsystem<ScenePhysics>().findScene3D();

        float4x4 viewProjection{};
        float3   eye{};
        getPreviewViewport().computeViewProjection( imageSize._x / imageSize._y, viewProjection, eye );
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->PushClipRect( ImVec2( imagePosition._x, imagePosition._y ), ImVec2( imagePosition._x + imageSize._x, imagePosition._y + imageSize._y ), true );
        EditorProjectedLineRenderer renderer( pDrawList, viewProjection, imagePosition, imageSize );

        if ( _model.editDrawSkeleton() && pUnit != nullptr )
        {
            const vector<float4x4>& listModel  = pUnit->getModelSpaceTransforms();
            const vector<int32>&    listParent = pUnit->getSkeleton().getParentIndices();
            const float4x4          world      = pUnit->getWorldMatrix();
            const size_t            boneCount  = listModel.size() < listParent.size() ? listModel.size() : listParent.size();
            for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                const int32 parentIndex = listParent[boneIndex];
                if ( parentIndex < 0 || static_cast<size_t>( parentIndex ) >= boneCount )
                    continue;
                renderer.drawLine( float3::transform( listModel[static_cast<size_t>( parentIndex )].getTranslation(), world ), float3::transform( listModel[boneIndex].getTranslation(), world ),
                                   RagdollPreviewPanelInternal::kSkeletonColor );
            }
        }

        const PhysicsRagdoll& ragdoll = pRagdoll->getRagdoll();
        if ( pPhysicsScene != nullptr )
        {
            for ( size_t bodyIndex = 0; bodyIndex < ragdoll._listBody.size(); ++bodyIndex )
            {
                if ( pPhysicsScene->isBodyValid( ragdoll._listBody[bodyIndex] ) == false )
                    continue;
                if ( _model.editDrawBodies() )
                    (void)pPhysicsScene->drawBodyDebug( ragdoll._listBody[bodyIndex], renderer );

                float3     position{};
                quaternion rotation{};
                if ( _model.editDrawJoints() == false || pPhysicsScene->getBodyTransform( ragdoll._listBody[bodyIndex], position, rotation ) == false )
                    continue;
                const bool   bSelected = _model.getSelectedBody() == static_cast<int32>( bodyIndex );
                const float4 color     = bSelected ? RagdollPreviewPanelInternal::kSelectedColor : RagdollPreviewPanelInternal::kJointColor;
                const float  radius    = RagdollPreviewPanelInternal::kJointMarkRadius;
                renderer.drawLine( position - float3{ radius, 0.0f, 0.0f }, position + float3{ radius, 0.0f, 0.0f }, color );
                renderer.drawLine( position - float3{ 0.0f, radius, 0.0f }, position + float3{ 0.0f, radius, 0.0f }, color );
                renderer.drawLine( position - float3{ 0.0f, 0.0f, radius }, position + float3{ 0.0f, 0.0f, radius }, color );
                const int32 parentBody = bodyIndex < ragdoll._listParentBody.size() ? ragdoll._listParentBody[bodyIndex] : -1; // 루트 바디는 -1
                if ( parentBody < 0 || static_cast<size_t>( parentBody ) >= ragdoll._listBody.size() )
                    continue;
                float3     parentPosition{};
                quaternion parentRotation{};
                if ( pPhysicsScene->getBodyTransform( ragdoll._listBody[static_cast<size_t>( parentBody )], parentPosition, parentRotation ) )
                    renderer.drawLine( parentPosition, position, color );
            }
        }
        pDrawList->PopClipRect();
    }

    bool RagdollPreviewPanel::findDocumentBodyCount( uint32& outCount ) const
    {
        if ( getLoadedAssetPath().empty() || isDocumentLoadFailed() )
            return false;
        outCount = static_cast<uint32>( _model.getBodyRows().size() );
        return true;
    }

    bool RagdollPreviewPanel::findBuiltBodyCount( uint32& outCount ) const
    {
        const RagdollComponent* pRagdoll = _model.findRagdoll( getPreviewScene() );
        if ( pRagdoll == nullptr || pRagdoll->getRagdoll().isEmpty() )
            return false;
        outCount = static_cast<uint32>( pRagdoll->getRagdoll()._listBody.size() );
        return true;
    }

    bool RagdollPreviewPanel::findRagdollState( uint32& outState ) const
    {
        const RagdollComponent* pRagdoll = _model.findRagdoll( getPreviewScene() );
        if ( pRagdoll == nullptr )
            return false;
        outState = static_cast<uint32>( pRagdoll->getState() );
        return true;
    }

    bool RagdollPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, RagdollPreviewPanelInternal::kDocumentSuffix );
    }

    ToolAssetLoadResult RagdollPreviewPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        return _model.loadAsset( path );
    }

    void RagdollPreviewPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
        getPreviewViewport().frameSphere( float3{ 0.0f, RagdollPreviewPanelInternal::kFrameCenterHeight, 0.0f }, RagdollPreviewPanelInternal::kFrameRadius );
    }
} // namespace sw::editor
