#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Animation/Component/PoseModifierComponent.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodySurfacePick.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr float32 kDragSpeed                = 0.005f;
            static constexpr float32 kRotationDragSpeed        = 0.5f;
            static constexpr int32   kLayerUp                  = 1;  ///< 피부 층 한 칸 위(목록 뒤가 위다)
            static constexpr int32   kLayerDown                = -1; ///< 피부 층 한 칸 아래
            static constexpr float32 kPathButtonsColumns       = 32.0f;
            static constexpr float32 kBoneTreeLines            = 10.0f;
            static constexpr float32 kPresetListLines          = 5.0f;
            static constexpr float32 kExplanationListLines     = 7.0f;
            static constexpr float32 kPreviewHeight            = 260.0f;  ///< 미리보기 뷰포트 높이(96 DPI 픽셀)
            static constexpr float32 kCustomizationReserve     = 150.0f;  ///< 꾸미기 머리글이 펼쳐졌을 때 뷰포트 아래에 남기는 높이(머리글 + 슬라이더 몇 줄, 96 DPI 픽셀)
            static constexpr float32 kPreviewFrameRadius       = 1.4f;    ///< 미리보기 카메라가 담는 반지름(m) — 사람 키 하나
            static constexpr float32 kSocketAxisLength         = 0.08f;   ///< 미리보기 소켓 축 길이(m)
            static constexpr float32 kSelectedSocketAxisLength = 0.18f;   ///< 고른 소켓의 축 길이(m)
            static constexpr float32 kPenetrationMarkLength    = 0.01f;   ///< 관통 점 십자의 한 팔 길이(m)
            static constexpr float32 kGizmoTolerance           = 1.0e-4f; ///< 놓은 월드 변환과 이만큼 다르면 기즈모가 옮긴 것이다(행렬 성분)
            static constexpr float32 kPickMarkLength           = 0.02f;   ///< 집은 자리 십자의 한 팔 길이(m)
            static constexpr uint32  kHighlightTriangleBudget  = 4000;    ///< 강조 구간 윤곽으로 긋는 삼각형 상한(넘으면 고르게 건너뛴다 — 큰 몸에서 프레임을 지키려고)
            static constexpr float32 kUVDragSpeed              = 0.002f;  ///< 체액 경로 점을 끄는 빠르기(UV)
            static constexpr uint32  kMaxWrinkleMask           = 7;       ///< 주름 마스크 채널의 끝 번호(0..7)
            static constexpr float32 kMaxGravityScale          = 2.0f;    ///< 지글 중력 배율 슬라이더 끝
            static constexpr float32 kMaxSwingDegrees          = 90.0f;   ///< 지글 흔들림 각 슬라이더 끝(도)
            static constexpr float32 kMaxImpulseScale          = 4.0f;    ///< 지글 충격 배율 슬라이더 끝
            static constexpr float32 kMaxWrinkleWeight         = 2.0f;    ///< 주름 세기 · 표정 무게 슬라이더 끝
            static constexpr float32 kMaxFluidValue            = 10.0f;   ///< 체액 흐름 값(초당 줄기 · 빠르기 · 반지름) 끌기 끝
            static constexpr float32 kFillWidth                = -1.0f;   ///< ImGui 다음 칸 폭 "남은 폭 모두"

            /** @brief 입력칸 글 버퍼입니다(별칭 · 묶음 — 그리기마다 데이터 값으로 채우고 놓을 때 쓴다). */
            using TextBuffer = fixed_string<constant::buffer::kMax128>;

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
                if ( ImGui::BeginCombo( pLabel, current.empty() ? "(none)" : current.c_str() ) )
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

            /** @brief 감시 값이 읽는 값 하나입니다 — 패널 인스턴스가 게시한 값 중 @p pField 를 @p outValue 에 씁니다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPublished( uint32 CharacterEditorPanel::Published::* pField, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished().*pField );
                return true;
            }

            /** @brief 열린 Character Editor 가 요청한 썸네일 캡처를 아직 거두지 않았으면 true 입니다(캡처 뒤 저장소 쓰기는 `LocalStoreRequests` 가 본다). */
            [[nodiscard]] static bool isThumbnailCapturePending()
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                return pPanel != nullptr && pPanel->isThumbnailCapturePending();
            }

            [[nodiscard]] static bool readBoneGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneGizmoPlaced, outValue );
            }
            [[nodiscard]] static bool readSocketGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_socketGizmoPlaced, outValue );
            }
            /** @brief 고른 소켓의 로컬 위치 x 입니다(실수라 `readPublished` 가 아니라 따로 읽는다). 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readSelectedSocketX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished()._selectedSocketX );
                return true;
            }
            [[nodiscard]] static bool readPenetrationMarkCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_penetrationMarkCount, outValue );
            }
            [[nodiscard]] static bool readBodyLayerCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyLayerCount, outValue );
            }
            [[nodiscard]] static bool readBodyHiddenSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyHiddenSlotCount, outValue );
            }
            [[nodiscard]] static bool readBodyDataDirty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_bodyDataDirty, outValue );
            }

            [[nodiscard]] static bool readBoneCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneCount, outValue );
            }
            [[nodiscard]] static bool readSocketCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_socketCount, outValue );
            }
            [[nodiscard]] static bool readStatusSet( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_statusSet, outValue );
            }
            [[nodiscard]] static bool readEquippedSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_equippedSlotCount, outValue );
            }
            [[nodiscard]] static bool readExportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_exportCount, outValue );
            }
            [[nodiscard]] static bool readPresetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_presetCount, outValue );
            }
            [[nodiscard]] static bool readPreviewAssembleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_previewAssembleCount, outValue );
            }
            [[nodiscard]] static bool readClipPlaying( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_clipPlaying, outValue );
            }
            /** @brief 미리보기 뷰포트의 오빗 거리입니다. 미리보기 시나리오가 휠 줌(`EditorWheel`)을 단언하는 데 쓴다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPreviewOrbitDistance( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager*   pManager = editor::findSubsystem<EditorPanelManager>();
                const CharacterEditorPanel* pPanel   = pManager != nullptr ? pManager->findPanel<CharacterEditorPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getOrbitDistance() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( CharacterEditorPanel, CharacterEditorPanel::kPanelID, EditorPanelCategory::Custom, 9020 );

    SW_AUTOMATION_FINISH_BLOCKER( characterEditorThumbnail, "CharacterEditorThumbnail", &CharacterEditorPanelInternal::isThumbnailCapturePending );

    SW_AUTOMATION_WATCH( editorCharacterEditorBodyLayerCount, "Editor.CharacterEditorBodyLayerCount", "Skin layers in the Character Editor appearance selection (published while the Body tab draws)",
                         &CharacterEditorPanelInternal::readBodyLayerCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBodyHiddenSlots, "Editor.CharacterEditorBodyHiddenSlots", "Slots the player hid in the Character Editor selection (published while the Body tab draws)",
                         &CharacterEditorPanelInternal::readBodyHiddenSlotCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBodyDataDirty, "Editor.CharacterEditorBodyDataDirty", "1 while the body data files (channel mapping, body profiles, sections) have unsaved edits",
                         &CharacterEditorPanelInternal::readBodyDataDirty );
    SW_AUTOMATION_WATCH( editorCharacterEditorBoneCount, "Editor.CharacterEditorBoneCount", "Bones of the skeleton the Character Editor has loaded", &CharacterEditorPanelInternal::readBoneCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorStatusSet, "Editor.CharacterEditorStatusSet", "1 while the Character Editor status line holds a message (an edit error or progress)", &CharacterEditorPanelInternal::readStatusSet );
    SW_AUTOMATION_WATCH( editorCharacterEditorSocketCount, "Editor.CharacterEditorSocketCount", "Sockets in the socket file the Character Editor has loaded", &CharacterEditorPanelInternal::readSocketCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorEquippedSlotCount, "Editor.CharacterEditorEquippedSlotCount", "Slots that hold an item in the appearance selection",
                         &CharacterEditorPanelInternal::readEquippedSlotCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorExportCount, "Editor.CharacterEditorExportCount", "Data presets the Character Editor exported in this run",
                         &CharacterEditorPanelInternal::readExportCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPresetCount, "Editor.CharacterEditorPresetCount", "Presets in the Character Editor's preset store", &CharacterEditorPanelInternal::readPresetCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewAssembleCount, "Editor.CharacterEditorPreviewAssembleCount",
                         "Times the Character Editor preview body was assembled (published while the Preview tab draws)", &CharacterEditorPanelInternal::readPreviewAssembleCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPenetrationMarks, "Editor.CharacterEditorPenetrationMarks", "Penetration points the Character Editor preview drew last frame (red crosses on the body)",
                         &CharacterEditorPanelInternal::readPenetrationMarkCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBoneGizmoPlaced, "Editor.CharacterEditorBoneGizmoPlaced", "1 while the Character Editor preview holds a gizmo marker on the selected bone",
                         &CharacterEditorPanelInternal::readBoneGizmoPlaced );
    SW_AUTOMATION_WATCH( editorCharacterEditorSocketGizmoPlaced, "Editor.CharacterEditorSocketGizmoPlaced", "1 while the Character Editor preview holds the gizmo on the selected socket",
                         &CharacterEditorPanelInternal::readSocketGizmoPlaced );
    SW_AUTOMATION_WATCH( editorCharacterEditorSelectedSocketX, "Editor.CharacterEditorSelectedSocketX", "Local translation x (m) of the socket selected in the Character Editor",
                         &CharacterEditorPanelInternal::readSelectedSocketX );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewClipPlaying, "Editor.CharacterEditorPreviewClipPlaying", "1 while the Character Editor preview body plays the selected clip (published while the Preview tab draws)",
                         &CharacterEditorPanelInternal::readClipPlaying );
    SW_AUTOMATION_WATCH( editorPreviewViewportOrbitDistance, "Editor.PreviewViewportOrbitDistance",
                         "Orbit distance of the Character Editor preview viewport camera (wheel zoom shrinks it)", &CharacterEditorPanelInternal::readPreviewOrbitDistance );
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorPanel::CharacterEditorPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, true }
        , _document{}
        , _newName{}
        , _renameBuffer{}
        , _bClipLoop{ true }
        , _previewBody{}
        , _previewBodyRevision{ 0 }
        , _previewDatabaseRevision{ 0 }
        , _bPreviewBodyDressed{ SW_FALSE }
        , _bModelsWereDirty{ SW_FALSE }
        , _bCustomizationOpen{ SW_TRUE }
        , _bCustomizationActive{ SW_FALSE }
        , _bGizmoPlaced{ SW_FALSE }
        , _bBodyPicking{ SW_FALSE }
        , _bBodyPickDrag{ SW_FALSE }
        , _bBodyPickShown{ SW_FALSE }
        , _seenFitRevision{ 0 }
        , _gizmo{}
        , _gizmoProxy{}
        , _placedGizmoWorld{}
        , _placedGizmoTarget{ CharacterGizmoTarget::None }
        , _bRigPreview{ false }
        , _published{}
        , _bodyState{}
        , _bodyHighlightDrive{ constant::invalid_index::kUint32 }
        , _bodySelectedSection{ constant::invalid_index::kUint32 }
        , _bodyPickLayer{ constant::invalid_index::kUint32 }
        , _bodyPickMarker{}
        , _bodyStepTimer{ 0.0f }
        , _thumbnailCapturePath{}
        , _thumbnailCapturePreset{}
        , _thumbnailCaptureSerial{ 0 }
    {
    }

    void CharacterEditorPanel::drawDocumentContent()
    {
        using PackDataState = CharacterEditorModel::PackDataState;
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
            const string hint = "This game has no character editor data (" + _document.getPackDataPath() + ")";
            EditorWidgets::drawEmptyHint( hint.c_str() );
            return;
        }
        if ( _document.getPackDataState() == PackDataState::Invalid )
        {
            EditorWidgets::drawEmptyHint( _document.getPackDataMessage().c_str() );
            return;
        }
        _document.ensureKindTable();
        _document.pollExternalChange( ImGui::GetTime() );
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

    bool CharacterEditorPanel::saveDocument()
    {
        if ( _document.saveFiles( ImGui::GetTime() ) == false )
            return false; // 까닭은 상태 줄에 남았다
        acknowledgePreviewEdits();
        return true;
    }

    void CharacterEditorPanel::syncDocumentDirty()
    {
        // 소켓 · 포즈 편집은 모델 안의 기록이고, 미리보기 씬 편집(인스펙터로 몸을 고친 것)은 창 스택이다 — 둘 다 문서의 미저장이다.
        // 모델이 되돌리기로 저장 상태에 돌아오면 dirty 를 내리고, 그 뒤 기반 클래스가 창 스택의 새 편집을 다시 올린다.
        const bool bModelsDirty = _document.isDirty();
        if ( bModelsDirty )
            markDocumentDirty();
        else if ( _bModelsWereDirty )
            clearDocumentDirty();
        _bModelsWereDirty = bModelsDirty;
        EditorPreviewPanel::syncDocumentDirty();
    }

    bool CharacterEditorPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, SocketSet::kExtension );
    }

    ToolAssetLoadResult CharacterEditorPanel::loadDocument()
    {
        // 콘텐츠 브라우저에서 고른 소켓 파일이 있으면 그것을, 없으면 경로 칸(팩 데이터의 소켓 파일)을 연다.
        const string_view focused = getMatchingFocusedPath();
        if ( focused.empty() == false )
        {
            acceptFocusedDocument();
            _document.editPaths()._socketPath = focused;
        }
        const CharacterEditorPaths& paths = _document.getPaths();
        if ( paths._socketPath.empty() )
            return ToolAssetLoadResult::Missing;
        if ( editor::getSubsystem<AssetPaths>().findLooseFile( string_view( paths._socketPath.c_str() ) ).has_value() == false )
            return ToolAssetLoadResult::Missing; // 새 소켓 파일 — 저장하면 만든다
        return _document.loadSocketFile() ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    ScenePreviewPolicy CharacterEditorPanel::getPreviewPolicy() const
    {
        ScenePreviewPolicy policy = EditorPreviewPanel::getPreviewPolicy();
        policy._bPlay             = true; // 몸의 애니메이터 · 외형 조립은 게임과 같이 onBeginPlay 와 틱으로 돈다(언리얼 FPreviewScene)
        return policy;
    }

    void CharacterEditorPanel::populatePreviewScene( Scene& scene )
    {
        (void)scene; // 몸은 외형 데이터를 읽은 뒤에 세운다(`syncPreviewBody`)
        _previewBody         = GameObjectHandle{};
        _bPreviewBodyDressed = SW_FALSE;
    }

    void CharacterEditorPanel::drawPreviewTab()
    {
        if ( _document.isDatabaseLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "LoadTheAppearanceDataAppearanceTabToDress" ) );
            return;
        }
        syncPreviewBody();
        const GameObjectManager*            pManager     = findPreviewManager();
        GameObject*                         pMutableBody = pManager != nullptr ? findPreviewManager()->resolveGameObject( _previewBody ) : nullptr;
        const GameObject*                   pBody        = pMutableBody;
        const CharacterAppearanceComponent* pAppearance  = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
        SkeletalAnimatorComponent*          pAnimator    = pMutableBody != nullptr ? pMutableBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator != nullptr )
            drawAnimationControls( *pAnimator );
        if ( _document.getPaths()._rigPath.empty() == false )
        {
            ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "PreviewConstraintRig" ), &_bRigPreview );
            EditorSelfTestMarks::record( "charactereditor.rigPreview" );
            if ( pMutableBody != nullptr )
                syncRigPreview( *pMutableBody );
        }
        const AppearanceSelection& selection = _document.getAppearanceModel().getSelection();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "WearingOnBasePreset", TextArgumentList().addText( "wearing", selection._listSlot.empty() ? "none" : "selection" ).addText( "basePresetID", selection._basePresetID.empty() ? _document.getPreviewPresetID().c_str() : selection._basePresetID.c_str() ).addInteger( "appearance", static_cast<int64>( pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u ) ) ).c_str() );
        _published._previewAssembleCount = pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u;
        const float2  available          = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight          = CharacterEditorPanelInternal::kPreviewHeight * EditorThemeUtil::getDPIScale();
        // 뷰포트가 남은 높이를 모두 차지하면 슬라이더가 늘 접힌 아래로 밀려 스크롤 없이는 닿지 못한다 — 펼쳐졌을 때는 아래 몇 줄 자리를 남긴다.
        const float32 reserve        = _bCustomizationOpen ? CharacterEditorPanelInternal::kCustomizationReserve * EditorThemeUtil::getDPIScale() : 0.0f;
        const float32 viewportHeight = available._y - reserve;
        syncPreviewGizmo();
        drawPreviewViewport( float2{ available._x, viewportHeight > minHeight ? viewportHeight : minHeight }, _gizmo.hasMouse() );
        _gizmo.draw( *this );
        recordGizmoMark();
        EditorSelfTestMarks::record( "charactereditor.previewScene" );
        drawBodyPickOverlay( pMutableBody );
        if ( pAppearance != nullptr )
            drawSocketAxes( *pAppearance );
        if ( pBody != nullptr )
            drawPenetrationPoints( *pBody );
        // 미리보기 조립이 푼 피팅 결과를 확인 표시에 올린다(세대가 바뀔 때만).
        const bool bNewFit = pAppearance != nullptr && pAppearance->getLastFitResult() != nullptr && pAppearance->getFitResultRevision() != _seenFitRevision;
        if ( bNewFit )
        {
            _document.setFitResult( *pAppearance->getLastFitResult() );
            _seenFitRevision = pAppearance->getFitResultRevision();
        }
        // 슬라이더는 뷰포트 아래에 둔다 — 모습이 바뀌는 것을 보며 끈다.
        _bCustomizationOpen = ImGui::CollapsingHeader( SW_LOCKEY( "EdPlugin", "CustomizeReleaseASliderToRefit" ), ImGuiTreeNodeFlags_DefaultOpen );
        if ( _bCustomizationOpen )
            drawCustomizationSliders( selection );
        else
            _bCustomizationActive = SW_FALSE;
    }

    void CharacterEditorPanel::syncPreviewGizmo()
    {
        GameObjectManager*         pManager   = findPreviewManager();
        GameObject*                pBody      = pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
        GameObject*                pProxy     = pManager != nullptr ? pManager->resolveGameObject( _gizmoProxy ) : nullptr;
        const SceneComponent*      pBodyScene = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
        const CharacterGizmoTarget target     = _document.getGizmoTarget();
        float4x4                   desiredWorld{};
        float4x4                   parentWorld{};
        bool                       bFrame = false;
        if ( pBodyScene != nullptr && target == CharacterGizmoTarget::Bone )
            bFrame = findBoneGizmoFrame( pBodyScene->getWorldMatrix(), desiredWorld, parentWorld );
        else if ( pBodyScene != nullptr && target == CharacterGizmoTarget::Socket )
            bFrame = findSocketGizmoFrame( *pBody, desiredWorld, parentWorld );
        if ( bFrame == false )
        {
            if ( pProxy != nullptr )
                pManager->destroyGameObject( pProxy );
            _gizmoProxy                   = GameObjectHandle{};
            _bGizmoPlaced                 = SW_FALSE;
            _published._boneGizmoPlaced   = 0;
            _published._socketGizmoPlaced = 0;
            return;
        }
        if ( pProxy == nullptr )
        {
            pProxy = pManager->createGameObject( hashed_string( "GizmoProxy" ) );
            if ( pProxy == nullptr || pProxy->addComponent<SceneComponent>() == nullptr )
            {
                _document.setStatus( "The gizmo target could not be created" );
                return;
            }
            _gizmoProxy   = pProxy->getHandle();
            _bGizmoPlaced = SW_FALSE;
        }
        SceneComponent* pScene = pProxy->getPrimarySceneComponent();
        if ( pScene == nullptr )
            return;
        if ( getPreviewSelection().getPrimaryObject() != pProxy )
            getPreviewSelection().selectObject( pProxy );
        // 주인이 바뀌었으면 지난 자리와 견주지 않는다 — 다른 본 · 소켓의 자리를 "끌었다" 로 읽어 옮기면 안 된다.
        if ( target != _placedGizmoTarget )
        {
            _bGizmoPlaced = SW_FALSE;
            _document.getPoseModel().endContinuousEdit();
            _document.getSocketModel().endContinuousEdit();
        }
        // 지난 그리기에 놓은 자리와 달라졌으면 기즈모가 옮긴 것이다 — 주인의 데이터(포즈 덮어쓰기 · 소켓 로컬 변환)로 되돌린다.
        bool           bMoved       = false;
        const float4x4 currentWorld = pScene->getWorldMatrix();
        if ( _bGizmoPlaced )
        {
            for ( uint32 element = 0; element < 16 && bMoved == false; ++element )
            {
                bMoved = MathUtil::abs( currentWorld.data()[element] - _placedGizmoWorld.data()[element] ) > CharacterEditorPanelInternal::kGizmoTolerance;
            }
        }
        if ( bMoved )
        {
            applyGizmoWorld( target, currentWorld, parentWorld );
        }
        else if ( _bGizmoPlaced && _gizmo.hasMouse() == false )
        {
            // 끌기가 끝났다 — 다음 편집은 새 되돌리기 단계
            _document.getPoseModel().endContinuousEdit();
            _document.getSocketModel().endContinuousEdit();
        }
        if ( bMoved == false )
        {
            pScene->setWorldTransform( desiredWorld );
            _placedGizmoWorld = pScene->getWorldMatrix();
        }
        else
        {
            _placedGizmoWorld = currentWorld; // 다음 그리기는 모델 값으로 다시 놓는다(위 `bMoved == false` 경로가 desired 로 맞춘다)
        }
        _bGizmoPlaced                 = SW_TRUE;
        _placedGizmoTarget            = target;
        _published._boneGizmoPlaced   = target == CharacterGizmoTarget::Bone ? 1u : 0u;
        _published._socketGizmoPlaced = target == CharacterGizmoTarget::Socket ? 1u : 0u;
    }

    bool CharacterEditorPanel::findBoneGizmoFrame( const float4x4& bodyWorld, float4x4& outWorld, float4x4& outParentWorld ) const
    {
        const BonePoseEditModel& pose      = _document.getPoseModel();
        const Skeleton&          skeleton  = pose.getSkeleton();
        const optional<uint32>   boneIndex = skeleton.findBoneIndex( _document.getSelectedBone() );
        if ( boneIndex.has_value() == false )
            return false; // 고른 본이 없다(고르지 않았거나 다른 스켈레톤) — 기즈모를 그리지 않는다
        // 덮어쓴 값을 얹은 모델 변환 — 부모까지 줄을 올라가 루트부터 곱한다(행벡터: 자식 × 부모).
        vector<int32> listChain;
        for ( int32 index = static_cast<int32>( *boneIndex ); index >= 0; index = skeleton.getBone( static_cast<uint32>( index ) )._parentIndex )
        {
            listChain.push_back( index );
        }
        float4x4 parentModel = float4x4::Identity;
        float4x4 boneModel   = float4x4::Identity;
        for ( size_t chainIndex = listChain.size(); chainIndex > 0; --chainIndex )
        {
            BoneTransform local{};
            if ( pose.getEffectiveTransform( skeleton.getBone( static_cast<uint32>( listChain[chainIndex - 1] ) )._name, local ) == false )
                return false;
            parentModel = boneModel;
            boneModel   = local.toMatrix() * boneModel;
        }
        outWorld       = boneModel * bodyWorld;
        outParentWorld = parentModel * bodyWorld;
        return true;
    }

    bool CharacterEditorPanel::findSocketGizmoFrame( const GameObject& body, float4x4& outWorld, float4x4& outParentWorld ) const
    {
        // 편집 중(저장 전) 소켓 정의를 몸의 지금 포즈에 얹는다 — 소켓 축(`drawSocketAxes`)과 같은 뼈 배열이다. 부모 해석은 런타임의 것(`SocketSet::computeParentTransform`)이다.
        const SkeletalMeshComponent* pUnit      = body.getComponent<SkeletalMeshComponent>();
        const SceneComponent*        pBodyScene = body.getPrimarySceneComponent();
        const SocketSet&             sockets    = _document.getSocketModel().getSockets();
        const SocketDef*             pSocket    = sockets.findSocket( _document.getSelectedSocket() );
        if ( pUnit == nullptr || pBodyScene == nullptr || pSocket == nullptr )
            return false;
        CharacterBoneArray poseBones;
        CharacterPoseUtil::makeBindBones( pUnit->getSkeleton(), poseBones );
        (void)CharacterPoseUtil::copyUnitPose( *pUnit, poseBones ); // 포즈가 아직 없으면 바인드 포즈 그대로 둔다(정상)
        float4x4 parentUnit{};
        if ( sockets.computeParentTransform( *pSocket, poseBones, parentUnit ) == false )
            return false; // 모르는 부모 본 — 소켓 축이 상태 줄에 이유를 남긴다
        outParentWorld = parentUnit * pBodyScene->getWorldMatrix();
        outWorld       = pSocket->makeLocalTransform() * outParentWorld;
        return true;
    }

    void CharacterEditorPanel::applyGizmoWorld( CharacterGizmoTarget target, const float4x4& world, const float4x4& parentWorld )
    {
        switch ( target )
        {
            case CharacterGizmoTarget::None:
                break;
            case CharacterGizmoTarget::Bone:
            {
                // 부모 월드의 역으로 본의 로컬을 구해 레퍼런스 포즈 덮어쓰기에 넣는다(끄는 동안은 한 되돌리기 단계).
                BonePoseEditModel&   pose    = _document.getPoseModel();
                const BoneTransform  local   = BoneTransform::makeFromMatrix( world * parentWorld.invert() );
                const float3         degrees = local._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
                const expected<bool> moved   = pose.setTranslation( _document.getSelectedBone(), local._translation, true );
                const expected<bool> turned  = pose.setRotationDegrees( _document.getSelectedBone(), degrees, true );
                _document.reportEdit( moved.has_value() ? turned : moved ); // 같은 값이면 false 다 — 오류만 상태 줄에 남는다
                break;
            }
            case CharacterGizmoTarget::Socket:
            {
                // 월드 두 행렬에 몸의 월드가 함께 곱혀 있어 나누면 유닛 공간과 같은 로컬이다 — 소켓 파일의 부모 기준 변환으로 쓴다.
                _document.reportEdit( _document.getSocketModel().setUnitTransform( _document.getSelectedSocket(), world, parentWorld, true ) );
                break;
            }
        }
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

    void CharacterEditorPanel::drawPenetrationPoints( const GameObject& body )
    {
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        const SceneComponent*        pScene   = body.getPrimarySceneComponent();
        const FitCheckSummary&       fit      = _document.getFit();
        const bool                   bNothing = _document.hasFit() == false || fit.hasPenetration() == false || pScene == nullptr;
        const bool                   bNoImage = size._x <= 0.0f || size._y <= 0.0f;
        if ( bNothing || bNoImage )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        EditorProjectedLineRenderer renderer{ ImGui::GetWindowDrawList(), viewProjection, position, size };
        // 피팅은 바인드 공간에서 풀므로 몸 오브젝트의 월드 변환으로 옮긴다. 십자 표시 하나가 정점 하나다(앞쪽 일부만 들고 있다).
        const float4x4 world = pScene->getWorldMatrix();
        const float4   color{ 1.0f, 0.2f, 0.2f, 1.0f };
        const float32  arm = CharacterEditorPanelInternal::kPenetrationMarkLength;
        for ( const FitPenetrationSummary& penetration : fit._listPenetration )
        {
            for ( const float3& point : penetration._listPoint )
            {
                const float3 center = float3::transform( point, world );
                renderer.drawLine( center - float3{ arm, 0.0f, 0.0f }, center + float3{ arm, 0.0f, 0.0f }, color );
                renderer.drawLine( center - float3{ 0.0f, arm, 0.0f }, center + float3{ 0.0f, arm, 0.0f }, color );
                renderer.drawLine( center - float3{ 0.0f, 0.0f, arm }, center + float3{ 0.0f, 0.0f, arm }, color );
            }
        }
        _published._penetrationMarkCount = renderer.getDrawnSegmentCount() / 3;
    }

    void CharacterEditorPanel::syncRigPreview( GameObject& body )
    {
        PoseModifierComponent* pRig = body.getComponent<PoseModifierComponent>();
        if ( _bRigPreview == false )
        {
            if ( pRig != nullptr && pRig->isRigEnabled() )
                pRig->setRigEnabled( false );
            return;
        }
        if ( pRig == nullptr )
        {
            pRig = body.addComponent<PoseModifierComponent>();
            if ( pRig == nullptr )
            {
                _document.setStatus( "The preview body could not take the constraint rig" );
                _bRigPreview = false;
                return;
            }
        }
        const string_view rigPath( _document.getPaths()._rigPath.c_str() );
        if ( string_view( pRig->getRigPath() ) != rigPath )
            pRig->setRigPath( rigPath );
        if ( pRig->isRigEnabled() == false )
            pRig->setRigEnabled( true );
    }

    void CharacterEditorPanel::drawSocketAxes( const CharacterAppearanceComponent& appearance )
    {
        using Internal                        = CharacterEditorPanelInternal;
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        if ( size._x <= 0.0f || size._y <= 0.0f )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        EditorProjectedLineRenderer renderer{ ImGui::GetWindowDrawList(), viewProjection, position, size };
        // 편집 중(저장 전) 소켓 정의를 몸의 지금 포즈에 얹어 자리를 구한다 — 저장하지 않아도 끄는 대로 축이 따라온다. 고른 소켓은 길고 밝게 긋는다.
        const GameObject*            pBodyObject = appearance.getOwner();
        const SkeletalMeshComponent* pUnit       = pBodyObject != nullptr ? pBodyObject->getComponent<SkeletalMeshComponent>() : nullptr;
        const SceneComponent*        pBodyScene  = pBodyObject != nullptr ? pBodyObject->getPrimarySceneComponent() : nullptr;
        if ( pUnit == nullptr || pBodyScene == nullptr )
            return;
        CharacterBoneArray bindBones;
        CharacterPoseUtil::makeBindBones( pUnit->getSkeleton(), bindBones );
        CharacterBoneArray poseBones = bindBones;
        (void)CharacterPoseUtil::copyUnitPose( *pUnit, poseBones ); // 포즈가 아직 없으면 바인드 포즈 그대로 둔다(정상)
        ResolvedSocketTable table;
        string              resolveError;
        table.beginResolve();
        const SocketSet& sockets   = _document.getSocketModel().getSockets();
        const bool       bResolved = table.addUnit( hashed_string{}, 0, sockets, bindBones, nullptr, &resolveError );
        table.endResolve();
        if ( bResolved == false )
        {
            _document.setStatus( resolveError ); // 모르는 부모 본 같은 편집 오류 — 축을 그리지 않고 알린다
            _socketResolveError = resolveError;
            return;
        }
        // 프레임마다 다시 푸는 오류라 풀리면 지운다 — 몸이 외형을 입기 전(뼈 하나짜리 스켈레톤) 프레임의 오류가 상태 줄에 남아 있었다. 다른 일이 남긴 글은 건드리지 않는다.
        if ( _socketResolveError.empty() == false && _document.getStatus() == _socketResolveError )
            _document.setStatus( string_view{} );
        _socketResolveError.clear();
        const float4x4 bodyWorld       = pBodyScene->getWorldMatrix();
        const float4   arrAxisColor[3] = { SW_THEME_COLOR( "Viewport", "AxisX" ).toFloat4(), SW_THEME_COLOR( "Viewport", "AxisY" ).toFloat4(), SW_THEME_COLOR( "Viewport", "AxisZ" ).toFloat4() };
        const float3   arrAxis[3]      = {
            float3{1.0f, 0.0f, 0.0f},
            float3{0.0f, 1.0f, 0.0f},
            float3{0.0f, 0.0f, 1.0f}
        };
        for ( const SocketDef& socket : sockets.getSockets() )
        {
            const SocketID socketID = table.findSocket( socket._name );
            float4x4       unitTransform{};
            if ( socketID == kInvalidSocketID || table.computeUnitTransform( socketID, poseBones, unitTransform ) == false )
                continue;
            const float4x4   world     = unitTransform * bodyWorld;
            const bool       bSelected = socket._name == _document.getSelectedSocket();
            const float32    length    = bSelected ? Internal::kSelectedSocketAxisLength : Internal::kSocketAxisLength;
            const float32    alpha     = bSelected ? 1.0f : 0.6f;
            const float3     origin    = world.getTranslation();
            const quaternion rotation  = world.getRotation();
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                const float4& color = arrAxisColor[axis];
                renderer.drawLine( origin, origin + float3::transform( arrAxis[axis], rotation ) * length, float4{ color._x, color._y, color._z, alpha } );
            }
        }
    }

    void CharacterEditorPanel::syncPreviewBody()
    {
        GameObjectManager* pManager = findPreviewManager();
        if ( pManager == nullptr )
            return;
        GameObject* pBody = pManager->resolveGameObject( _previewBody );
        if ( pBody == nullptr )
        {
            pBody = pManager->createGameObject( hashed_string( "PreviewCharacter" ) );
            if ( pBody == nullptr )
            {
                _document.setStatus( "The preview body could not be created" );
                return;
            }
            // 애니메이터는 클립 폴더만 가진다(그래프 없이 클립 하나를 재생). 포즈는 같은 오브젝트의 유닛이 외형 조립으로 받은 스켈레톤으로 낸다.
            // 유닛(`SkeletalMeshComponent`)을 먼저 둔다 — 애니메이터는 시작할 때 같은 오브젝트의 유닛에 붙는다.
            const bool                 bUnitAndLook = pBody->addComponent<SkeletalMeshComponent>() != nullptr && pBody->addComponent<CharacterAppearanceComponent>() != nullptr;
            SkeletalAnimatorComponent* pAnimator    = bUnitAndLook ? pBody->addComponent<SkeletalAnimatorComponent>() : nullptr;
            const bool                 bComponents  = bUnitAndLook && pAnimator != nullptr;
            if ( bComponents == false )
            {
                pManager->destroyGameObject( pBody );
                _document.setStatus( "The preview body could not take its components" );
                return;
            }
            pAnimator->setAnimationSetPath( string_view( _document.getPaths()._animationSet.c_str() ) );
            // 몸 동작(굽 · 지글 · 동공 · 체액)은 몸 프로필이 있는 외형에서만 일한다 — 없는 몸에서는 쉰다. 몸 탭의 미리보기 단추가 이것을 부른다.
            (void)pBody->addComponent<CharacterBodyDynamicsComponent>();
            _previewBody         = pBody->getHandle();
            _bPreviewBodyDressed = SW_FALSE;
        }
        CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
        const AppearanceEditModel&    appearance  = _document.getAppearanceModel();
        const AppearanceDatabase&     database    = _document.getDatabase();
        const bool                    bChanged    = _bPreviewBodyDressed == false || _previewBodyRevision != appearance.getRevision() || _previewDatabaseRevision != database.getRevision();
        // 슬라이더를 끄는 동안은 다시 입히지 않는다 — 피팅이 무거워 놓을 때 한 번 푼다(첫 입히기는 기다리지 않는다).
        const bool bDeferred = _bCustomizationActive && _bPreviewBodyDressed;
        const bool bSkip     = pAppearance == nullptr || bChanged == false;
        if ( bSkip || bDeferred )
            return;
        // 기준 프리셋이 없는 선택은 팩 데이터의 미리보기 몸 위에 입힌다(`makePreviewSelection`).
        pAppearance->previewSelection( _document.makePreviewSelection(), database );
        _previewBodyRevision     = appearance.getRevision();
        _previewDatabaseRevision = database.getRevision();
        _bPreviewBodyDressed     = SW_TRUE;
    }

    void CharacterEditorPanel::drawAnimationControls( SkeletalAnimatorComponent& animator )
    {
        const vector<hashed_string>& listClipName = _document.getClipNames();
        if ( listClipName.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "NoAnimationClipsIn", TextArgumentList().addText( "clipFolder", _document.getPaths()._animationSet.c_str() ) ).c_str() );
            return;
        }
        const hashed_string selectedClip = _document.getSelectedClip();
        EditorChrome::setNextItemWidthColumns( 32.0f );
        if ( ImGui::BeginCombo( "##PreviewClip", selectedClip.empty() ? "(select a clip)" : selectedClip.c_str() ) )
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
            if ( animator.preloadClip( clip ) && animator.play( clip, _bClipLoop, 0.0f ) )
                _document.clearStatus();
            else
                _document.setStatus( "The clip could not be played (see log)" );
        }
        EditorSelfTestMarks::record( "charactereditor.playClip" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Stop##PreviewClip" ) );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Stop" ), "Stop##PreviewClip" ) ) )
            animator.stop();
        const bool bPlaying     = animator.getCurrentStateName() == clip && clip.empty() == false && animator.isAnimationActive();
        _published._clipPlaying = bPlaying ? 1u : 0u;
    }

    GameObjectManager* CharacterEditorPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void CharacterEditorPanel::revertDocument()
    {
        _document.revert();
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
        ImGui::InputTextWithHint( "##NewSocketName", SW_LOCKEY( "EditorCharacterEditor", "NewSocketHint" ), _newName.data(), _newName.max_size() );
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
        ImGui::InputText( "##Rename", _renameBuffer.data(), _renameBuffer.max_size() );
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
        if ( ImGui::InputText( "##Parent", parent.data(), parent.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            _document.reportEdit( sockets.setParent( name, CharacterEditorPanelInternal::makeName( parent.c_str() ) ) );

        hashed_string pickedKind;
        if ( CharacterEditorPanelInternal::drawNameCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Kind" ), "Kind" ), socket._kind, sockets.getKindTable().getKinds(), pickedKind ) )
            _document.reportEdit( sockets.setKind( name, pickedKind ) );

        float3 translation = socket._translation;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Translation" ) );
        if ( ImGui::DragFloat3( "##Translation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setTranslation( name, translation, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 degrees = socket._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Rotation" ) );
        if ( ImGui::DragFloat3( "##Rotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( sockets.setRotationDegrees( name, degrees, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 scale = socket._scale;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Scale" ) );
        if ( ImGui::DragFloat3( "##Scale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setScale( name, scale, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        int32 anchorIndex = socket._anchor == SocketAnchor::Surface ? 1 : 0;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Anchor" ) );
        if ( ImGui::Combo( "##Anchor", &anchorIndex, "Bone\0Surface\0" ) )
            _document.reportEdit( sockets.setAnchor( name, anchorIndex == 1 ? SocketAnchor::Surface : SocketAnchor::Bone ) );

        fixed_string<constant::file::kMaxPathSize> previewMesh;
        previewMesh = string_view( socket._previewMesh );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "PreviewMesh" ) );
        if ( ImGui::InputText( "##Previewmesh", previewMesh.data(), previewMesh.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
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
        if ( ImGui::DragFloat3( "##Bonetranslation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( pose.setTranslation( bone, translation, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();
        EditorSelfTestMarks::record( "charactereditor.bone.translation" );

        float3 degrees = current._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BoneRotation" ) );
        if ( ImGui::DragFloat3( "##Bonerotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( pose.setRotationDegrees( bone, degrees, true ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();

        float3 scale = current._scale;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BoneScale" ) );
        if ( ImGui::DragFloat3( "##Bonescale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
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
        const AppearanceDatabase& database   = _document.getDatabase();
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        GameObjectManager*        pManager   = findPreviewManager();
        GameObject*               pBody      = pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
        BodyCustomizationViewModel::gather( appearance, database, pBody, _bodyHighlightDrive, _bodyState );
        const BodyCustomizationViewState& state = _bodyState;
        _document.openBodyData( state._bodyShapes.c_str() ); // 고친 파일은 다시 읽지 않는다
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
        drawBodyDynamicsPreview( state, pBody );
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
            (void)_document.saveBodyData(); // 실패 까닭은 상태 줄에 남는다
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
                if ( ImGui::DragFloat2( "##range", arrRange, Internal::kDragSpeed ) )
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
            if ( ImGui::Selectable( row._section.c_str(), index == _bodySelectedSection || row._bHighlighted == SW_TRUE, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap ) )
                _bodySelectedSection = index;
            ImGui::TableNextColumn();
            ImGui::BeginDisabled( visuals.isOpen() == false );
            Internal::TextBuffer alias{ row._alias.c_str() };
            ImGui::SetNextItemWidth( Internal::kFillWidth );
            ImGui::InputText( "##alias", alias.data(), alias.max_size() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                (void)BodyCustomizationViewModel::setSectionAlias( visuals, row, alias.c_str() );
            ImGui::TableNextColumn();
            Internal::TextBuffer groups{ row._groups.c_str() };
            ImGui::SetNextItemWidth( Internal::kFillWidth );
            ImGui::InputText( "##groups", groups.data(), groups.max_size() );
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
                float32     _value;
                float32     _max;
            };
            const JiggleField arrField[] = {
                {      "stiffness",       params._stiffness,                       1.0f},
                {        "damping",         params._damping,                       1.0f},
                {   "gravityScale",    params._gravityScale, Internal::kMaxGravityScale},
                {"maxSwingDegrees", params._maxSwingDegrees, Internal::kMaxSwingDegrees},
                {   "impulseScale",    params._impulseScale, Internal::kMaxImpulseScale}
            };
            for ( const JiggleField& field : arrField )
            {
                float32 value = field._value;
                ImGui::PushID( field._pAttribute );
                EditorWidgets::drawPropertyRowLabel( field._pAttribute ); // 이름은 데이터 칸 이름이다(파일과 같은 철자)
                const bool bChanged = ImGui::SliderFloat( "##value", &value, 0.0f, field._max );
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
        float32 intensity = state._wrinkleIntensity;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "WrinkleIntensity" ) );
        if ( ImGui::SliderFloat( "##wrinkleIntensity", &intensity, 0.0f, Internal::kMaxWrinkleWeight ) )
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
            if ( ImGui::SliderInt( "##mask", &mask, 0, static_cast<int32>( Internal::kMaxWrinkleMask ) ) )
                (void)BodyCustomizationViewModel::setWrinkleMask( profiles, state._bodyProfile, region._region, static_cast<uint32>( mask ) );
            for ( const WrinkleSourceDef& source : region._listSource )
            {
                float32 weight = source._weight;
                ImGui::PushID( source._expression.c_str() );
                EditorWidgets::drawPropertyRowLabel( source._expression.c_str() ); // 표정 이름은 데이터 이름이다
                const bool bWeightChanged = ImGui::SliderFloat( "##weight", &weight, 0.0f, Internal::kMaxWrinkleWeight );
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
                bChanged = ImGui::DragFloat2( "##point", &listPoint[point]._x, Internal::kUVDragSpeed, 0.0f, 1.0f ) || bChanged;
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
                float32     _value;
                float32     _speed;
            };
            const FluidField arrField[] = {
                {"ratePerSecond", row._ratePerSecond,   Internal::kDragSpeed},
                {        "speed",         row._speed, Internal::kUVDragSpeed},
                {       "radius",        row._radius, Internal::kUVDragSpeed}
            };
            for ( const FluidField& field : arrField )
            {
                float32 value = field._value;
                ImGui::PushID( field._pAttribute );
                EditorWidgets::drawPropertyRowLabel( field._pAttribute ); // 데이터 칸 이름
                const bool bValueChanged = ImGui::DragFloat( "##value", &value, field._speed, 0.0f, Internal::kMaxFluidValue );
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

    void CharacterEditorPanel::drawBodyPickOverlay( GameObject* pBody )
    {
        using Internal                        = CharacterEditorPanelInternal;
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        SkeletalMeshComponent*       pUnit    = pBody != nullptr ? pBody->getComponent<SkeletalMeshComponent>() : nullptr;
        const SceneComponent*        pScene   = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
        const shared_ptr<Mesh>       mesh     = pUnit != nullptr ? pUnit->getMesh() : shared_ptr<Mesh>{};
        if ( size._x <= 0.0f || size._y <= 0.0f || mesh == nullptr || pScene == nullptr )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        const float4x4              bodyWorld = pScene->getWorldMatrix();
        EditorProjectedLineRenderer renderer{ ImGui::GetWindowDrawList(), viewProjection, position, size };

        // 드라이브가 고른 구간을 윤곽으로 긋는다(바인드 자세 — 집기와 같다). 큰 몸은 삼각형을 고르게 건너뛴다.
        const vector<MeshSection>& listSection = mesh->getSections();
        const vector<RHIVertex>&   listVertex  = mesh->getVertices();
        const vector<uint32>&      listIndex   = mesh->getIndices();
        const float4               highlight   = SW_THEME_COLOR( "Viewport", "SelectionOutline" ).toFloat4();
        for ( const MeshSection& section : listSection )
        {
            const uint32 row = BodyCustomizationViewModel::findSectionRow( _bodyState, section._name );
            if ( row >= _bodyState._listSection.size() || ( _bodyState._listSection[row]._bHighlighted == SW_FALSE && row != _bodySelectedSection ) )
                continue;
            const uint32 triangleCount = section._indexCount / 3u;
            const uint32 stride        = triangleCount > Internal::kHighlightTriangleBudget ? triangleCount / Internal::kHighlightTriangleBudget + 1u : 1u;
            for ( uint32 triangle = 0; triangle < triangleCount; triangle += stride )
            {
                const uint32 first = section._firstIndex + triangle * 3u;
                float3       arrCorner[3]{};
                for ( uint32 corner = 0; corner < 3u; ++corner )
                {
                    const RHIVertex& vertex = listVertex[listIndex[first + corner]];
                    arrCorner[corner]       = float3::transform( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }, bodyWorld );
                }
                renderer.drawLine( arrCorner[0], arrCorner[1], highlight );
                renderer.drawLine( arrCorner[1], arrCorner[2], highlight );
                renderer.drawLine( arrCorner[2], arrCorner[0], highlight );
            }
        }
        if ( _bBodyPickShown == SW_TRUE )
        {
            const float4 mark = SW_THEME_COLOR( "Viewport", "SelectionOutline" ).toFloat4();
            for ( const float3& axis : {
                      float3{1.0f, 0.0f, 0.0f},
                      float3{0.0f, 1.0f, 0.0f},
                      float3{0.0f, 0.0f, 1.0f}
            } )
                renderer.drawLine( _bodyPickMarker - axis * Internal::kPickMarkLength, _bodyPickMarker + axis * Internal::kPickMarkLength, mark );
        }
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
        if ( EditorViewportProjectionUtil::makeMouseRay( viewProjection.invert(), position, size, ray ) == false )
            return;
        BodySurfaceHit hit{};
        if ( BodySurfaceHit::pick( listVertex, listIndex, listSection, bodyWorld, ray._origin, ray._direction, hit ) == false )
            return;
        _bodyPickMarker = hit._position;
        _bBodyPickShown = SW_TRUE;
        if ( bPlacingLayer )
        {
            // 배치 — 끄는 동안 같은 층의 가운데를 맞은 UV 로 옮긴다(한 되돌리기 단계).
            (void)BodyCustomizationViewModel::placeSkinLayer( _document.getAppearanceModel(), _bodyPickLayer, hit._uv, true );
            _bBodyPickDrag = SW_TRUE;
            return;
        }
        if ( hit._section < listSection.size() )
            _bodySelectedSection = BodyCustomizationViewModel::findSectionRow( _bodyState, listSection[hit._section]._name );
    }

    void CharacterEditorPanel::drawSkinLayerRows( const BodyCustomizationViewState& state )
    {
        const AppearanceDatabase& database   = _document.getDatabase();
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SkinLayers" ) );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPlugin", "AddSkinLayer" ), "AddSkinLayer" ), "" ) )
        {
            for ( size_t index = 0; index < state._listAvailableLayer.size(); ++index )
            {
                const hashed_string& layer = state._listAvailableLayer[index];
                if ( ImGui::Selectable( layer.c_str() ) )
                    (void)BodyCustomizationViewModel::addSkinLayer( appearance, database, layer );
                if ( index == 0 )
                    EditorSelfTestMarks::record( "charactereditor.body.addLayer.first" );
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
                if ( ImGui::ColorEdit4( "##color", &value._color._x, ImGuiColorEditFlags_NoInputs ) )
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
            if ( ImGui::SliderFloat( "##opacity", &value._opacity, 0.0f, 1.0f ) )
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
                bool bChanged = ImGui::DragFloat2( "##offset", &value._offset._x, CharacterEditorPanelInternal::kDragSpeed, 0.0f, 1.0f );
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerScale" ), SW_LOCKEY( "EdPlugin", "LayerScaleTooltip" ) );
                bChanged = ImGui::DragFloat2( "##scale", &value._scale._x, CharacterEditorPanelInternal::kDragSpeed, SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale ) || bChanged;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "LayerRotation" ), SW_LOCKEY( "EdPlugin", "LayerRotationTooltip" ) );
                bChanged     = ImGui::DragFloat( "##rotation", &value._rotationDegrees, CharacterEditorPanelInternal::kRotationDragSpeed, -SkinLayerValue::kMaxRotation, SkinLayerValue::kMaxRotation ) || bChanged;
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

    void CharacterEditorPanel::drawBodyDynamicsPreview( const BodyCustomizationViewState& state, GameObject* pBody )
    {
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "BodyDynamics" ) );
        if ( state._bHasBody == SW_FALSE || state._bodyProfile.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "BodyDynamicsNoProfile" ) );
            return;
        }
        // 미리보기 단추는 몸만 바꾼다(기록 없음) — 맞음은 지글, 울기 · 침은 체액 방아쇠, 지우기는 화장 번짐.
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "PreviewHit" ) ) )
            BodyCustomizationViewModel::previewHit( pBody );
        EditorSelfTestMarks::record( "charactereditor.body.hit" );
        for ( const hashed_string& trigger : state._listFluidTrigger )
        {
            ImGui::SameLine();
            if ( ImGui::Button( trigger.c_str() ) ) // 단추 이름은 데이터의 방아쇠 이름(Cry · Drool)이다
                BodyCustomizationViewModel::previewFluid( pBody, trigger );
        }
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ClearSmears" ) ) )
            BodyCustomizationViewModel::clearSmears( pBody );
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
                BodyCustomizationViewModel::previewActionTag( pBody, action._tag, bActive );
            bAnyActive = bAnyActive || bActive;
        }
        EditorSelfTestMarks::record( "charactereditor.body.actionTest" );
        _bodyStepTimer -= ImGui::GetIO().DeltaTime;
        if ( bAnyActive && _bodyStepTimer <= 0.0f )
        {
            BodyCustomizationViewModel::previewStep( pBody );
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
        ImGui::InputText( "##AppearanceFolder", paths._appearanceFolder.data(), paths._appearanceFolder.max_size() );
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
            if ( ImGui::InputText( "##Basepreset", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                (void)appearance.setBasePreset( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        }
        text = selection._bodyType.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BodyType" ) );
        if ( ImGui::InputText( "##Bodytype", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyType( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._bodyShape.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "BodyShape" ) );
        if ( ImGui::InputText( "##Bodyshape", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyShape( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._face.c_str();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Face" ) );
        if ( ImGui::InputText( "##Face", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
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
                if ( ImGui::InputText( request._slot.c_str(), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                    (void)appearance.setSlotItem( request._slot, CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
                ImGui::PopID();
            }
            ImGui::InputText( "##NewSlot", _newName.data(), _newName.max_size() );
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
            if ( ImGui::DragFloat( value._parameter.c_str(), &number, CharacterEditorPanelInternal::kDragSpeed ) )
                (void)appearance.setNumber( value._parameter, number, true ); // 같은 값이면 false 이고 그대로 둔다
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                appearance.endContinuousEdit();
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
        pSchema->normalize( selection._customization, normalized, nullptr );
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
                    if ( ImGui::SliderFloat( param._name.c_str(), &number, param._min, param._max ) )
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
                    if ( ImGui::ColorEdit4( param._name.c_str(), arrColor ) )
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
                    if ( ImGui::BeginCombo( param._name.c_str(), current.empty() ? "(none)" : current.c_str() ) )
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

    void CharacterEditorPanel::saveThumbnail()
    {
        if ( _thumbnailCapturePath.empty() == false )
        {
            _document.setStatus( "A thumbnail capture is still running" );
            return;
        }
        if ( _document.canAttachThumbnail() == false )
            return; // 까닭은 상태 줄에 남았다
        const string imagePath = CharacterEditorLocalStore::makeCaptureImagePath();
        const uint32 serial    = RenderThread::getCompletedScreenshotSerial(); // 요청 전에 읽는다 - 이 값이 바뀌면 그 뒤에 처리한 요청이다
        const bool   bReady    = FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( imagePath ) ) && getPreviewViewport().requestImageFile( imagePath );
        if ( bReady == false )
        {
            _document.setStatus( "The thumbnail needs the Preview tab drawn at least once (and a writable user data folder)" );
            return;
        }
        _thumbnailCapturePath   = imagePath;
        _thumbnailCapturePreset = string{ _document.getPaths()._presetName.c_str() };
        _thumbnailCaptureSerial = serial;
        SW_LOG_INFO_ID( "CharacterEditorPanel.CharacterEditorThumbnailPreset", "Character Editor: thumbnail of preset '%#' requested", _thumbnailCapturePreset.c_str() );
    }

    void CharacterEditorPanel::pollThumbnailCapture()
    {
        if ( _thumbnailCapturePath.empty() || RenderThread::getCompletedScreenshotSerial() == _thumbnailCaptureSerial )
            return;
        const string imagePath = std::move( _thumbnailCapturePath );
        _thumbnailCapturePath.clear();
        vector<uint8> pngBytes;
        // 다른 요청이 이 요청을 덮었거나(앞 요청이 남아 있으면 덮어쓴다) 쓰기에 실패하면 마지막 경로가 다르다.
        const bool bCaptured = RenderThread::getLastScreenshotPath() == imagePath && FileUtil::readFile( imagePath, pngBytes ) && pngBytes.empty() == false;
        if ( FileUtil::exists( imagePath ) && FileUtil::removeFile( imagePath ) == false )
            SW_LOG_WARNING_ID( "CharacterEditorPanel.CharacterEditorThumbnailCapture", "Character Editor: the thumbnail capture file %# could not be removed", imagePath.c_str() );
        if ( bCaptured == false )
        {
            _document.setStatus( "The thumbnail capture failed or was replaced by another screenshot - try again" );
            return;
        }
        _document.attachThumbnail( _thumbnailCapturePreset, std::move( pngBytes ) );
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
        ImGui::InputText( "##PresetName", paths._presetName.data(), paths._presetName.max_size() );
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
            for ( const UserAppearancePreset& preset : _document.getPresetStore().getPresets() )
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
        ImGui::InputText( "##ExportFolder", paths._exportFolder.data(), paths._exportFolder.max_size() );
        EditorSelfTestMarks::record( "charactereditor.exportFolder" );
        EditorChrome::setNextItemWidthColumns( 24.0f );
        ImGui::InputText( "##ExportID", paths._exportID.data(), paths._exportID.max_size() );
        EditorSelfTestMarks::record( "charactereditor.exportID" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ExportDataPreset" ) ) );
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ExportDataPreset" ) ) && _document.exportDataPreset() )
            ++_published._exportCount;
        EditorSelfTestMarks::record( "charactereditor.exportPreset" );
        ImGui::EndDisabled();
    }

    bool CharacterEditorPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, 1.0f, 0.0f };
        outRadius = CharacterEditorPanelInternal::kPreviewFrameRadius;
        return true;
    }
} // namespace sw::editor
