#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr float32 kDragSpeed                = 0.005f;
            static constexpr float32 kRotationDragSpeed        = 0.5f;
            static constexpr float32 kPathButtonsColumns       = 32.0f;
            static constexpr float32 kSocketListLines          = 7.0f;
            static constexpr float32 kBoneTreeLines            = 10.0f;
            static constexpr float32 kPresetListLines          = 5.0f;
            static constexpr float32 kExplanationListLines     = 7.0f;
            static constexpr float32 kPreviewHeight            = 260.0f;  ///< 미리보기 뷰포트 높이(96 DPI 픽셀)
            static constexpr float32 kCustomizationReserve     = 150.0f;  ///< 꾸미기 머리글이 펼쳐졌을 때 뷰포트 아래에 남기는 높이(머리글 + 슬라이더 몇 줄, 96 DPI 픽셀)
            static constexpr float32 kPreviewFrameRadius       = 1.4f;    ///< 미리보기 카메라가 담는 반지름(m) — 사람 키 하나
            static constexpr float32 kSocketAxisLength         = 0.08f;   ///< 미리보기 소켓 축 길이(m)
            static constexpr float32 kSelectedSocketAxisLength = 0.18f;   ///< 고른 소켓의 축 길이(m)
            static constexpr float32 kPenetrationMarkLength    = 0.01f;   ///< 관통 점 십자의 한 팔 길이(m)
            static constexpr float32 kBoneGizmoTolerance       = 1.0e-4f; ///< 놓은 월드 변환과 이만큼 다르면 기즈모가 옮긴 것이다(행렬 성분)

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
                            EditorSelfTestMarks::note( pFirstNameMark ); // 자동화가 첫 이름을 누를 자리
                        bFirstName = false;
                    }
                    ImGui::EndCombo();
                }
                return bChanged;
            }

            /** @brief @p other 가 @p param 과 같은 대칭 그룹의 다른 매개변수(같은 종류)면 true 입니다. 대칭 그룹이 비어 있으면 짝이 없다. */
            static bool isSymmetryPartner( const CustomizationParamDef& param, const CustomizationParamDef& other )
            {
                if ( param._symmetry.empty() || other._name == param._name )
                    return false;
                return other._symmetry == param._symmetry && other._kind == param._kind;
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

            [[nodiscard]] static bool readBoneGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneGizmoPlaced, outValue );
            }
            [[nodiscard]] static bool readPenetrationMarkCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_penetrationMarkCount, outValue );
            }
            [[nodiscard]] static bool readBoneCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_boneCount, outValue );
            }
            [[nodiscard]] static bool readSocketCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_socketCount, outValue );
            }
            [[nodiscard]] static bool readEquippedSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_equippedSlotCount, outValue );
            }
            [[nodiscard]] static bool readTraceCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &CharacterEditorPanel::Published::_traceCount, outValue );
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

    SW_AUTOMATION_WATCH( editorCharacterEditorBoneCount, "Editor.CharacterEditorBoneCount", "Bones of the skeleton the Character Editor has loaded", &CharacterEditorPanelInternal::readBoneCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorSocketCount, "Editor.CharacterEditorSocketCount", "Sockets in the socket file the Character Editor has loaded", &CharacterEditorPanelInternal::readSocketCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorEquippedSlotCount, "Editor.CharacterEditorEquippedSlotCount", "Slots that hold an item in the appearance selection",
                         &CharacterEditorPanelInternal::readEquippedSlotCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorTraceCount, "Editor.CharacterEditorTraceCount", "Rule explanation lines for the current appearance selection",
                         &CharacterEditorPanelInternal::readTraceCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorExportCount, "Editor.CharacterEditorExportCount", "Data presets the Character Editor exported in this run",
                         &CharacterEditorPanelInternal::readExportCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPresetCount, "Editor.CharacterEditorPresetCount", "Presets in the Character Editor's preset store", &CharacterEditorPanelInternal::readPresetCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPreviewAssembleCount, "Editor.CharacterEditorPreviewAssembleCount",
                         "Times the Character Editor preview body was assembled (published while the Preview tab draws)", &CharacterEditorPanelInternal::readPreviewAssembleCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorPenetrationMarks, "Editor.CharacterEditorPenetrationMarks", "Penetration points the Character Editor preview drew last frame (red crosses on the body)",
                         &CharacterEditorPanelInternal::readPenetrationMarkCount );
    SW_AUTOMATION_WATCH( editorCharacterEditorBoneGizmoPlaced, "Editor.CharacterEditorBoneGizmoPlaced", "1 while the Character Editor preview holds a gizmo marker on the selected bone",
                         &CharacterEditorPanelInternal::readBoneGizmoPlaced );
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
        , _bPreviewBodyDressed{ false }
        , _bModelsWereDirty{ false }
        , _bCustomizationOpen{ true }
        , _bCustomizationActive{ false }
        , _seenFitRevision{ 0 }
        , _gizmo{}
        , _boneGizmoTarget{}
        , _placedBoneWorld{}
        , _bBoneGizmoPlaced{ false }
        , _bRigPreview{ false }
        , _published{}
    {
    }

    void CharacterEditorPanel::drawDocumentContent()
    {
        using PackDataState = CharacterEditorDocument::PackDataState;
        _document.ensurePackData();
        if ( _document.getPaths()._rigPath.empty() )
            _bRigPreview = false; // 리그가 없는 게임으로 바뀌면 켜 둔 단추도 내린다
        beginPreviewFrame();
        updateFocusedDocument();
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
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getWarningColor() ), "A socket file was changed on disk while this window has unsaved edits." );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reload from disk (discard edits)" ) );
            if ( ImGui::SmallButton( "Reload from disk (discard edits)" ) && _document.loadSocketFile() == false )
                SW_LOG_WARNING( "Character Editor: the socket file '%#' could not be reloaded - the edits stay", _document.getPaths()._socketPath.c_str() ); // 까닭은 상태 줄에 남았다
        }
        ImGui::Separator();

        Published& published         = _published;
        published._boneCount         = _document.getPoseModel().getSkeleton().getBoneCount();
        published._socketCount       = static_cast<uint32>( _document.getSocketModel().getSockets().getSockets().size() );
        published._presetCount       = static_cast<uint32>( _document.getPresetStore().getPresets().size() );
        published._equippedSlotCount = _document.countEquippedSlots();

        if ( ImGui::BeginTabBar( "##CharacterEditorTabs" ) )
        {
            const bool bSocketTab = ImGui::BeginTabItem( "Sockets" );
            EditorSelfTestMarks::note( "charactereditor.tab.sockets" );
            if ( bSocketTab )
            {
                drawSocketTab();
                ImGui::EndTabItem();
            }
            const bool bSkeletonTab = ImGui::BeginTabItem( "Skeleton" );
            EditorSelfTestMarks::note( "charactereditor.tab.skeleton" );
            if ( bSkeletonTab )
            {
                drawSkeletonTab();
                ImGui::EndTabItem();
            }
            const bool bAppearanceTab = ImGui::BeginTabItem( "Appearance" );
            EditorSelfTestMarks::note( "charactereditor.tab.appearance" );
            if ( bAppearanceTab )
            {
                drawAppearanceTab();
                ImGui::EndTabItem();
            }
            // 미리보기는 탭 하나다 — 다른 탭의 줄을 밀지 않고, 가려진 탭은 그리지 않는다(뷰포트를 부른 프레임만 렌더한다).
            const bool bPreviewTab = ImGui::BeginTabItem( "Preview" );
            EditorSelfTestMarks::note( "charactereditor.tab.preview" );
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
        if ( AssetPaths::findLooseFile( string_view( paths._socketPath.c_str() ) ).getDiskPath().empty() )
            return ToolAssetLoadResult::Missing; // 새 소켓 파일 — 저장하면 만든다
        return _document.loadSocketFile() ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    ScenePreviewPolicy CharacterEditorPanel::getPreviewPolicy() const
    {
        ScenePreviewPolicy policy = EditorPreviewPanel::getPreviewPolicy();
        policy._bPlay             = true; // 몸의 애니메이터는 onBeginPlay 에서 유닛에 붙는다(외형 조립은 `previewSelection` 이 시작을 기다리지 않는다)
        return policy;
    }

    void CharacterEditorPanel::populatePreviewScene( Scene& scene )
    {
        (void)scene; // 몸은 외형 데이터를 읽은 뒤에 세운다(`syncPreviewBody`)
        getPreviewViewport().frameSphere( float3{ 0.0f, 1.0f, 0.0f }, CharacterEditorPanelInternal::kPreviewFrameRadius );
        _previewBody         = GameObjectHandle{};
        _bPreviewBodyDressed = false;
    }

    void CharacterEditorPanel::drawPreviewTab()
    {
        if ( _document.isDatabaseLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( "Load the appearance data (Appearance tab) to dress the preview body." );
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
            ImGui::Checkbox( "Preview constraint rig", &_bRigPreview );
            EditorSelfTestMarks::note( "charactereditor.rigPreview" );
            if ( pMutableBody != nullptr )
                syncRigPreview( *pMutableBody );
        }
        const AppearanceSelection& selection = _document.getAppearanceModel().getSelection();
        ImGui::TextDisabled( "Wearing %s on base preset '%s' (assembled %u times)", selection._listSlot.empty() ? "no slot items" : "the selection",
                             selection._basePresetID.empty() ? _document.getPreviewPresetID().c_str() : selection._basePresetID.c_str(), pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u );
        _published._previewAssembleCount = pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u;
        const float2  available          = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight          = CharacterEditorPanelInternal::kPreviewHeight * EditorThemeUtil::getDPIScale();
        // 뷰포트가 남은 높이를 모두 차지하면 슬라이더가 늘 접힌 아래로 밀려 스크롤 없이는 닿지 못한다 — 펼쳐졌을 때는 아래 몇 줄 자리를 남긴다.
        const float32 reserve        = _bCustomizationOpen ? CharacterEditorPanelInternal::kCustomizationReserve * EditorThemeUtil::getDPIScale() : 0.0f;
        const float32 viewportHeight = available._y - reserve;
        syncBoneGizmo();
        drawPreviewViewport( float2{ available._x, viewportHeight > minHeight ? viewportHeight : minHeight }, _gizmo.hasMouse() );
        _gizmo.draw( *this );
        EditorSelfTestMarks::note( "charactereditor.previewScene" );
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
        _bCustomizationOpen = ImGui::CollapsingHeader( "Customize (release a slider to refit)", ImGuiTreeNodeFlags_DefaultOpen );
        if ( _bCustomizationOpen )
            drawCustomizationSliders( selection );
        else
            _bCustomizationActive = false;
    }

    void CharacterEditorPanel::syncBoneGizmo()
    {
        GameObjectManager* pManager   = findPreviewManager();
        GameObject*        pBody      = pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
        GameObject*        pTarget    = pManager != nullptr ? pManager->resolveGameObject( _boneGizmoTarget ) : nullptr;
        BonePoseEditModel& pose       = _document.getPoseModel();
        const Skeleton&    skeleton   = pose.getSkeleton();
        const int32        boneIndex  = _document.getSelectedBone().empty() ? -1 : skeleton.findBoneIndex( _document.getSelectedBone() );
        SceneComponent*    pBodyScene = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
        if ( boneIndex < 0 || pBodyScene == nullptr )
        {
            if ( pTarget != nullptr )
                pManager->destroyObject( pTarget );
            _boneGizmoTarget            = GameObjectHandle{};
            _bBoneGizmoPlaced           = false;
            _published._boneGizmoPlaced = 0;
            return;
        }
        // 덮어쓴 값을 얹은 모델 변환 — 부모까지 줄을 올라가 루트부터 곱한다(행벡터: 자식 × 부모).
        vector<int32> listChain;
        for ( int32 index = boneIndex; index >= 0; index = skeleton.getBone( static_cast<uint32>( index ) )._parentIndex )
        {
            listChain.push_back( index );
        }
        float4x4 parentModel = float4x4::Identity;
        float4x4 boneModel   = float4x4::Identity;
        for ( size_t chainIndex = listChain.size(); chainIndex > 0; --chainIndex )
        {
            BoneTransform local{};
            if ( pose.getEffectiveTransform( skeleton.getBone( static_cast<uint32>( listChain[chainIndex - 1] ) )._name, local ) == false )
                return;
            parentModel = boneModel;
            boneModel   = local.toMatrix() * boneModel;
        }
        const float4x4 bodyWorld    = pBodyScene->getWorldMatrix();
        const float4x4 desiredWorld = boneModel * bodyWorld;
        if ( pTarget == nullptr )
        {
            pTarget = pManager->createGameObject( hashed_string( "BoneGizmoTarget" ) );
            if ( pTarget == nullptr || pTarget->addComponent<SceneComponent>() == nullptr )
            {
                _document.setStatus( "The bone gizmo target could not be created" );
                return;
            }
            _boneGizmoTarget  = pTarget->getHandle();
            _bBoneGizmoPlaced = false;
        }
        SceneComponent* pScene = pTarget->getPrimarySceneComponent();
        if ( pScene == nullptr )
            return;
        if ( getPreviewSelection().getPrimaryObject() != pTarget )
            getPreviewSelection().selectObject( pTarget );
        // 지난 그리기에 놓은 자리와 달라졌으면 기즈모가 옮긴 것이다 — 부모 월드의 역으로 본의 로컬을 구해 모델에 넣는다.
        bool           bMoved       = false;
        const float4x4 currentWorld = pScene->getWorldMatrix();
        if ( _bBoneGizmoPlaced )
        {
            for ( uint32 element = 0; element < 16 && bMoved == false; ++element )
            {
                bMoved = MathUtil::abs( currentWorld.data()[element] - _placedBoneWorld.data()[element] ) > CharacterEditorPanelInternal::kBoneGizmoTolerance;
            }
        }
        if ( bMoved )
        {
            const BoneTransform local = BoneTransform::makeFromMatrix( currentWorld * ( parentModel * bodyWorld ).invert() );
            const hashed_string bone  = skeleton.getBone( static_cast<uint32>( boneIndex ) )._name;
            string              error;
            const float3        degrees = local._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
            (void)pose.setTranslation( bone, local._translation, true, error ); // 같은 값이면 false 이고 오류는 비어 있다
            (void)pose.setRotationDegrees( bone, degrees, true, error );        // 같은 값이면 false 이고 오류는 비어 있다
            _document.setStatus( error );
        }
        else if ( _bBoneGizmoPlaced && _gizmo.hasMouse() == false )
        {
            pose.endContinuousEdit(); // 끌기가 끝났다 — 다음 편집은 새 되돌리기 단계
        }
        if ( bMoved == false )
        {
            pScene->setWorldTransform( desiredWorld );
            _placedBoneWorld = pScene->getWorldMatrix();
        }
        else
        {
            _placedBoneWorld = currentWorld; // 다음 그리기는 모델 값으로 다시 놓는다(위 `bMoved == false` 경로가 desired 로 맞춘다)
        }
        _bBoneGizmoPlaced           = true;
        _published._boneGizmoPlaced = 1;
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
            return;
        }
        const float4x4 bodyWorld       = pBodyScene->getWorldMatrix();
        const float4   arrAxisColor[3] = {
            float4{ 1.0f, 0.25f, 0.25f, 1.0f},
            float4{ 0.3f,  1.0f,  0.3f, 1.0f},
            float4{0.35f,  0.5f,  1.0f, 1.0f}
        };
        const float3 arrAxis[3] = {
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
                pManager->destroyObject( pBody );
                _document.setStatus( "The preview body could not take its components" );
                return;
            }
            pAnimator->setClipFolder( string_view( _document.getPaths()._clipFolder.c_str() ) );
            _previewBody         = pBody->getHandle();
            _bPreviewBodyDressed = false;
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
        _bPreviewBodyDressed     = true;
    }

    void CharacterEditorPanel::drawAnimationControls( SkeletalAnimatorComponent& animator )
    {
        const vector<hashed_string>& listClipName = _document.getClipNames();
        if ( listClipName.empty() )
        {
            ImGui::TextDisabled( "No animation clips in '%s'", _document.getPaths()._clipFolder.c_str() );
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
                    EditorSelfTestMarks::note( "charactereditor.clip.first" ); // 자동화가 첫 클립을 누를 자리
                bFirst = false;
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( "charactereditor.clip" );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Loop##PreviewClip" ) );
        ImGui::Checkbox( "Loop##PreviewClip", &_bClipLoop );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Play##PreviewClip" ) );
        const hashed_string& clip = _document.getSelectedClip();
        ImGui::BeginDisabled( clip.empty() );
        if ( ImGui::Button( "Play##PreviewClip" ) )
        {
            if ( animator.preloadClip( clip ) && animator.play( clip, _bClipLoop, 0.0f ) )
                _document.clearStatus();
            else
                _document.setStatus( "The clip could not be played (see log)" );
        }
        EditorSelfTestMarks::note( "charactereditor.playClip" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Stop##PreviewClip" ) );
        if ( ImGui::Button( "Stop##PreviewClip" ) )
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
        ImGui::InputText( "##SocketPath", paths._socketPath.data(), paths._socketPath.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load" ) );
        if ( ImGui::Button( "Load" ) )
            reloadDocument(); // 경로 칸의 소켓 파일을 다시 읽는다 — 읽음 · 못 읽음 표시와 미저장 기준은 문서 계약이 맞춘다
        EditorSelfTestMarks::note( "charactereditor.loadSockets" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        ImGui::BeginDisabled( canSaveDocument() == false );
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "New" ) );
        if ( ImGui::Button( "New" ) )
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
        if ( ImGui::Button( "Undo" ) )
            (void)sockets.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Redo" ) );
        ImGui::BeginDisabled( sockets.canRedo() == false );
        if ( ImGui::Button( "Redo" ) )
            (void)sockets.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::drawSocketTab()
    {
        drawSocketList();
        const SocketDef* pSelected = _document.getSocketModel().findSocket( _document.getSelectedSocket() );
        if ( pSelected != nullptr )
        {
            ImGui::Separator();
            const SocketDef selected = *pSelected; // 편집이 표를 바꾸므로 복사본으로 그린다
            drawSocketDetail( selected );
        }

        ImGui::Separator();
        vector<string> listIssue;
        if ( _document.getSocketModel().validate( listIssue ) == false )
        {
            for ( const string& issue : listIssue )
            {
                ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "%s", issue.c_str() );
            }
        }
    }

    void CharacterEditorPanel::drawSocketList()
    {
        ImGui::InputText( "##NewSocketName", _newName.data(), _newName.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Add" ) );
        if ( ImGui::Button( "Add" ) )
            _document.addSocket( CharacterEditorPanelInternal::makeName( _newName.c_str() ) );

        if ( ImGui::BeginChild( "##SocketList", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CharacterEditorPanelInternal::kSocketListLines ) ), ImGuiChildFlags_Borders ) )
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
                    EditorSelfTestMarks::note( "charactereditor.socket.first" );
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
        string&             status  = _document.editStatus();

        EditorChrome::setNextItemWidthColumns( 24.0f );
        ImGui::InputText( "##Rename", _renameBuffer.data(), _renameBuffer.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Rename" ) );
        if ( ImGui::Button( "Rename" ) )
            _document.renameSocket( name, CharacterEditorPanelInternal::makeName( _renameBuffer.c_str() ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Mirror" ) );
        if ( ImGui::Button( "Mirror" ) )
            _document.mirrorSocket( name );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
        if ( ImGui::Button( "Remove" ) )
        {
            _document.removeSocket( name );
            return;
        }

        fixed_string<constant::kMaxBuffer128> parent;
        parent = socket._parent.c_str();
        if ( ImGui::InputText( "Parent", parent.data(), parent.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            _document.reportEdit( sockets.setParent( name, CharacterEditorPanelInternal::makeName( parent.c_str() ), status ) );

        hashed_string pickedKind;
        if ( CharacterEditorPanelInternal::drawNameCombo( "Kind", socket._kind, sockets.getKindTable().getKinds(), pickedKind ) )
            _document.reportEdit( sockets.setKind( name, pickedKind, status ) );

        float3 translation = socket._translation;
        if ( ImGui::DragFloat3( "Translation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setTranslation( name, translation, true, status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 degrees = socket._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        if ( ImGui::DragFloat3( "Rotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( sockets.setRotationDegrees( name, degrees, true, status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        float3 scale = socket._scale;
        if ( ImGui::DragFloat3( "Scale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( sockets.setScale( name, scale, true, status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            sockets.endContinuousEdit();

        int32 anchorIndex = socket._anchor == SocketAnchor::Surface ? 1 : 0;
        if ( ImGui::Combo( "Anchor", &anchorIndex, "Bone\0Surface\0" ) )
            _document.reportEdit( sockets.setAnchor( name, anchorIndex == 1 ? SocketAnchor::Surface : SocketAnchor::Bone, status ) );

        fixed_string<constant::kMaxPathSize> previewMesh;
        previewMesh = string_view( socket._previewMesh );
        if ( ImGui::InputText( "Preview mesh", previewMesh.data(), previewMesh.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            _document.reportEdit( sockets.setPreviewMesh( name, previewMesh.c_str(), status ) );
    }

    void CharacterEditorPanel::drawSkeletonTab()
    {
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        BonePoseEditModel&    pose  = _document.getPoseModel();
        ImGui::InputText( "##SkeletonPath", paths._skeletonPath.data(), paths._skeletonPath.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load skeleton" ) );
        if ( ImGui::Button( "Load skeleton" ) )
            _document.loadSkeleton();
        EditorSelfTestMarks::note( "charactereditor.loadSkeleton" );

        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        ImGui::InputText( "##RefPosePath", paths._refPosePath.data(), paths._refPosePath.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load pose" ) );
        if ( ImGui::Button( "Load pose" ) )
            _document.loadReferencePose();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save pose" ) );
        ImGui::BeginDisabled( pose.isDirty() == false );
        if ( ImGui::Button( "Save pose" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();

        ImGui::BeginDisabled( pose.canUndo() == false );
        if ( ImGui::Button( "Undo pose" ) )
            (void)pose.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Redo pose" ) );
        ImGui::BeginDisabled( pose.canRedo() == false );
        if ( ImGui::Button( "Redo pose" ) )
            (void)pose.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "%u bones, %u overridden%s", pose.getSkeleton().getBoneCount(), static_cast<uint32>( pose.getOverride().getOverrides().size() ),
                             pose.isDirty() ? " (modified)" : "" );

        ImGui::Separator();
        if ( pose.getSkeleton().getBoneCount() == 0 )
        {
            ImGui::TextDisabled( "Load a skeleton to see its bones. The skeleton is read-only; edits go to the reference pose file." );
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
        ImGui::TextDisabled( "Viewport gizmo editing needs the preview scene (EditorMultiWorld)." );
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
            EditorSelfTestMarks::note( "charactereditor.bone.root" );
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
            ImGui::TextDisabled( "Select a bone to edit its reference pose." );
            return;
        }
        ImGui::TextUnformatted( bone.c_str() );
        string error;

        float3 translation = current._translation;
        if ( ImGui::DragFloat3( "Bone translation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( pose.setTranslation( bone, translation, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();
        EditorSelfTestMarks::note( "charactereditor.bone.translation" );

        float3 degrees = current._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        if ( ImGui::DragFloat3( "Bone rotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            _document.reportEdit( pose.setRotationDegrees( bone, degrees, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();

        float3 scale = current._scale;
        if ( ImGui::DragFloat3( "Bone scale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            _document.reportEdit( pose.setScale( bone, scale, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pose.endContinuousEdit();

        ImGui::BeginDisabled( pose.hasOverride( bone ) == false );
        if ( ImGui::Button( "Reset bone" ) )
            _document.reportEdit( pose.clearBone( bone, error ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Mirror bone" ) );
        if ( ImGui::Button( "Mirror bone" ) )
            _document.mirrorSelectedBone();
        ImGui::EndDisabled();
        if ( error.empty() == false )
            _document.setStatus( error );
    }

    void CharacterEditorPanel::drawAppearanceTab()
    {
        drawAppearanceDataRow();
        // 데이터 프리셋 내보내기는 데이터 줄 바로 아래 — 씬이 쓰는 결과물이라 창을 열면 보이는 자리에 둔다.
        drawDataPresetRow();
        ImGui::Separator();
        ImGui::TextDisabled( "Selection (base preset, body, slots, customization values)" );
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        const AppearanceSelection selection  = appearance.getSelection();
        drawSelectionHeader( selection );
        ImGui::Separator();
        drawSlotRows( selection );
        ImGui::Separator();
        drawCustomizationRows( selection );

        ImGui::BeginDisabled( appearance.canUndo() == false );
        if ( ImGui::Button( "Undo appearance" ) )
            (void)appearance.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Redo appearance" ) );
        ImGui::BeginDisabled( appearance.canRedo() == false );
        if ( ImGui::Button( "Redo appearance" ) )
            (void)appearance.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();

        ImGui::Separator();
        drawExplanation();
        drawFitCheck();
        drawPresetRow();
    }

    void CharacterEditorPanel::drawAppearanceDataRow()
    {
        const AppearanceDatabase& database = _document.getDatabase();
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        ImGui::InputText( "##ItemsPath", paths._itemsPath.data(), paths._itemsPath.max_size() );
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        ImGui::InputText( "##AppearanceFolder", paths._appearanceFolder.data(), paths._appearanceFolder.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load data" ) );
        if ( ImGui::Button( "Load data" ) )
            _document.loadAppearanceData();
        EditorSelfTestMarks::note( "charactereditor.loadData" );
        if ( _document.isDatabaseLoaded() )
            ImGui::TextDisabled( "%u slots, %u sets, %u presets", static_cast<uint32>( database.getSlotTable().getSlots().size() ), static_cast<uint32>( database.getSets().getSets().size() ),
                                 static_cast<uint32>( database.getPresets().getPresets().size() ) );
        else
            ImGui::TextDisabled( "Load the items file and the appearance folder to pick items and see rule explanations." );
    }

    void CharacterEditorPanel::drawSelectionHeader( const AppearanceSelection& selection )
    {
        const AppearanceDatabase&             database   = _document.getDatabase();
        AppearanceEditModel&                  appearance = _document.getAppearanceModel();
        fixed_string<constant::kMaxBuffer128> text;
        if ( _document.isDatabaseLoaded() )
        {
            vector<hashed_string> listPreset;
            for ( const CharacterAppearanceDef& preset : database.getPresets().getPresets() )
            {
                listPreset.push_back( preset._id );
            }
            hashed_string pickedPreset;
            const bool    bPresetPicked = CharacterEditorPanelInternal::drawNameCombo( "Base preset", selection._basePresetID, listPreset, pickedPreset, "charactereditor.basePreset.first" );
            EditorSelfTestMarks::note( "charactereditor.basePreset" );
            if ( bPresetPicked )
                (void)appearance.setBasePreset( pickedPreset ); // 같은 값이면 false 이고 그대로 둔다
        }
        else
        {
            text = selection._basePresetID.c_str();
            if ( ImGui::InputText( "Base preset", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                (void)appearance.setBasePreset( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        }
        text = selection._bodyType.c_str();
        if ( ImGui::InputText( "Body type", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyType( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._bodyShape.c_str();
        if ( ImGui::InputText( "Body shape", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setBodyShape( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
        text = selection._face.c_str();
        if ( ImGui::InputText( "Face", text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)appearance.setFace( CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
    }

    void CharacterEditorPanel::drawSlotRows( const AppearanceSelection& selection )
    {
        const AppearanceDatabase& database   = _document.getDatabase();
        AppearanceEditModel&      appearance = _document.getAppearanceModel();
        ImGui::TextUnformatted( "Slots" );
        if ( _document.isDatabaseLoaded() == false )
        {
            fixed_string<constant::kMaxBuffer128> text;
            for ( const AppearanceSlotRequest& request : selection._listSlot )
            {
                ImGui::PushID( request._slot.c_str() );
                text = request._itemID.c_str();
                if ( ImGui::InputText( request._slot.c_str(), text.data(), text.capacity() + 1, ImGuiInputTextFlags_EnterReturnsTrue ) )
                    (void)appearance.setSlotItem( request._slot, CharacterEditorPanelInternal::makeName( text.c_str() ) ); // 같은 값이면 false 이고 그대로 둔다
                ImGui::PopID();
            }
            ImGui::InputText( "##NewSlot", _newName.data(), _newName.max_size() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Add slot" ) );
            if ( ImGui::Button( "Add slot" ) )
                (void)appearance.setSlotItem( CharacterEditorPanelInternal::makeName( _newName.c_str() ), hashed_string{} ); // 이미 있으면 false 이고 그대로 둔다
            return;
        }

        // 세트 고르기는 칸 목록 위에 둔다 — 패널이 낮아도 보이게.
        vector<hashed_string> listSet;
        for ( const EquipSetDef& set : database.getSets().getSets() )
        {
            listSet.push_back( set._id );
        }
        hashed_string pickedSet;
        const bool    bSetPicked = CharacterEditorPanelInternal::drawNameCombo( "Equip set", hashed_string{}, listSet, pickedSet, "charactereditor.equipSet.first" );
        EditorSelfTestMarks::note( "charactereditor.equipSet" );
        if ( bSetPicked && pickedSet.empty() == false )
            (void)appearance.equipSet( database, pickedSet ); // 이미 입고 있으면 false 이고 그대로 둔다

        // 데이터가 있으면 슬롯 표의 칸마다 그 칸에 낄 수 있는 아이템만 고르게 한다.
        vector<hashed_string> listItem;
        bool                  bFirstSlot = true;
        for ( const AppearanceSlotDef& def : database.getSlotTable().getSlots() )
        {
            AppearanceEditModel::collectSlotItems( database, def._name, listItem );
            hashed_string picked;
            ImGui::PushID( def._name.c_str() );
            if ( CharacterEditorPanelInternal::drawNameCombo( def._name.c_str(), appearance.findSlotItem( def._name ), listItem, picked ) )
                (void)appearance.setSlotItem( def._name, picked );
            if ( bFirstSlot )
            {
                EditorSelfTestMarks::note( "charactereditor.slot.first" );
                bFirstSlot = false;
            }
            ImGui::PopID();
        }
    }

    void CharacterEditorPanel::drawCustomizationRows( const AppearanceSelection& selection )
    {
        AppearanceEditModel& appearance = _document.getAppearanceModel();
        ImGui::TextUnformatted( "Customization values" );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            float32 number = value._number._x;
            if ( ImGui::DragFloat( value._parameter.c_str(), &number, CharacterEditorPanelInternal::kDragSpeed ) )
                (void)appearance.setNumber( value._parameter, number, true ); // 같은 값이면 false 이고 그대로 둔다
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                appearance.endContinuousEdit();
        }
        ImGui::TextDisabled( "Sliders, colours and choices of the schema are in the Preview tab (the body updates when you release a slider)." );
    }

    void CharacterEditorPanel::drawCustomizationSliders( const AppearanceSelection& selection )
    {
        // 스키마는 선택의 것, 없으면 기준 프리셋(없으면 미리보기 몸 프리셋)의 것이다(`findCustomizationSchema`).
        AppearanceEditModel&          appearance = _document.getAppearanceModel();
        const CustomizationSchemaDef* pSchema    = _document.findCustomizationSchema( selection );
        if ( pSchema == nullptr )
        {
            _bCustomizationActive = false;
            ImGui::TextDisabled( "No customization schema for this selection (load the appearance data and pick a base preset with a schema)." );
            return;
        }
        CustomizationValueSet normalized;
        CustomizationUtil::normalize( *pSchema, selection._customization, normalized, nullptr );
        bool          bAnyActive   = false;
        bool          bFirstSlider = true;
        hashed_string category{};
        bool          bFirst = true;
        for ( const CustomizationParamDef& param : pSchema->_listParameter )
        {
            if ( CustomizationUtil::isActive( param, *pSchema, normalized ) == false )
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
                        (void)appearance.setNumber( param._name, number, true ); // 같은 값이면 false 이고 그대로 둔다
                        // 대칭 그룹(좌우 귀)의 짝은 같은 값으로 함께 바뀐다 — 자기 범위로 자른다.
                        for ( const CustomizationParamDef& other : pSchema->_listParameter )
                        {
                            if ( CharacterEditorPanelInternal::isSymmetryPartner( param, other ) )
                                (void)appearance.setNumber( other._name, MathUtil::clamp( number, other._min, other._max ), true );
                        }
                    }
                    bAnyActive = bAnyActive || ImGui::IsItemActive();
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        appearance.endContinuousEdit();
                    if ( bFirstSlider )
                        EditorSelfTestMarks::note( "charactereditor.slider.first" ); // 첫 슬라이더 — 자동화가 누를 자리(마지막 것은 스키마가 길면 접힌 아래다)
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
                        (void)appearance.setColor( param._name, edited, true ); // 같은 값이면 false 이고 그대로 둔다
                        for ( const CustomizationParamDef& other : pSchema->_listParameter )
                        {
                            if ( CharacterEditorPanelInternal::isSymmetryPartner( param, other ) )
                                (void)appearance.setColor( other._name, edited, true );
                        }
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
        string thumbnailID;
        if ( _document.makeThumbnailID( thumbnailID ) == false )
            return; // 까닭은 상태 줄에 남았다
        const string imagePath = AssetPaths::getWritePath( thumbnailID );
        if ( imagePath.empty() || getPreviewViewport().requestImageFile( imagePath ) == false )
        {
            _document.setStatus( "The thumbnail needs a write path and the Preview tab drawn at least once" );
            return;
        }
        if ( _document.attachThumbnail( thumbnailID ) )
            SW_LOG_INFO( "Character Editor: thumbnail of preset '%#' requested -> %#", _document.getPaths()._presetName.c_str(), imagePath.c_str() );
    }

    void CharacterEditorPanel::drawExplanation()
    {
        Published& published  = _published;
        published._traceCount = 0;
        if ( ImGui::CollapsingHeader( "Why does it look like this? (rules)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        if ( _document.isDatabaseLoaded() == false )
        {
            ImGui::TextDisabled( "Load appearance data to see which rules hid or changed what." );
            return;
        }
        // 기준 프리셋이 칸 · 몸 종류의 시드라서, 없으면 펼칠 것이 없고 해석기가 칸마다 경고를 낸다 — 고르게 하고 넘어간다.
        if ( _document.hasBasePresetInData() == false )
        {
            ImGui::TextDisabled( "Pick a base preset to see which rules hid or changed what." );
            return;
        }
        _document.refreshResolvedPreview(); // 선택 · 데이터가 바뀔 때만 다시 푼다
        const ResolvedAppearance&        resolved = _document.getResolvedPreview();
        const AppearanceSelectionReport& report   = _document.getResolvedReport();
        published._traceCount                     = static_cast<uint32>( resolved._listTrace.size() );

        if ( report._bUnknownBasePreset == SW_TRUE )
            ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "Base preset is not in the data" );
        for ( const hashed_string& dropped : report._listDroppedParameter )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "Dropped: %s", dropped.c_str() );
        }
        for ( const AppearanceSelectionReport::SlotFallback& fallback : report._listSlotFallback )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "Slot %s: %s -> %s (%s)", fallback._slot.c_str(), fallback._requestedItem.c_str(), fallback._fallbackItem.c_str(),
                                getEnumName( fallback._reason ) );
        }
        if ( ImGui::BeginChild( "##RuleTrace", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, CharacterEditorPanelInternal::kExplanationListLines ) ), ImGuiChildFlags_Borders ) )
        {
            if ( resolved._listTrace.empty() )
                ImGui::TextDisabled( "No rule fired." );
            for ( const AppearanceTraceEntry& entry : resolved._listTrace )
            {
                ImGui::TextWrapped( "[%s] %s: %s", getEnumName( entry._step ), entry._source.c_str(), entry._message.c_str() );
            }
        }
        ImGui::EndChild();
    }

    void CharacterEditorPanel::drawFitCheck()
    {
        if ( ImGui::CollapsingHeader( "Fit check (view only)" ) == false )
            return;
        if ( _document.hasFit() == false )
        {
            ImGui::TextDisabled( "No fit result yet. The preview scene solves the fit when a slider is released." );
            return;
        }
        const FitCheckSummary& fit = _document.getFit();
        for ( const FitPartSummary& part : fit._listPart )
        {
            ImGui::Text( "%s: cut %u/%u (%.0f%%), %u verts moved, max push %.3f", part._name.c_str(), part._cutTriangleCount, part._triangleCount, static_cast<float64>( part._cutRatio * 100.0f ),
                         part._movedVertexCount, static_cast<float64>( part._maxDelta ) );
        }
        if ( fit.hasPenetration() == false )
            ImGui::TextDisabled( "No penetration." );
        for ( const FitPenetrationSummary& penetration : fit._listPenetration )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "Penetration %s / %s: %u points, depth %.3f", penetration._partA.c_str(), penetration._partB.c_str(),
                                penetration._vertexCount, static_cast<float64>( penetration._maxDepth ) );
        }
        for ( const string& message : fit._listMessage )
        {
            ImGui::TextWrapped( "%s", message.c_str() );
        }
    }

    void CharacterEditorPanel::drawPresetRow()
    {
        if ( ImGui::CollapsingHeader( "Presets (file)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        ImGui::InputText( "##PresetPath", paths._presetPath.data(), paths._presetPath.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load presets" ) );
        if ( ImGui::Button( "Load presets" ) )
            _document.loadPresets();
        EditorChrome::setNextItemWidthColumns( 24.0f );
        ImGui::InputText( "##PresetName", paths._presetName.data(), paths._presetName.max_size() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save as preset" ) );
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( "Save as preset" ) )
            _document.savePreset();
        EditorSelfTestMarks::note( "charactereditor.savePreset" );
        ImGui::EndDisabled();
        if ( _document.getAppearanceModel().isDirty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(unsaved)" );
        }
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( "Save thumbnail" ) )
            saveThumbnail();
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "charactereditor.saveThumbnail" );
        EditorWidgets::drawTooltip( "Writes the Preview tab's picture next to the preset file and attaches it to the preset of this name (save the preset first; it is saved again with the current selection)" );

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
        if ( ImGui::CollapsingHeader( "Data preset (scene use)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        ImGui::TextDisabled( "A scene's Character Appearance component names this id." );
        EditorChrome::setNextItemWidthColumns( -CharacterEditorPanelInternal::kPathButtonsColumns );
        CharacterEditorPaths& paths = _document.editPaths();
        ImGui::InputText( "##ExportFolder", paths._exportFolder.data(), paths._exportFolder.max_size() );
        EditorSelfTestMarks::note( "charactereditor.exportFolder" );
        EditorChrome::setNextItemWidthColumns( 24.0f );
        ImGui::InputText( "##ExportID", paths._exportID.data(), paths._exportID.max_size() );
        EditorSelfTestMarks::note( "charactereditor.exportID" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Export data preset" ) );
        ImGui::BeginDisabled( _document.isDatabaseLoaded() == false );
        if ( ImGui::Button( "Export data preset" ) && _document.exportDataPreset() )
            ++_published._exportCount;
        EditorSelfTestMarks::note( "charactereditor.exportPreset" );
        ImGui::EndDisabled();
    }
} // namespace sw::editor
