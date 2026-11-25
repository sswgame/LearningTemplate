// 캐릭터 편집 창의 그리기 · 입력입니다(MVVM View 쪽). 문서 · 미리보기 씬 동기화 · 썸네일 · 감시 값은 `Model/CharacterEditorPanelScene.cpp` 다.
#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

#include <imgui.h>
#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr float32 kDragSpeed            = 0.005f;
            static constexpr float32 kRotationDragSpeed    = 0.5f;
            static constexpr int32   kLayerUp              = 1;  ///< 피부 층 한 칸 위(목록 뒤가 위다)
            static constexpr int32   kLayerDown            = -1; ///< 피부 층 한 칸 아래
            static constexpr float32 kPathButtonsColumns   = 32.0f;
            static constexpr float32 kBoneTreeLines        = 10.0f;
            static constexpr float32 kPresetListLines      = 5.0f;
            static constexpr float32 kExplanationListLines = 7.0f;
            static constexpr float32 kPreviewHeight        = 260.0f; ///< 미리보기 뷰포트 높이(96 DPI 픽셀)
            static constexpr float32 kCustomizationReserve = 150.0f; ///< 꾸미기 머리글이 펼쳐졌을 때 뷰포트 아래에 남기는 높이(머리글 + 슬라이더 몇 줄, 96 DPI 픽셀)
            static constexpr float32 kUVDragSpeed          = 0.002f; ///< 체액 경로 점을 끄는 빠르기(UV)
            static constexpr float32 kFillWidth            = -1.0f;  ///< ImGui 다음 칸 폭 "남은 폭 모두"

            /** @brief 입력칸 글 버퍼입니다(별칭 · 묶음 — 그리기마다 데이터 값으로 채우고 놓을 때 쓴다). */
            using TextBuffer = fixed_string<constant::buffer::kMax128>;

            /**
             * @brief 데이터 형식 @p TDef 의 칸 @p pProperty 가 받는 범위(`PROPERTY( Min · Max )`)입니다. 이 패널의 슬라이더 칸은 모두 양 끝을 적는다.
             * @details 파일을 읽는 쪽이 같은 범위로 거절하므로 에디터가 따로 정하지 않는다.
             */
            template <typename TDef>
            static InspectorNumericRange getDataRange( const utf8* pProperty )
            {
                const PropertyInfo* const pInfo = TDef::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
                SW_ASSERT( pInfo != nullptr );
                const InspectorNumericRange range = InspectorPropertyLayout::getNumericRange( *pInfo, ObjectAddress{ nullptr } );
                SW_ASSERT( range._bHasClampMin && range._bHasClampMax );
                return range;
            }

            /** @brief 데이터 속성 글의 수입니다. 읽지 못하면(빈 칸) 0 입니다 — 고치는 칸이 0 에서 시작한다. */
            static float32 readFloat( const string& text )
            {
                float32 value = 0.0f;
                (void)StringUtil::parseFloat( text, value );
                return value;
            }

            /** @brief 입력칸의 글을 이름으로 바꿉니다. 비어 있으면 빈 이름입니다. */
            static hashed_string makeName( const utf8* pText )
            {
                return pText[0] == '\0' ? hashed_string{} : hashed_string( string_view( pText ) );
            }

            /** @brief 이름 고르기 콤보입니다. 바뀌었으면 true 이고 @p outName 에 고른 이름이 담깁니다. 첫 항목은 빈 이름입니다. */
            static bool drawNameCombo( const utf8* pLabel, const hashed_string& current, const vector<hashed_string>& listName, hashed_string& outName,
                                       const utf8* pFirstNameMark = nullptr )
            {
                bool bChanged = false;
                if ( EditorWidgets::beginCombo( pLabel, current.empty() ? "(none)" : current.c_str() ) )
                {
                    if ( ImGui::Selectable( "(none)", current.empty() ) )
                    {
                        outName  = hashed_string{};
                        bChanged = true;
                    }
                    bool bFirstName = true;
                    for ( const hashed_string& name : listName )
                    {
                        const bool bSelected = name == current;
                        if ( ImGui::Selectable( name.c_str(), bSelected ) )
                        {
                            outName  = name;
                            bChanged = true;
                        }
                        if ( bFirstName && pFirstNameMark != nullptr )
                            EditorSelfTestMarks::record( pFirstNameMark ); // 자동화가 첫 이름을 누를 자리
                        bFirstName = false;
                    }
                    ImGui::EndCombo();
                }
                return bChanged;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{

    void CharacterEditorPanel::drawDocumentContent()
    {
        using PackDataState = CharacterEditorViewModel::PackDataState;
        _document.ensurePackData();
        if ( _document.getPaths()._rigPath.empty() )
            _bRigPreview = false; // 리그가 없는 게임으로 바뀌면 켜 둔 단추도 내린다
        beginPreviewFrame();
        updateFocusedDocument();
        pollThumbnailCapture();
        if ( _document.getPackDataState() == PackDataState::Ready )
            ensureDocumentLoaded(); // 팩 데이터가 경로 칸을 채운 뒤에 읽는다
        if ( _document.getPackDataState() == PackDataState::Missing )
        {
            fixed_string<constant::buffer::kMax512> hint;
            formatstring( hint.data(), hint.capacity(), "This game has no character editor data (%#)", _document.getPackDataPath() );
            EditorWidgets::drawEmptyHint( hint.c_str() );
            return;
        }
        if ( _document.getPackDataState() == PackDataState::Invalid )
        {
            EditorWidgets::drawEmptyHint( _document.getPackDataMessage().c_str() );
            return;
        }
        _document.ensureKindTable();
        _document.pollExternalChange();
        drawFileRow();
        drawHistoryRow();
        if ( _document.getStatus().empty() == false )
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "%s", _document.getStatus().c_str() );
        if ( _document.hasExternalChange() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "A socket file was changed on disk while this window has unsaved edits." );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ReloadFromDiskDiscardEdits" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "ReloadFromDiskDiscardEdits" ) ) && _document.loadSocketFile() == false )
                SW_LOG_WARNING_ID( "CharacterEditorPanel.CharacterEditorSocketFile", "Character Editor: the socket file '%#' could not be reloaded - the edits stay", _document.getPaths()._socketPath.c_str() ); // 까닭은 상태 줄에 남았다
        }
        ImGui::Separator();

        Published& published              = _published;
        published._boneCount              = _document.getPoseModel().getSkeleton().getBoneCount();
        published._socketCount            = static_cast<uint32>( _document.getSocketModel().getSockets().getSockets().size() );
        published._statusSet              = _document.getStatus().empty() ? 0u : 1u;
        published._presetCount            = static_cast<uint32>( _document.getPresetStore().getPresets().size() );
        published._equippedSlotCount      = _document.countEquippedSlots();
        const SocketDef* pPublishedSocket = _document.getSocketModel().findSocket( _document.getSelectedSocket() );
        published._selectedSocketX        = pPublishedSocket != nullptr ? pPublishedSocket->_translation._x : 0.0f;

        if ( ImGui::BeginTabBar( "##CharacterEditorTabs" ) )
        {
            const bool bSocketTab = ImGui::BeginTabItem( SW_LOCKEY( "EdPlugin", "Sockets" ) );
            EditorSelfTestMarks::record( "charactereditor.tab.sockets" );
            if ( bSocketTab )
            {
                drawSocketTab();
                ImGui::EndTabItem();
            }
            const bool bSkeletonTab = ImGui::BeginTabItem( SW_LOCKEY( "EdPlugin", "Skeleton" ) );
            EditorSelfTestMarks::record( "charactereditor.tab.skeleton" );
            if ( bSkeletonTab )
            {
                drawSkeletonTab();
                ImGui::EndTabItem();
            }
            const bool bAppearanceTab = ImGui::BeginTabItem( SW_LOCKEY( "EdPlugin", "Appearance" ) );
            EditorSelfTestMarks::record( "charactereditor.tab.appearance" );
            if ( bAppearanceTab )
            {
                drawAppearanceTab();
                ImGui::EndTabItem();
            }
            const bool bBodyTab = ImGui::BeginTabItem( SW_LOCKEY( "EdPlugin", "BodyTab" ) );
            EditorSelfTestMarks::record( "charactereditor.tab.body" );
            if ( bBodyTab )
            {
                drawBodyTab();
                ImGui::EndTabItem();
            }
            // 미리보기는 탭 하나다 — 다른 탭의 줄을 밀지 않고, 가려진 탭은 그리지 않는다(뷰포트를 부른 프레임만 렌더한다).
            const bool bPreviewTab = ImGui::BeginTabItem( SW_LOCKEY( "EdPlugin", "Preview" ) );
            EditorSelfTestMarks::record( "charactereditor.tab.preview" );
            if ( bPreviewTab )
            {
                drawPreviewTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void CharacterEditorPanel::drawPreviewTab()
    {
        if ( _document.isDatabaseLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "LoadTheAppearanceDataAppearanceTabToDress" ) );
            return;
        }
        syncPreviewBody();
        gatherPreview();
        const PreviewViewState& preview = _previewState;
        if ( preview._bHasAnimator )
            drawAnimationControls();
        if ( _document.getPaths()._rigPath.empty() == false )
        {
            ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "PreviewConstraintRig" ), &_bRigPreview );
            EditorSelfTestMarks::record( "charactereditor.rigPreview" );
        }
        const AppearanceSelection& selection = _document.getAppearanceModel().getSelection();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "WearingOnBasePreset", TextArgumentList().addText( "wearing", selection._listSlot.empty() ? "none" : "selection" ).addText( "basePresetID", selection._basePresetID.empty() ? _document.getPreviewPresetID().c_str() : selection._basePresetID.c_str() ).addInteger( "appearance", static_cast<int64>( preview._assembleCount ) ) ).c_str() );
        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = CharacterEditorPanelInternal::kPreviewHeight * EditorThemeUtil::getDPIScale();
        // 뷰포트가 남은 높이를 모두 차지하면 슬라이더가 늘 접힌 아래로 밀려 스크롤 없이는 닿지 못한다 — 펼쳐졌을 때는 아래 몇 줄 자리를 남긴다.
        const float32 reserve        = _bCustomizationOpen ? CharacterEditorPanelInternal::kCustomizationReserve * EditorThemeUtil::getDPIScale() : 0.0f;
        const float32 viewportHeight = available._y - reserve;
        syncPreviewGizmo();
        drawPreviewViewport( float2{ available._x, viewportHeight > minHeight ? viewportHeight : minHeight }, _gizmo.hasMouse() );
        _gizmo.draw( *this );
        recordGizmoMark();
        EditorSelfTestMarks::record( "charactereditor.previewScene" );
        drawBodyPickOverlay();
        // 모은 선(소켓 축 · 관통 점)을 뷰포트 이미지 위에 긋는다. 관통 점 십자 하나는 선 셋이다.
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        _published._penetrationMarkCount      = 0;
        if ( preview._bHasBody && size._x > 0.0f && size._y > 0.0f )
        {
            float4x4 viewProjection{};
            float3   eye{};
            viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
            (void)EditorViewportVisualizer::drawLines( ImGui::GetWindowDrawList(), viewProjection, position, size, preview._listSocketAxisLine );
            _published._penetrationMarkCount = EditorViewportVisualizer::drawLines( ImGui::GetWindowDrawList(), viewProjection, position, size, preview._listPenetrationLine ) / 3u;
        }
        // 슬라이더는 뷰포트 아래에 둔다 — 모습이 바뀌는 것을 보며 끈다.
        _bCustomizationOpen = ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "CustomizeReleaseASliderToRefit" ), ImGuiTreeNodeFlags_DefaultOpen );
        if ( _bCustomizationOpen )
            drawCustomizationSliders( selection );
        else
            _bCustomizationActive = SW_FALSE;
    }

    void CharacterEditorPanel::recordGizmoMark()
    {
        // 시나리오가 기즈모 가운데(화면 평면 이동 손잡이)를 끌 수 있게 표시 오브젝트의 화면 자리를 적는다.
        if ( EditorSelfTestMarks::isEnabled() == false || _bGizmoPlaced == SW_FALSE )
            return;
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        if ( size._x <= 0.0f || size._y <= 0.0f )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        const float3 origin = _placedGizmoWorld.getTranslation();
        const float4 clip   = float4::transform( float4{ origin._x, origin._y, origin._z, 1.0f }, viewProjection );
        if ( clip._w <= MathUtil::kEpsilon )
            return;
        const float2  screen{ position._x + ( clip._x / clip._w * 0.5f + 0.5f ) * size._x, position._y + ( 0.5f - clip._y / clip._w * 0.5f ) * size._y };
        const float32 half = ImGui::GetFontSize() * 0.25f;
        EditorSelfTestMarks::recordRect( "charactereditor.gizmoCenter", float2{ screen._x - half, screen._y - half }, float2{ screen._x + half, screen._y + half } );
    }

    void CharacterEditorPanel::drawAnimationControls()
    {
        const vector<hashed_string>& listClipName = _document.getClipNames();
        if ( listClipName.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "NoAnimationClipsIn", TextArgumentList().addText( "clipFolder", _document.getPaths()._animationSet.c_str() ) ).c_str() );
            return;
        }
        const hashed_string selectedClip = _document.getSelectedClip();
        EditorChrome::setNextItemWidthColumns( 32.0f );
        if ( EditorWidgets::beginCombo( "##PreviewClip", selectedClip.empty() ? "(select a clip)" : selectedClip.c_str() ) )
        {
            bool bFirst = true;
            for ( const hashed_string& name : listClipName )
            {
                if ( ImGui::Selectable( name.c_str(), name == selectedClip ) )
                    _document.selectClip( name );
                if ( bFirst )
                    EditorSelfTestMarks::record( "charactereditor.clip.first" ); // 자동화가 첫 클립을 누를 자리
                bFirst = false;
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "charactereditor.clip" );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Loop##PreviewClip" ) );
        ImGui::Checkbox( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Loop" ), "Loop##PreviewClip" ), &_bClipLoop );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Play##PreviewClip" ) );
        const hashed_string& clip = _document.getSelectedClip();
        ImGui::BeginDisabled( clip.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Play" ), "Play##PreviewClip" ) ) )
        {
            if ( playPreviewClip() )
                _document.clearStatus();
            else
                _document.setStatus( "The clip could not be played (see log)" );
        }
        EditorSelfTestMarks::record( "charactereditor.playClip" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Stop##PreviewClip" ) );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Stop" ), "Stop##PreviewClip" ) ) )
            stopPreviewClip();
    }

    void CharacterEditorPanel::drawFileRow()
    {
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        (void)EditorWidgets::drawAssetPathField( "##SocketPath", paths._socketPath, "sockets file", SocketSet::kExtension, nullptr ); // 콘텐츠 브라우저에서 끌어 놓을 수 있다
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Load" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Load" ) ) )
            reloadDocument(); // 경로 칸의 소켓 파일을 다시 읽는다 — 읽음 · 못 읽음 표시와 미저장 기준은 문서 계약이 맞춘다
        EditorSelfTestMarks::record( "charactereditor.loadSockets" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Save" ) ) );
        ImGui::BeginDisabled( canSaveDocument() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "New" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "New" ) ) )
            _document.clearSockets();
        if ( canSaveDocument() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(modified)" );
        }
    }

    void CharacterEditorPanel::drawHistoryRow()
    {
        SocketEditModel& sockets = _document.getSocketModel();
        ImGui::BeginDisabled( sockets.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Undo" ) ) )
            (void)sockets.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "charactereditor.socketUndo" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Redo" ) ) );
        ImGui::BeginDisabled( sockets.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Redo" ) ) )
            (void)sockets.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::drawSocketTab()
    {
        // 목록(왼쪽) · 고른 소켓의 상세(오른쪽) 두 칸이 남은 높이를 모두 쓴다 — 언리얼 스켈레톤 에디터의 소켓 트리 · 디테일 배치. 예전에는 7 줄 고정 목록 아래에 상세를 쌓아
        // 소켓이 많으면 목록이 잘리고 창 아래 반이 비었다(uiaudit 36).
        constexpr ImGuiTableFlags kSplitFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
        if ( ImGui::BeginTable( "##SocketSplit", 2, kSplitFlags, ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y } ) == false )
            return;
        ImGui::TableSetupColumn( "##List", ImGuiTableColumnFlags_WidthStretch, 0.35f );
        ImGui::TableSetupColumn( "##Detail", ImGuiTableColumnFlags_WidthStretch, 0.65f );
        ImGui::TableNextColumn();
        drawSocketList();
        ImGui::TableNextColumn();
        const SocketDef* pSelected = _document.getSocketModel().findSocket( _document.getSelectedSocket() );
        if ( pSelected != nullptr )
        {
            const SocketDef selected = *pSelected; // 편집이 표를 바꾸므로 복사본으로 그린다
            drawSocketDetail( selected );
        }
        else
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorCharacterEditor", "SocketNoSelection" ) );
        }
        drawSocketIssues();
        ImGui::EndTable();
    }

    void CharacterEditorPanel::drawSocketIssues()
    {
        vector<string> listIssue;
        if ( _document.getSocketModel().validate( listIssue ) == false )
        {
            for ( const string& issue : listIssue )
            {
                EditorThemeUtil::textWarning( issue.c_str() );
            }
        }
    }

    void CharacterEditorPanel::drawSocketList()
    {
        const utf8* const pAdd = SW_LOCKEY( "EditorCharacterEditor", "AddSocketButton" );
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - EditorChrome::measureSmallButton( pAdd ) - ImGui::GetStyle().ItemSpacing.x );
        EditorWidgets::inputTextWithHint( SW_WIDGET_ID( "##NewSocketName" ), SW_LOCKEY( "EditorCharacterEditor", "NewSocketHint" ), _newName.data(), _newName.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( pAdd ) )
            _document.addSocket( CharacterEditorPanelInternal::makeName( _newName.c_str() ) );

        if ( ImGui::BeginChild( "##SocketList", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders ) )
        {
            bool bFirst = true;
            for ( const SocketDef& socket : _document.getSocketModel().getSockets().getSockets() )
            {
                const bool bSelected = socket._name == _document.getSelectedSocket();
                if ( ImGui::Selectable( socket._name.c_str(), bSelected ) )
                {
                    _document.selectSocket( socket._name );
                    _renameBuffer = socket._name.c_str();
                }
                if ( bFirst )
                {
                    EditorSelfTestMarks::record( "charactereditor.socket.first" );
                    bFirst = false;
                }
            }
        }
        ImGui::EndChild();
    }

    void CharacterEditorPanel::drawSocketDetail( const SocketDef& socket )
    {
        const hashed_string name    = socket._name;
        SocketEditModel&    sockets = _document.getSocketModel();

        EditorChrome::setNextItemWidthColumns( 24.0f );
        EditorWidgets::inputText( SW_WIDGET_ID( "##Rename" ), _renameBuffer.data(), _renameBuffer.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Rename" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Rename" ) ) )
            _document.renameSocket( name, CharacterEditorPanelInternal::makeName( _renameBuffer.c_str() ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Mirror" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Mirror" ) ) )
            _document.mirrorSocket( name );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
        {
            _document.removeSocket( name );
            return;
        }

        fixed_string<constant::buffer::kMax128> parent;
        parent = socket._parent.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Parent" ) );
        if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Parent" ), parent.data(), parent.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            _document.reportEdit( sockets.setParent( name, CharacterEditorPanelInternal::makeName( parent.c_str() ) ) );

        hashed_string pickedKind;
        if ( CharacterEditorPanelInternal::drawNameCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Kind" ), "Kind" ), socket._kind, sockets.getKindTable().getKinds(), pickedKind ) )
            _document.reportEdit( sockets.setKind( name, pickedKind ) );

        float3 translation = socket._translation;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Translation" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Translation" ), &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setTranslation( name, translation, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 degrees = socket._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Rotation" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Rotation" ), &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( sockets.setRotationDegrees( name, degrees, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 scale = socket._scale;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Scale" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Scale" ), &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setScale( name, scale, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        int32 anchorIndex = socket._anchor == SocketAnchor::Surface ? 1 : 0;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Anchor" ) );
        if ( EditorWidgets::combo( SW_WIDGET_ID( "##Anchor" ), &anchorIndex, "Bone\0Surface\0" ) )
            _document.reportEdit( sockets.setAnchor( name, anchorIndex == 1 ? SocketAnchor::Surface : SocketAnchor::Bone ) );

        fixed_string<constant::file::kMaxPathSize> previewMesh;
        previewMesh = string_view( socket._previewMesh );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "PreviewMesh" ) );
        if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Previewmesh" ), previewMesh.data(), previewMesh.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            _document.reportEdit( sockets.setPreviewMesh( name, previewMesh.c_str() ) );
    }

    void CharacterEditorPanel::drawSkeletonTab()
    {
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        BonePoseEditModel&    pose  = _document.getPoseModel();
        (void)EditorWidgets::drawAssetPathField( "##SkeletonPath", paths._skeletonPath, "skeleton", nullptr, nullptr );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "LoadSkeleton" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "LoadSkeleton" ) ) )
            _document.loadSkeleton();
        EditorSelfTestMarks::record( "charactereditor.loadSkeleton" );

        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        (void)EditorWidgets::drawAssetPathField( "##RefPosePath", paths._refPosePath, "reference pose", nullptr, nullptr );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "LoadPose" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "LoadPose" ) ) )
            _document.loadReferencePose();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "SavePose" ) ) );
        ImGui::BeginDisabled( pose.isDirty() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "SavePose" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();

        ImGui::BeginDisabled( pose.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "UndoPose" ) ) )
            (void)pose.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "RedoPose" ) ) );
        ImGui::BeginDisabled( pose.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "RedoPose" ) ) )
            (void)pose.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "BonesOverridden", TextArgumentList().addInteger( "boneCount", static_cast<int64>( pose.getSkeleton().getBoneCount() ) ).addInteger( "overridesCount", static_cast<int64>( pose.getOverride().getOverrides().size() ) ).addText( "dirty", pose.isDirty() ? " (modified)" : "" ) ).c_str() );

        ImGui::Separator();
        if ( pose.getSkeleton().getBoneCount() == 0 )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "LoadASkeletonToSeeItsBonesThe" ) );
            return;
        }
        if ( ImGui::BeginChild( "##BoneTree", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CharacterEditorPanelInternal::kBoneTreeLines ) ), ImGuiChildFlags_Borders ) )
        {
            vector<uint32> listRoot;
            pose.collectChildren( -1, listRoot );
            for ( const uint32 rootIndex : listRoot )
            {
                drawBoneNode( rootIndex );
            }
        }
        ImGui::EndChild();
        drawBoneDetail();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "ViewportGizmoEditingNeedsThePreviewSceneEditorMultiWorld" ) );
    }

    void CharacterEditorPanel::drawBoneNode( uint32 boneIndex )
    {
        const BonePoseEditModel& pose = _document.getPoseModel();
        const hashed_string      bone = pose.getSkeleton().getBone( boneIndex )._name;
        vector<uint32>           listChild;
        pose.collectChildren( static_cast<int32>( boneIndex ), listChild );

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if ( listChild.empty() )
            flags |= ImGuiTreeNodeFlags_Leaf;
        if ( bone == _document.getSelectedBone() )
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool bOverridden = pose.hasOverride( bone );
        ImGui::PushID( static_cast<int32>( boneIndex ) );
        const bool bOpen = ImGui::TreeNodeEx( bone.c_str(), flags, "%s%s", bone.c_str(), bOverridden ? " *" : "" );
        if ( EditorInputGUI::wasItemClicked() )
            _document.selectBone( bone );
        if ( boneIndex == 0 )
            EditorSelfTestMarks::record( "charactereditor.bone.root" );
        if ( bOpen )
        {
            for ( const uint32 childIndex : listChild )
            {
                drawBoneNode( childIndex );
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void CharacterEditorPanel::drawBoneDetail()
    {
        BonePoseEditModel&  pose = _document.getPoseModel();
        const hashed_string bone = _document.getSelectedBone();
        BoneTransform       current;
        if ( bone.empty() || pose.getEffectiveTransform( bone, current ) == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SelectABoneToEditItsReferencePose" ) );
            return;
        }
        ImGui::TextUnformatted( bone.c_str() );

        float3 translation = current._translation;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BoneTranslation" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Bonetranslation" ), &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( pose.setTranslation( bone, translation, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();
        EditorSelfTestMarks::record( "charactereditor.bone.translation" );

        float3 degrees = current._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BoneRotation" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Bonerotation" ), &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( pose.setRotationDegrees( bone, degrees, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();

        float3 scale = current._scale;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BoneScale" ) );
        if ( EditorWidgets::dragFloat3( SW_WIDGET_ID( "##Bonescale" ), &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( pose.setScale( bone, scale, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();

        ImGui::BeginDisabled( pose.hasOverride( bone ) == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ResetBone" ) ) )
            _document.reportEdit( pose.clearBone( bone ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "MirrorBone" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "MirrorBone" ) ) )
            _document.mirrorSelectedBone();
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::drawAppearanceTab()
    {
        drawAppearanceDataRow();
        // 데이터 프리셋 내보내기는 데이터 줄 바로 아래 — 씬이 쓰는 결과물이라 창을 열면 보이는 자리에 둔다.
        drawDataPresetRow();
        ImGui::Separator();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SelectionBasePresetBodySlotsCustomizationValues" ) );
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        const AppearanceSelection selection  = appearance.getSelection();
        drawSelectionHeader( selection );
        ImGui::Separator();
        drawSlotRows( selection );
        ImGui::Separator();
        drawCustomizationRows( selection );

        ImGui::BeginDisabled( appearance.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "UndoAppearance" ) ) )
            (void)appearance.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "RedoAppearance" ) ) );
        ImGui::BeginDisabled( appearance.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "RedoAppearance" ) ) )
            (void)appearance.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();

        ImGui::Separator();
        drawExplanation();
        drawFitCheck();
        drawPresetRow();
    }

    void CharacterEditorPanel::drawBodyTab()
    {
        // MVVM: 그리기마다 한 번 모으고(ViewModel), 상태만 읽어 그리며, 바꿀 때는 이름 붙은 명령을 부른다.
        AppearanceEditModel& appearance = _document.getAppearanceModel();
        gatherBodyTab();
        const BodyCustomizationViewState& state = _bodyState;
        _document.openBodyData( state._bodyShapes ); // 고친 파일은 다시 읽지 않는다
        _published._bodyDataDirty       = _document.isBodyDataDirty() ? 1u : 0u;
        _published._bodyLayerCount      = static_cast<uint32>( state._listLayer.size() );
        _published._bodyHiddenSlotCount = static_cast<uint32>( std::count_if( state._listSlot.begin(), state._listSlot.end(), []( const BodyCustomizationViewState::SlotRow& row )
        { return row._bHidden == SW_TRUE; } ) );
        if ( _document.isDatabaseLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "LoadTheItemsFileAndTheAppearanceFolder" ) );
            return;
        }

        // 슬롯 숨김 — 투구 숨기기처럼 장비는 그대로 두고 외형 · 몸 채널만 뺀다.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "BodySlotVisibility" ) );
        for ( const BodyCustomizationViewState::SlotRow& row : state._listSlot )
        {
            bool bHidden = row._bHidden == SW_TRUE;
            ImGui::PushID( row._slot.c_str() );
            if ( ImGui::Checkbox( "##hidden", &bHidden ) )
                (void)BodyCustomizationViewModel::setSlotHidden( appearance, row._slot, bHidden );
            if ( row._item.empty() == false )
                EditorSelfTestMarks::record( "charactereditor.body.slot.equipped" ); // 입은 칸의 숨김 — 자동화가 누를 자리(같은 프레임의 첫 것이 남는다)
            ImGui::SameLine();
            ImGui::TextUnformatted( row._slot.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", row._item.empty() ? "-" : row._item.c_str() );
            ImGui::PopID();
        }
        EditorSelfTestMarks::record( "charactereditor.body.slots" );
        ImGui::Separator();

        drawBodyDataRow();
        ImGui::Separator();
        drawBodyChannelRows( state );
        ImGui::Separator();
        drawBodySectionRows( state );
        ImGui::Separator();
        drawSkinLayerRows( state );
        ImGui::Separator();
        drawBodyDynamicsPreview( state );
        ImGui::Separator();
        drawBodyProfileRows( state );
    }

    void CharacterEditorPanel::drawBodyDataRow()
    {
        BodyDataEditModel* arrModel[] = { &_document.getBodyShapesModel(), &_document.getBodyProfilesModel(), &_document.getVisualsModel() };
        bool               bCanUndo   = false;
        bool               bCanRedo   = false;
        for ( const BodyDataEditModel* pModel : arrModel )
        {
            bCanUndo = bCanUndo || pModel->canUndo();
            bCanRedo = bCanRedo || pModel->canRedo();
        }
        // 세 파일은 기록이 따로다 — 되돌리기는 기록이 있는 파일 중 마지막에 고친 것부터가 아니라 표 순서다(파일마다의 단계는 그대로 지킨다).
        ImGui::BeginDisabled( bCanUndo == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "BodyDataUndo" ) ) )
        {
            for ( BodyDataEditModel* pModel : arrModel )
            {
                if ( pModel->canUndo() && pModel->undo() )
                    break;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( bCanRedo == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "BodyDataRedo" ) ) )
        {
            for ( BodyDataEditModel* pModel : arrModel )
            {
                if ( pModel->canRedo() && pModel->redo() )
                    break;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _document.isBodyDataDirty() == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "BodyDataSave" ) ) )
            (void)_document.saveBodyData( getAssetWriter() ); // 실패 까닭은 상태 줄에 남는다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "charactereditor.body.dataSave" );
        if ( _document.isBodyDataDirty() )
        {
            ImGui::SameLine();
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "%s", SW_LOCKEY( "EdPlugin", "BodyDataUnsaved" ) );
        }
    }

    void CharacterEditorPanel::drawBodyChannelRows( const BodyCustomizationViewState& state )
    {
        using Internal            = CharacterEditorPanelInternal;
        BodyDataEditModel& shapes = _document.getBodyShapesModel();
        // 몸 채널 — 장비 · 프로필 · 슬라이더를 모은 값과 이 몸의 매핑(스켈레톤마다 *.bodyshape.xml). 매핑은 이 파일에서 고친다.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "BodyChannels" ) );
        if ( state._listChannel.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "BodyChannelsEmpty" ) );
            return;
        }
        if ( ImGui::BeginTable( "##BodyChannels", 4, ImGuiTableFlags_SizingStretchProp ) == false )
            return;
        for ( const BodyCustomizationViewState::ChannelRow& row : state._listChannel )
        {
            ImGui::PushID( row._channel.c_str() );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( row._channel.c_str() );
            ImGui::TableNextColumn();
            ImGui::Text( "%.3f", row._value );
            ImGui::TableNextColumn();
            const utf8* pMapping = row._mapping == BodyChannelMapping::Mapped      ? SW_LOCKEY( "EdPlugin", "BodyChannelMapped" )
                                 : row._mapping == BodyChannelMapping::Unsupported ? SW_LOCKEY( "EdPlugin", "BodyChannelUnsupported" )
                                                                                   : SW_LOCKEY( "EdPlugin", "BodyChannelMissing" );
            if ( row._mapping == BodyChannelMapping::Missing )
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", pMapping ); // 상태 색은 상태에만 — 매핑이 없으면 로드 오류다
            else
                ImGui::TextDisabled( "%s", pMapping );
            ImGui::TableNextColumn();
            ImGui::BeginDisabled( shapes.isOpen() == false );
            const BodyDataElementKey arrAxis[] = {
                BodyDataElementKey{ "Axis", "name", string( row._channel.c_str() ) }
            };
            if ( row._mapping == BodyChannelMapping::Missing )
            {
                if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "ChannelMap" ) ) )
                    (void)BodyCustomizationViewModel::addAxis( shapes, row._channel );
                ImGui::SameLine();
                if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "ChannelUnsupported" ) ) )
                    (void)BodyCustomizationViewModel::declareUnsupported( shapes, row._channel );
            }
            else if ( shapes.hasElement( arrAxis ) )
            {
                // 축 범위 — 슬라이더 값이 [min, max] 안에서 본 · 모프로 옮겨진다. 끄는 동안 한 되돌리기 단계.
                float32 arrRange[2] = { Internal::readFloat( shapes.readAttribute( arrAxis, "min" ) ), Internal::readFloat( shapes.readAttribute( arrAxis, "max" ) ) };
                if ( EditorWidgets::dragFloat2( SW_WIDGET_ID( "##range" ), arrRange, Internal::kDragSpeed ) )
                {
                    (void)BodyCustomizationViewModel::setAxisValue( shapes, row._channel, "min", arrRange[0], true );
                    (void)BodyCustomizationViewModel::setAxisValue( shapes, row._channel, "max", arrRange[1], true );
                }
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    shapes.endContinuousEdit();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
        EditorSelfTestMarks::record( "charactereditor.body.channels" );
    }

    void CharacterEditorPanel::drawBodySectionRows( const BodyCustomizationViewState& state )
    {
        using Internal             = CharacterEditorPanelInternal;
        BodyDataEditModel& visuals = _document.getVisualsModel();
        // 염색 구간 — 별칭 · 묶음 · 염색 여부와 해석이 건 염색 채널 수. 뷰포트에서 집으면 그 구간 줄을 고르고, 드라이브를 고르면 그 include · exclude 가 고르는 구간을 강조한다.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "DyeSections" ) );
        if ( state._listSection.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "DyeSectionsEmpty" ) );
            return;
        }
        bool bPickingSections = _bBodyPicking == SW_TRUE && _bodyPickLayer == constant::invalid_index::kUint32;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "PickSection" ), &bPickingSections ) )
        {
            _bBodyPicking  = bPickingSections ? SW_TRUE : SW_FALSE;
            _bodyPickLayer = constant::invalid_index::kUint32;
        }
        EditorSelfTestMarks::record( "charactereditor.body.pickSection" );
        ImGui::SameLine();
        const utf8* pDriveLabel = _bodyHighlightDrive < state._listDrive.size() ? state._listDrive[_bodyHighlightDrive]._label.c_str() : SW_LOCKEY( "EdPlugin", "HighlightNone" );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPlugin", "HighlightDrive" ), "HighlightDrive" ), pDriveLabel ) )
        {
            if ( ImGui::Selectable( SW_LOCKEY( "EdPlugin", "HighlightNone" ), _bodyHighlightDrive >= state._listDrive.size() ) )
                _bodyHighlightDrive = constant::invalid_index::kUint32;
            for ( uint32 drive = 0; drive < static_cast<uint32>( state._listDrive.size() ); ++drive )
            {
                if ( ImGui::Selectable( state._listDrive[drive]._label.c_str(), drive == _bodyHighlightDrive ) )
                    _bodyHighlightDrive = drive;
                if ( drive == 0 )
                    EditorSelfTestMarks::record( "charactereditor.body.highlight.first" );
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "charactereditor.body.highlight" );
        if ( ImGui::BeginTable( "##DyeSections", 5, ImGuiTableFlags_SizingStretchProp ) == false )
            return;
        ImGui::TableSetupColumn( SW_LOCKEY( "EdPlugin", "ColumnSection" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EdPlugin", "ColumnAlias" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EdPlugin", "ColumnGroups" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EdPlugin", "ColumnDyeable" ) );
        ImGui::TableSetupColumn( SW_LOCKEY( "EdPlugin", "ColumnDyeChannels" ) );
        ImGui::TableHeadersRow();
        for ( uint32 index = 0; index < static_cast<uint32>( state._listSection.size() ); ++index )
        {
            const BodyCustomizationViewState::SectionRow& row = state._listSection[index];
            ImGui::PushID( static_cast<int32>( index ) );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // 강조는 데이터 선택이다 — 강조색은 고른 줄 · 드라이브가 고른 구간에만(앱 화면 규칙).
            if ( ImGui::Selectable( row._section.c_str(), _bodySelectedSection == index || row._bHighlighted == SW_TRUE, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap ) )
                _bodySelectedSection = index;
            ImGui::TableNextColumn();
            ImGui::BeginDisabled( visuals.isOpen() == false );
            Internal::TextBuffer alias{ row._alias.c_str() };
            ImGui::SetNextItemWidth( Internal::kFillWidth );
            EditorWidgets::inputText( SW_WIDGET_ID( "##alias" ), alias.data(), alias.max_size() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                (void)BodyCustomizationViewModel::setSectionAlias( visuals, row, alias.c_str() );
            ImGui::TableNextColumn();
            Internal::TextBuffer groups{ row._groups.c_str() };
            ImGui::SetNextItemWidth( Internal::kFillWidth );
            EditorWidgets::inputText( SW_WIDGET_ID( "##groups" ), groups.data(), groups.max_size() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                (void)BodyCustomizationViewModel::setSectionGroups( visuals, row, StringUtil::replace( groups.c_str(), " ", "" ) );
            ImGui::TableNextColumn();
            bool bDyeable = row._bDyeable == SW_TRUE;
            if ( ImGui::Checkbox( "##dyeable", &bDyeable ) )
                (void)BodyCustomizationViewModel::setSectionDyeable( visuals, row, bDyeable );
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            ImGui::Text( "%u", row._dyeCount );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void CharacterEditorPanel::drawBodyProfileRows( const BodyCustomizationViewState& state )
    {
        using Internal              = CharacterEditorPanelInternal;
        BodyDataEditModel& profiles = _document.getBodyProfilesModel();
        if ( state._bodyProfile.empty() )
            return;
        ImGui::BeginDisabled( profiles.isOpen() == false );
        // 지글 — 리그의 Jiggle 노드 묶음마다 물성. 파생 프로필에서 고치면 그 프로필에 덮어쓰기 원소가 생긴다(상속 표가 group 으로 합친다).
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "JiggleGroups" ) );
        for ( const BodyCustomizationViewState::JiggleRow& row : state._listJiggle )
        {
            const RigJiggleGroupParams& params = row._params;
            ImGui::PushID( params._group.c_str() );
            ImGui::TextUnformatted( params._group.c_str() );
            struct JiggleField
            {
                const utf8* _pAttribute;
                const utf8* _pProperty;
                float32     _value;
            };
            const JiggleField arrField[] = {
                {      "stiffness",    "_stiffness",          params._stiffness},
                {        "damping",      "_damping",            params._damping},
                {   "gravityScale", "_gravityScale",       params._gravityScale},
                {"maxSwingDegrees",     "_maxSwing", params._maxSwing.degrees()},
                {   "impulseScale", "_impulseScale",       params._impulseScale}
            };
            for ( const JiggleField& field : arrField )
            {
                const InspectorNumericRange range = Internal::getDataRange<RigJiggleGroupParams>( field._pProperty );
                float32                     value = field._value;
                ImGui::PushID( field._pAttribute );
                EditorWidgets::drawPropertyRowLabel( field._pAttribute ); // 이름은 데이터 칸 이름이다(파일과 같은 철자)
                const bool bChanged = EditorWidgets::sliderFloat( SW_WIDGET_ID( "##value" ), &value, static_cast<float32>( range._clampMin ), static_cast<float32>( range._clampMax ) );
                ImGui::PopID();
                if ( bChanged )
                    (void)BodyCustomizationViewModel::setJiggleValue( profiles, state._bodyProfile, params._group, field._pAttribute, value, true );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    profiles.endContinuousEdit();
            }
            ImGui::PopID();
        }
        EditorSelfTestMarks::record( "charactereditor.body.jiggle" );
        ImGui::Separator();

        // 주름 — 표정이 주름 마스크 채널을 얼마나 켜는지.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "Wrinkles" ) );
        const InspectorNumericRange intensityRange = Internal::getDataRange<CharacterBodyProfileDef>( "_wrinkleIntensity" );
        const InspectorNumericRange maskRange      = Internal::getDataRange<WrinkleRegionDef>( "_mask" );
        const InspectorNumericRange weightRange    = Internal::getDataRange<WrinkleSourceDef>( "_weight" );
        float32                     intensity      = state._wrinkleIntensity;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "WrinkleIntensity" ) );
        if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##wrinkleIntensity" ), &intensity, static_cast<float32>( intensityRange._clampMin ), static_cast<float32>( intensityRange._clampMax ) ) )
            (void)BodyCustomizationViewModel::setWrinkleIntensity( profiles, state._bodyProfile, intensity, true );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            profiles.endContinuousEdit();
        for ( const BodyCustomizationViewState::WrinkleRow& row : state._listWrinkle )
        {
            const WrinkleRegionDef& region = row._region;
            ImGui::PushID( region._region.c_str() );
            ImGui::TextUnformatted( region._region.c_str() );
            ImGui::SameLine();
            int32 mask = static_cast<int32>( region._mask );
            EditorWidgets::drawInlineLabel( "mask" ); // 데이터 칸 이름
            if ( EditorWidgets::sliderInt( SW_WIDGET_ID( "##mask" ), &mask, static_cast<int32>( maskRange._clampMin ), static_cast<int32>( maskRange._clampMax ) ) )
                (void)BodyCustomizationViewModel::setWrinkleMask( profiles, state._bodyProfile, region._region, static_cast<uint32>( mask ) );
            for ( const WrinkleSourceDef& source : region._listSource )
            {
                float32 weight = source._weight;
                ImGui::PushID( source._expression.c_str() );
                EditorWidgets::drawPropertyRowLabel( source._expression.c_str() ); // 표정 이름은 데이터 이름이다
                const bool bWeightChanged = EditorWidgets::sliderFloat( SW_WIDGET_ID( "##weight" ), &weight, static_cast<float32>( weightRange._clampMin ), static_cast<float32>( weightRange._clampMax ) );
                ImGui::PopID();
                if ( bWeightChanged )
                    (void)BodyCustomizationViewModel::setWrinkleWeight( profiles, state._bodyProfile, region._region, source._expression, weight, true );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    profiles.endContinuousEdit();
            }
            ImGui::PopID();
        }
        ImGui::Separator();

        // 체액 경로 — 얼굴 UV 위 흐름 길(점 둘 이상)과 흐름 값.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "FluidPaths" ) );
        for ( const BodyCustomizationViewState::FluidRow& row : state._listFluid )
        {
            ImGui::PushID( row._name.c_str() );
            ImGui::TextUnformatted( row._name.c_str() );
            vector<float2> listPoint = row._listPathPoint;
            bool           bChanged  = false;
            for ( size_t point = 0; point < listPoint.size(); ++point )
            {
                ImGui::PushID( static_cast<int32>( point ) );
                bChanged = EditorWidgets::dragFloat2( SW_WIDGET_ID( "##point" ), &listPoint[point]._x, Internal::kUVDragSpeed, 0.0f, 1.0f ) || bChanged;
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    profiles.endContinuousEdit();
                ImGui::PopID();
            }
            if ( bChanged )
                (void)BodyCustomizationViewModel::setFluidPath( profiles, state._bodyProfile, row._name, listPoint, true );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "FluidAddPoint" ) ) && listPoint.empty() == false )
            {
                listPoint.push_back( listPoint.back() );
                (void)BodyCustomizationViewModel::setFluidPath( profiles, state._bodyProfile, row._name, listPoint, false );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled( listPoint.size() <= 2 );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "FluidRemovePoint" ) ) )
            {
                listPoint.pop_back();
                (void)BodyCustomizationViewModel::setFluidPath( profiles, state._bodyProfile, row._name, listPoint, false );
            }
            ImGui::EndDisabled();
            struct FluidField
            {
                const utf8* _pAttribute;
                const utf8* _pProperty;
                float32     _value;
                float32     _speed;
            };
            const FluidField arrField[] = {
                {"ratePerSecond", "_ratePerSecond", row._ratePerSecond,   Internal::kDragSpeed},
                {        "speed",         "_speed",         row._speed, Internal::kUVDragSpeed},
                {       "radius",        "_radius",        row._radius, Internal::kUVDragSpeed}
            };
            for ( const FluidField& field : arrField )
            {
                const InspectorNumericRange range = Internal::getDataRange<BodyFluidDef>( field._pProperty );
                float32                     value = field._value;
                ImGui::PushID( field._pAttribute );
                EditorWidgets::drawPropertyRowLabel( field._pAttribute ); // 데이터 칸 이름
                const bool bValueChanged = EditorWidgets::dragFloat( SW_WIDGET_ID( "##value" ), &value, field._speed, static_cast<float32>( range._clampMin ), static_cast<float32>( range._clampMax ) );
                ImGui::PopID();
                if ( bValueChanged )
                    (void)BodyCustomizationViewModel::setFluidValue( profiles, state._bodyProfile, row._name, field._pAttribute, value, true );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    profiles.endContinuousEdit();
            }
            ImGui::PopID();
        }
        EditorSelfTestMarks::record( "charactereditor.body.fluids" );
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::drawBodyPickOverlay()
    {
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        if ( size._x <= 0.0f || size._y <= 0.0f || _previewState._bHasBody == false )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        (void)EditorViewportVisualizer::drawLines( ImGui::GetWindowDrawList(), viewProjection, position, size, _previewState._listPickLine );
        if ( _bBodyPicking == SW_FALSE )
            return;
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "PickActiveHint" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "PickCancel" ) ) || EditorInputGUI::wasCancelPressed() )
        {
            _bBodyPicking = SW_FALSE;
            return;
        }
        const EditorPointerState pointer       = EditorInputGUI::readPointer();
        const float2&            mouse         = pointer._position;
        const bool               bInsideImage  = mouse._x >= position._x && mouse._y >= position._y && mouse._x < position._x + size._x && mouse._y < position._y + size._y;
        const bool               bDown         = pointer.isDown( EditorMouseButton::Left );
        const bool               bPress        = bInsideImage && bDown && _gizmo.hasMouse() == false;
        const bool               bPlacingLayer = _bodyPickLayer != constant::invalid_index::kUint32;
        // 구간 고르기는 누른 프레임 한 번, 층 배치는 누른 동안 계속 따라간다.
        if ( bPress == false || ( bPlacingLayer == false && pointer.wasPressed( EditorMouseButton::Left ) == false ) )
        {
            if ( _bBodyPickDrag == SW_TRUE && bDown == false )
            {
                _document.getAppearanceModel().endContinuousEdit(); // 끌기가 끝났다 — 다음 끌기는 새 되돌리기 단계
                _bBodyPickDrag = SW_FALSE;
            }
            return;
        }
        Ray ray{};
        if ( EditorViewportProjectionUtil::makeMouseRay( viewProjection.invert(), position, size, ray ) )
            pickBodySurface( ray );
    }

    void CharacterEditorPanel::drawSkinLayerRows( const BodyCustomizationViewState& state )
    {
        const AppearanceDatabase& database   = _document.getDatabase();
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SkinLayers" ) );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPlugin", "AddSkinLayer" ), "AddSkinLayer" ), "" ) )
        {
            bool bPlaceableMarked = false;
            for ( size_t index = 0; index < state._listAvailableLayer.size(); ++index )
            {
                const hashed_string& layer = state._listAvailableLayer[index];
                if ( ImGui::Selectable( layer.c_str() ) )
                    (void)BodyCustomizationViewModel::addSkinLayer( appearance, database, layer );
                if ( index == 0 )
                    EditorSelfTestMarks::record( "charactereditor.body.addLayer.first" );
                const SkinLayerDef* pDef = database.getSkinLayers().findLayer( layer );
                if ( bPlaceableMarked == false && pDef != nullptr && pDef->_bPlaceable )
                {
                    EditorSelfTestMarks::record( "charactereditor.body.addLayer.placeable" ); // 배치 집기를 켤 수 있는 첫 층(문신 · 흉터)
                    bPlaceableMarked = true;
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "charactereditor.body.addLayer" );
        if ( state._listLayer.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "SkinLayersEmpty" ) );
            return;
        }
        // 목록 뒤가 위다 — 위층부터 그려 위가 위에 보이게 한다.
        for ( size_t reverse = state._listLayer.size(); reverse > 0; --reverse )
        {
            const uint32                                index = static_cast<uint32>( reverse - 1 );
            const BodyCustomizationViewState::LayerRow& row   = state._listLayer[index];
            SkinLayerValue                              value = row._value;
            ImGui::PushID( static_cast<int32>( index ) );
            bool bVisible = value._bHidden == SW_FALSE;
            if ( ImGui::Checkbox( "##visible", &bVisible ) )
            {
                value._bHidden = bVisible ? SW_FALSE : SW_TRUE;
                (void)BodyCustomizationViewModel::setSkinLayer( appearance, index, value, false );
            }
            ImGui::SameLine();
            ImGui::TextUnformatted( value._layer.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", row._category.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "MoveUp" ) ) )
                (void)BodyCustomizationViewModel::moveSkinLayer( appearance, index, CharacterEditorPanelInternal::kLayerUp );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "MoveDown" ) ) )
                (void)BodyCustomizationViewModel::moveSkinLayer( appearance, index, CharacterEditorPanelInternal::kLayerDown );
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "RemoveSkinLayer" ) ) )
                (void)BodyCustomizationViewModel::removeSkinLayer( appearance, index );

            // 색 — 견본 묶음이 있으면 견본 단추만(염색과 같은 팔레트), 없으면 자유 색.
            if ( row._listSwatch.empty() )
            {
                if ( EditorWidgets::colorEdit4( SW_WIDGET_ID( "##color" ), &value._color._x, ImGuiColorEditFlags_NoInputs ) )
                    (void)BodyCustomizationViewModel::setSkinLayer( appearance, index, value, ImGui::IsItemActive() );
            }
            for ( size_t swatch = 0; swatch < row._listSwatch.size(); ++swatch )
            {
                const float4& color = row._listSwatch[swatch];
                ImGui::PushID( static_cast<int32>( swatch ) );
                if ( ImGui::ColorButton( "##swatch", ImVec4( color._x, color._y, color._z, color._w ) ) )
                {
                    value._color = color;
                    (void)BodyCustomizationViewModel::setSkinLayer( appearance, index, value, false );
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerOpacity" ), SW_LOCKEY( "EdPlugin", "LayerOpacityTooltip" ) );
            if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##opacity" ), &value._opacity, 0.0f, 1.0f ) )
                (void)BodyCustomizationViewModel::setSkinLayer( appearance, index, value, true );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                appearance.endContinuousEdit();
            if ( row._bPlaceable == SW_TRUE )
            {
                // 표면 집기 배치 — 켜고 미리보기 탭에서 몸을 누르고 끈다(맞은 UV 가 층 가운데가 된다).
                const bool bPlacing = _bBodyPicking == SW_TRUE && _bodyPickLayer == index;
                if ( ImGui::Selectable( SW_LOCKEY( "EdPlugin", "PickPlaceLayer" ), bPlacing, ImGuiSelectableFlags_None, ImVec2( ImGui::CalcTextSize( SW_LOCKEY( "EdPlugin", "PickPlaceLayer" ) ).x, 0.0f ) ) )
                {
                    _bBodyPicking  = bPlacing ? SW_FALSE : SW_TRUE;
                    _bodyPickLayer = bPlacing ? constant::invalid_index::kUint32 : index;
                }
                if ( index == 0 || reverse == state._listLayer.size() )
                    EditorSelfTestMarks::record( "charactereditor.body.placeLayer" );
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerOffset" ), SW_LOCKEY( "EdPlugin", "LayerOffsetTooltip" ) );
                bool bChanged = EditorWidgets::dragFloat2( SW_WIDGET_ID( "##offset" ), &value._offset._x, CharacterEditorPanelInternal::kDragSpeed, 0.0f, 1.0f );
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerScale" ), SW_LOCKEY( "EdPlugin", "LayerScaleTooltip" ) );
                bChanged = EditorWidgets::dragFloat2( SW_WIDGET_ID( "##scale" ), &value._scale._x, CharacterEditorPanelInternal::kDragSpeed, SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale ) || bChanged;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerRotation" ), SW_LOCKEY( "EdPlugin", "LayerRotationTooltip" ) );
                float32 rotationDegrees = value._rotation.degrees();
                if ( EditorWidgets::dragFloat( SW_WIDGET_ID( "##rotation" ), &rotationDegrees, CharacterEditorPanelInternal::kRotationDragSpeed, -SkinLayerValue::kMaxRotation, SkinLayerValue::kMaxRotation ) )
                {
                    value._rotation = Radians::fromDegrees( rotationDegrees );
                    bChanged        = true;
                }
                bool bMirror = value._bMirror == SW_TRUE;
                if ( ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "LayerMirror" ), &bMirror ) )
                {
                    value._bMirror = bMirror ? SW_TRUE : SW_FALSE;
                    bChanged       = true;
                }
                if ( bChanged )
                    (void)BodyCustomizationViewModel::setSkinLayer( appearance, index, value, true );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    appearance.endContinuousEdit();
            }
            ImGui::PopID();
        }
    }

    void CharacterEditorPanel::drawBodyDynamicsPreview( const BodyCustomizationViewState& state )
    {
        using PreviewAction = BodyCustomizationViewModel::PreviewAction;
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "BodyDynamics" ) );
        if ( state._bHasBody == SW_FALSE || state._bodyProfile.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "BodyDynamicsNoProfile" ) );
            return;
        }
        // 미리보기 단추는 몸만 바꾼다(기록 없음) — 맞음은 지글, 울기 · 침은 체액 방아쇠, 지우기는 화장 번짐.
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "PreviewHit" ) ) )
            postBodyPreview( { PreviewAction::Hit, {}, SW_FALSE } );
        EditorSelfTestMarks::record( "charactereditor.body.hit" );
        for ( const hashed_string& trigger : state._listFluidTrigger )
        {
            ImGui::SameLine();
            if ( ImGui::Button( trigger.c_str() ) ) // 단추 이름은 데이터의 방아쇠 이름(Cry · Drool)이다
                postBodyPreview( { PreviewAction::Fluid, trigger, SW_FALSE } );
        }
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ClearSmears" ) ) )
            postBodyPreview( { PreviewAction::ClearSmears, {}, SW_FALSE } );
        // 동작 태그 시험(달리기 · 공격) — 몸에 태그를 켜 지글 동작 배율을 걸고, 켠 동안 걸음 간격마다 착지 충격을 준다.
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "ActionTagTest" ) );
        bool bAnyActive = false;
        for ( const BodyCustomizationViewState::ActionRow& action : state._listAction )
        {
            bool bActive = action._bActive == SW_TRUE;
            ImGui::SameLine();
            ImGui::PushID( action._tag.c_str() );
            EditorWidgets::drawInlineLabel( action._tag.c_str() ); // 태그 이름은 데이터 이름이다
            const bool bToggled = ImGui::Checkbox( "##tag", &bActive );
            ImGui::PopID();
            if ( bToggled )
                postBodyPreview( { PreviewAction::ActionTag, action._tag, static_cast<uint8>( bActive ? SW_TRUE : SW_FALSE ) } );
            bAnyActive = bAnyActive || bActive;
        }
        EditorSelfTestMarks::record( "charactereditor.body.actionTest" );
        _bodyStepTimer -= ImGui::GetIO().DeltaTime;
        if ( bAnyActive && _bodyStepTimer <= 0.0f )
        {
            postBodyPreview( { PreviewAction::Step, {}, SW_FALSE } );
            _bodyStepTimer = BodyCustomizationViewModel::kPreviewStepSeconds;
        }
        if ( ImGui::BeginTable( "##BodyDynamicsValues", 2, ImGuiTableFlags_SizingStretchProp ) )
        {
            const auto drawValue = []( const utf8* pLabel, float32 value )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( pLabel );
                ImGui::TableNextColumn();
                ImGui::Text( "%.3f", value );
            };
            drawValue( SW_LOCKEY( "EdPlugin", "FootLift" ), state._footLift );
            drawValue( SW_LOCKEY( "EdPlugin", "PupilSize" ), state._pupilSize );
            drawValue( SW_LOCKEY( "EdPlugin", "EyeLight" ), state._eyeLuminance );
            drawValue( SW_LOCKEY( "EdPlugin", "JiggleDisplacement" ), state._jiggleDisplacement );
            drawValue( SW_LOCKEY( "EdPlugin", "FluidWetness" ), state._fluidWetness );
            drawValue( SW_LOCKEY( "EdPlugin", "SmearDilution" ), state._smearDilution );
            ImGui::EndTable();
        }
    }

    void CharacterEditorPanel::drawAppearanceDataRow()
    {
        const AppearanceDatabase& database = _document.getDatabase();
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        (void)EditorWidgets::drawAssetPathField( "##ItemsPath", paths._itemsPath, "appearance items", nullptr, nullptr );
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        EditorWidgets::inputText( SW_WIDGET_ID( "##AppearanceFolder" ), paths._appearanceFolder.data(), paths._appearanceFolder.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "LoadData" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "LoadData" ) ) )
            _document.loadAppearanceData();
        EditorSelfTestMarks::record( "charactereditor.loadData" );
        if ( _document.isDatabaseLoaded() )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "SlotsSetsPresets", TextArgumentList().addInteger( "slotsCount", static_cast<int64>( database.getSlotCatalog().getSlots().size() ) ).addInteger( "setsCount", static_cast<int64>( database.getSets().getSets().size() ) ).addInteger( "presetsCount", static_cast<int64>( database.getPresets().getPresets().size() ) ) ).c_str() );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "LoadTheItemsFileAndTheAppearanceFolder" ) );
    }

    void CharacterEditorPanel::drawSelectionHeader( const AppearanceSelection& selection )
    {
        const AppearanceDatabase&               database   = _document.getDatabase();
        AppearanceEditModel&                    appearance = _document.getAppearanceModel();
        fixed_string<constant::buffer::kMax128> text;
        if ( _document.isDatabaseLoaded() )
        {
            vector<hashed_string> listPreset;
            for ( const CharacterAppearanceDef& preset : database.getPresets().getPresets() )
            {
                listPreset.push_back( preset._id );
            }
            hashed_string pickedPreset;
            const bool    bPresetPicked = CharacterEditorPanelInternal::drawNameCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "BasePreset" ), "Base preset" ), selection._basePresetID, listPreset, pickedPreset, "charactereditor.basePreset.first" );
            EditorSelfTestMarks::record( "charactereditor.basePreset" );
            if ( bPresetPicked )
                (void)appearance.setBasePreset( pickedPreset ); // 같은 값이면 false 이고 그대로 둔다
        }
        else
        {
            text = selection._basePresetID.c_str();
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BasePreset" ) );
            if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Basepreset" ), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                (void)appearance.setBasePreset( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        }
        text = selection._bodyType.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BodyType" ) );
        if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Bodytype" ), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyType( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._bodyShape.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BodyShape" ) );
        if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Bodyshape" ), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyShape( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._face.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Face" ) );
        if ( EditorWidgets::inputText( SW_WIDGET_ID( "##Face" ), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setFace( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
    }

    void CharacterEditorPanel::drawSlotRows( const AppearanceSelection& selection )
    {
        const AppearanceDatabase& database   = _document.getDatabase();
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "Slots" ) );
        if ( _document.isDatabaseLoaded() == false )
        {
            fixed_string<constant::buffer::kMax128> text;
            for ( const AppearanceSlotRequest& request : selection._listSlot )
            {
                ImGui::PushID( request._slot.c_str() );
                text = request._itemID.c_str();
                EditorWidgets::drawPropertyRowLabel( request._slot.c_str() ); // 이름표는 칸 앞에(칸에 주면 오른쪽에 붙는다)
                if ( EditorWidgets::inputText( SW_WIDGET_ID( "##item" ), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                    (void)appearance.setSlotItem( request._slot, CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
                ImGui::PopID();
            }
            EditorWidgets::inputText( SW_WIDGET_ID( "##NewSlot" ), _newName.data(), _newName.max_size() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "AddSlot" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddSlot" ) ) )
                (void)appearance.setSlotItem( CharacterEditorPanelInternal::makeName( _newName.c_str() ), hashed_string{} ); // 이미 있으면 false 이고 그대로 둔다
            return;
        }

        // 세트 고르기는 칸 목록 위에 둔다 — 패널이 낮아도 보이게.
        vector<hashed_string> listSet;
        for ( const EquipmentSetDef& set : database.getSets().getSets() )
        {
            listSet.push_back( set._id );
        }
        hashed_string pickedSet;
        const bool    bSetPicked = CharacterEditorPanelInternal::drawNameCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "EquipmentSet" ), "Equip set" ), hashed_string{}, listSet, pickedSet, "charactereditor.equipSet.first" );
        EditorSelfTestMarks::record( "charactereditor.equipSet" );
        if ( bSetPicked && pickedSet.empty() == false )
            (void)appearance.equipSet( database, pickedSet ); // 이미 입고 있으면 false 이고 그대로 둔다

        // 데이터가 있으면 슬롯 표의 칸마다 그 칸에 낄 수 있는 아이템만 고르게 한다.
        vector<hashed_string> listItem;
        bool                  bFirstSlot = true;
        for ( const AppearanceSlotDef& def : database.getSlotCatalog().getSlots() )
        {
            AppearanceEditModel::collectSlotItems( database, def._name, listItem );
            hashed_string picked;
            ImGui::PushID( def._name.c_str() );
            if ( CharacterEditorPanelInternal::drawNameCombo( def._name.c_str(), appearance.findSlotItem( def._name ), listItem, picked ) )
                (void)appearance.setSlotItem( def._name, picked );
            if ( bFirstSlot )
            {
                EditorSelfTestMarks::record( "charactereditor.slot.first" );
                bFirstSlot = false;
            }
            ImGui::PopID();
        }
    }

    void CharacterEditorPanel::drawCustomizationRows( const AppearanceSelection& selection )
    {
        AppearanceEditModel& appearance = _document.getAppearanceModel();
        ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "CustomizationValues" ) );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            float32 number = value._number._x;
            ImGui::PushID( value._parameter.c_str() );
            EditorWidgets::drawPropertyRowLabel( value._parameter.c_str() ); // 이름표는 칸 앞에(칸에 주면 오른쪽에 붙는다)
            if ( EditorWidgets::dragFloat( SW_WIDGET_ID( "##value" ), &number, CharacterEditorPanelInternal::kDragSpeed ) )
                (void)appearance.setNumber( value._parameter, number, true ); // 같은 값이면 false 이고 그대로 둔다
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                appearance.endContinuousEdit();
            ImGui::PopID();
        }
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SlidersColoursAndChoicesOfTheSchemaAre" ) );
    }

    void CharacterEditorPanel::drawCustomizationSliders( const AppearanceSelection& selection )
    {
        // 스키마는 선택의 것, 없으면 기준 프리셋(없으면 미리보기 몸 프리셋)의 것이다(`findCustomizationSchema`).
        AppearanceEditModel&          appearance = _document.getAppearanceModel();
        const CustomizationSchemaDef* pSchema    = _document.findCustomizationSchema( selection );
        if ( pSchema == nullptr )
        {
            _bCustomizationActive = SW_FALSE;
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "NoCustomizationSchemaForThisSelectionLoadThe" ) );
            return;
        }
        CustomizationValueSet normalized;
        (void)pSchema->normalize( selection._customization, normalized, nullptr ); // 옛 판 값도 올려 보인다. 더 새 판이면 기본값
        bool          bAnyActive   = false;
        bool          bFirstSlider = true;
        hashed_string category{};
        bool          bFirst = true;
        for ( const CustomizationParamDef& param : pSchema->_listParameter )
        {
            if ( param.isActive( *pSchema, normalized ) == false )
                continue;
            if ( bFirst || param._category != category )
            {
                category = param._category;
                bFirst   = false;
                ImGui::SeparatorText( category.empty() ? "General" : category.c_str() );
            }
            const CustomizationValue* pValue = normalized.findValue( param._name );
            ImGui::PushID( param._name.c_str() );
            switch ( param._kind )
            {
                case CustomizationKind::Slider:
                {
                    float32 number = pValue != nullptr ? pValue->_number._x : param._default;
                    EditorWidgets::drawPropertyRowLabel( param._name.c_str() ); // 이름표는 칸 앞에(칸에 주면 오른쪽에 붙는다)
                    if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##value" ), &number, param._min, param._max ) )
                    {
                        appearance.setNumberSymmetric( pSchema->_listParameter, param, number, true );
                    }
                    bAnyActive = bAnyActive || ImGui::IsItemActive();
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        appearance.endContinuousEdit();
                    if ( bFirstSlider )
                        EditorSelfTestMarks::record( "charactereditor.slider.first" ); // 첫 슬라이더 — 자동화가 누를 자리(마지막 것은 스키마가 길면 접힌 아래다)
                    bFirstSlider = false;
                    break;
                }
                case CustomizationKind::Color:
                {
                    const float4 current     = pValue != nullptr ? pValue->_number : param._defaultColor;
                    float32      arrColor[4] = { current._x, current._y, current._z, current._w };
                    EditorWidgets::drawPropertyRowLabel( param._name.c_str() );
                    if ( EditorWidgets::colorEdit4( SW_WIDGET_ID( "##value" ), arrColor ) )
                    {
                        const float4 edited{ arrColor[0], arrColor[1], arrColor[2], arrColor[3] };
                        appearance.setColorSymmetric( pSchema->_listParameter, param, edited, true );
                    }
                    bAnyActive = bAnyActive || ImGui::IsItemActive();
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        appearance.endContinuousEdit();
                    break;
                }
                case CustomizationKind::Choice:
                case CustomizationKind::Attachment:
                {
                    const hashed_string current = pValue != nullptr ? pValue->_option : param._defaultOption;
                    if ( EditorWidgets::beginCombo( param._name.c_str(), current.empty() ? "(none)" : current.c_str() ) )
                    {
                        for ( const CustomizationOptionDef& option : param._listOption )
                        {
                            if ( ImGui::Selectable( option._name.empty() ? "(none)" : option._name.c_str(), option._name == current ) )
                                (void)appearance.setOption( param._name, option._name ); // 같은 값이면 false 이고 그대로 둔다
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
            }
            ImGui::PopID();
        }
        _bCustomizationActive = bAnyActive;
    }

    void CharacterEditorPanel::drawExplanation()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "WhyDoesItLookLikeThisRules" ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        if ( _document.isDatabaseLoaded() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "LoadAppearanceDataToSeeWhichRulesHid" ) );
            return;
        }
        // 기준 프리셋이 칸 · 몸 종류의 시드라서, 없으면 펼칠 것이 없고 해석기가 칸마다 경고를 낸다 — 고르게 하고 넘어간다.
        if ( _document.hasBasePresetInData() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "PickABasePresetToSeeWhichRules" ) );
            return;
        }
        _document.refreshResolvedPreview(); // 선택 · 데이터가 바뀔 때만 다시 푼다
        const ResolvedAppearance&        resolved = _document.getResolvedPreview();
        const AppearanceSelectionReport& report   = _document.getResolvedReport();

        if ( report._bUnknownBasePreset == SW_TRUE )
            EditorThemeUtil::textWarning( "Base preset is not in the data" );
        for ( const hashed_string& dropped : report._listDroppedParameter )
        {
            EditorThemeUtil::textWarning( SW_LOCKEYFORMAT( "EdPluginFormat", "Dropped", TextArgumentList().addText( "dropped", dropped.c_str() ) ).c_str() );
        }
        for ( const hashed_string& clamped : report._listClampedParameter )
        {
            EditorThemeUtil::textWarning( SW_LOCKEYFORMAT( "EdPluginFormat", "Clamped", TextArgumentList().addText( "clamped", clamped.c_str() ) ).c_str() );
        }
        for ( const AppearanceSelectionReport::SlotFallback& fallback : report._listSlotFallback )
        {
            EditorThemeUtil::textWarning( SW_LOCKEYFORMAT( "EdPluginFormat", "Slot", TextArgumentList().addText( "slot", fallback._slot.c_str() ).addText( "requestedItem", fallback._requestedItem.c_str() ).addText( "fallbackItem", fallback._fallbackItem.c_str() ).addText( "reason", getEnumName( fallback._reason ) ) ).c_str() );
        }
        if ( ImGui::BeginChild( "##RuleTrace", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CharacterEditorPanelInternal::kExplanationListLines ) ), ImGuiChildFlags_Borders ) )
        {
            if ( resolved._listTrace.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "NoRuleFired" ) );
            for ( const AppearanceTraceEntry& entry : resolved._listTrace )
            {
                ImGui::TextWrapped( "[%s] %s: %s", getEnumName( entry._step ), entry._source.c_str(), entry._message.c_str() );
            }
        }
        ImGui::EndChild();
    }

    void CharacterEditorPanel::drawFitCheck()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "FitCheckViewOnly" ) ) == false )
            return;
        if ( _document.hasFit() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "NoFitResultYetThePreviewSceneSolves" ) );
            return;
        }
        const FitCheckSummary& fit = _document.getFit();
        for ( const FitPartSummary& part : fit._listPart )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "CutVertsMovedMax", TextArgumentList().addText( "name", part._name.c_str() ).addInteger( "cutTriangleCount", static_cast<int64>( part._cutTriangleCount ) ).addInteger( "triangleCount", static_cast<int64>( part._triangleCount ) ).addDecimal( "cutRatio", static_cast<float64>( part._cutRatio * 100.0f ), 0 ).addInteger( "movedVertexCount", static_cast<int64>( part._movedVertexCount ) ).addDecimal( "maxDelta", static_cast<float64>( part._maxDelta ), 3 ) ).c_str() );
        }
        if ( fit.hasPenetration() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "NoPenetration" ) );
        for ( const FitPenetrationSummary& penetration : fit._listPenetration )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "PenetrationPointsDepth", TextArgumentList().addText( "partA", penetration._partA.c_str() ).addText( "partB", penetration._partB.c_str() ).addInteger( "vertexCount", static_cast<int64>( penetration._vertexCount ) ).addDecimal( "maxDepth", static_cast<float64>( penetration._maxDepth ), 3 ) ).c_str() );
        }
        for ( const string& message : fit._listMessage )
        {
            ImGui::TextWrapped( "%s", message.c_str() );
        }
    }

    void CharacterEditorPanel::drawPresetRow()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "PresetsUserData" ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        CharacterEditorPaths& paths = _document.editPaths();
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "LoadPresets" ) ) )
            _document.loadPresets();
        EditorChrome::setNextItemWidthColumns( 24.0f );
        EditorWidgets::inputText( SW_WIDGET_ID( "##PresetName" ), paths._presetName.data(), paths._presetName.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "SaveAsPreset" ) ) );
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "SaveAsPreset" ) ) )
            _document.savePreset();
        EditorSelfTestMarks::record( "charactereditor.savePreset" );
        ImGui::EndDisabled();
        if ( _document.getAppearanceModel().isDirty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(unsaved)" );
        }
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "SaveThumbnail" ) ) )
            saveThumbnail();
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "charactereditor.saveThumbnail" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "StoresThePreviewTabSPictureIn" ) );

        if ( ImGui::BeginChild( "##PresetList", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CharacterEditorPanelInternal::kPresetListLines ) ), ImGuiChildFlags_Borders ) )
        {
            for ( const UserAppearancePresetRecord& preset : _document.getPresetStore().getPresets() )
            {
                if ( ImGui::Selectable( preset._name.c_str() ) )
                    _document.applyPreset( string_view( preset._name ) );
            }
        }
        ImGui::EndChild();
    }

    void CharacterEditorPanel::drawDataPresetRow()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "DataPresetSceneUse" ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "ASceneSCharacterAppearanceComponentNamesThis" ) );
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        EditorWidgets::inputText( SW_WIDGET_ID( "##ExportFolder" ), paths._exportFolder.data(), paths._exportFolder.max_size() );
        EditorSelfTestMarks::record( "charactereditor.exportFolder" );
        EditorChrome::setNextItemWidthColumns( 24.0f );
        EditorWidgets::inputText( SW_WIDGET_ID( "##ExportID" ), paths._exportID.data(), paths._exportID.max_size() );
        EditorSelfTestMarks::record( "charactereditor.exportID" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ExportDataPreset" ) ) );
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ExportDataPreset" ) ) && _document.exportDataPreset() )
            ++_published._exportCount;
        EditorSelfTestMarks::record( "charactereditor.exportPreset" );
        ImGui::EndDisabled();
    }

} // namespace sw::editor
