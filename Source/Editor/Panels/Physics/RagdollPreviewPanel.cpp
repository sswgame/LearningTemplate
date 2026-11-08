#include "pch.h"

#include "Editor/Panels/Physics/RagdollPreviewPanel.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

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
            /** @brief 열린 래그돌 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const RagdollPreviewPanel* findOpenPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const RagdollPreviewPanel* pPanel = pPanels->findPanel<RagdollPreviewPanel>();
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
        , _ragdollState{}
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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "OpenAPhysicsXmlPhysicsAssetPickA" ) );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) )
        {
            reloadDocument();
            _model.requestRespawn(); // 다시 읽은 에셋으로 몸을 다시 놓는다
        }
        EditorSelfTestMarks::record( "ragdoll.reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "ReadThePhysicsAssetFileAgainAfter" ) );

        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "BodyPrefabSkeletalMeshOnItsRoot" ) );
        (void)EditorWidgets::drawTextField( "##ragdollPreview.body", _model.editBodyPrefabPath(), -1.0f );
        EditorSelfTestMarks::record( "ragdoll.bodyField" );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Spawn" ) ) )
            _model.spawnFromField();
        EditorSelfTestMarks::record( "ragdoll.spawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlaceTheBodyInThePreviewSceneAlt" ) );

        if ( isDocumentLoadFailed() == false ) // 에셋을 읽지 못했으면 몸을 놓지 않는다
            _model.syncBody( getPreviewScene(), getLoadedAssetPath() );
        _model.gatherRagdoll( getPreviewScene(), _ragdollState );
        if ( _ragdollState._bHasRagdoll != SW_FALSE )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Ragdoll" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Ragdoll" ) ) )
                _model.startRagdoll( getPreviewScene() );
            EditorSelfTestMarks::record( "ragdoll.drop" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "TurnEveryBodyDynamicTheBodyFalls" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Impulse" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Impulse" ) ) )
                _model.applyImpulse( getPreviewScene() );
            EditorSelfTestMarks::record( "ragdoll.impulse" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "GoRagdollAndPushThePickedBody" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Partial" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Partial" ) ) )
                _model.startPartialRagdoll( getPreviewScene() );
            EditorSelfTestMarks::record( "ragdoll.partial" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "OnlyTheBodiesUnderTheRagdollS" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Release" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Release" ) ) )
                _model.stopPartialRagdoll( getPreviewScene() );
            EditorSelfTestMarks::record( "ragdoll.release" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "EndThePartialRagdollAndBlendBack" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reset" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reset" ) ) )
                _model.requestRespawn(); // 다음 그리기가 몸을 처음 자세로 다시 놓는다
            EditorSelfTestMarks::record( "ragdoll.reset" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlaceTheBodyAgainInItsStart" ) );
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "ImpulseNS" ) );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
            (void)ImGui::SliderFloat( "##ImpulseNs", &_model.editImpulse(), 0.0f, RagdollPreviewPanelInternal::kMaxImpulse );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "StateBuiltBodiesSettled", TextArgumentList().addText( "state", _ragdollState._pStateName ).addInteger( "bodyCount", static_cast<int64>( _ragdollState._listBuiltBody.size() ) ).addText( "settled", _ragdollState._bSettled != SW_FALSE ? "yes" : "no" ) ).c_str() );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoBodyYetEnterAPrefabPathAnd" ) );
        }

        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Skeleton" ), &_model.editDrawSkeleton() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "Bodies" ) ) );
        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Bodies" ), &_model.editDrawBodies() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "Joints" ) ) );
        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Joints" ), &_model.editDrawJoints() );
        drawBodyTable();

        const float2 viewSize = computeViewportFillSize( RagdollPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        drawOverlay();
        EditorSelfTestMarks::record( "ragdoll.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void RagdollPreviewPanel::drawBodyTable()
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
                if ( rowIndex < _ragdollState._listBuiltBody.size() )
                {
                    const RagdollPreviewBuiltBody& body = _ragdollState._listBuiltBody[rowIndex];
                    ImGui::Text( "%.2f %s", body._weight, body._bDynamic != SW_FALSE ? "dynamic" : "kinematic" );
                }
                else
                    ImGui::TextDisabled( "-" );
            }
            ImGui::EndTable();
        }
        EditorSelfTestMarks::record( "ragdoll.bodyTable" );
    }

    void RagdollPreviewPanel::drawOverlay()
    {
        const float2 imagePosition = getPreviewViewport().getImagePosition();
        const float2 imageSize     = getPreviewViewport().getImageSize();
        if ( _ragdollState._bHasRagdoll == SW_FALSE || imageSize._x <= 0.0f || imageSize._y <= 0.0f )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        getPreviewViewport().computeViewProjection( imageSize._x / imageSize._y, viewProjection, eye );
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->PushClipRect( ImVec2( imagePosition._x, imagePosition._y ), ImVec2( imagePosition._x + imageSize._x, imagePosition._y + imageSize._y ), true );
        EditorProjectedLineRenderer renderer( pDrawList, viewProjection, imagePosition, imageSize );
        RagdollPreviewOverlayColors colors;
        colors._bone          = SW_THEME_COLOR( "Viewport", "Bone" ).toFloat4();
        colors._joint         = SW_THEME_COLOR( "Viewport", "Joint" ).toFloat4();
        colors._selectedJoint = SW_THEME_COLOR( "Viewport", "BoneSelected" ).toFloat4();
        _model.drawOverlay( getPreviewScene(), renderer, colors );
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
        RagdollPreviewState state;
        _model.gatherRagdoll( getPreviewScene(), state );
        if ( state._listBuiltBody.empty() )
            return false;
        outCount = static_cast<uint32>( state._listBuiltBody.size() );
        return true;
    }

    bool RagdollPreviewPanel::findRagdollState( uint32& outState ) const
    {
        RagdollPreviewState state;
        _model.gatherRagdoll( getPreviewScene(), state );
        if ( state._bHasRagdoll == SW_FALSE )
            return false;
        outState = state._state;
        return true;
    }

    bool RagdollPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, RagdollPreviewPanelInternal::kDocumentSuffix );
    }

    ToolAssetLoadResult RagdollPreviewPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        return _model.loadAsset( path );
    }

    void RagdollPreviewPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
    }

    bool RagdollPreviewPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, RagdollPreviewPanelInternal::kFrameCenterHeight, 0.0f };
        outRadius = RagdollPreviewPanelInternal::kFrameRadius;
        return true;
    }
} // namespace sw::editor
