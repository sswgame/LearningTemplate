#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include <algorithm>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr const utf8* kDefaultPresetPath        = "editor/characterpresets.txt";
            static constexpr const utf8* kDefaultExportID          = "MyCharacter";
            static constexpr float32     kDragSpeed                = 0.005f;
            static constexpr float32     kRotationDragSpeed        = 0.5f;
            static constexpr float32     kPathButtonsWidth         = 240.0f;
            static constexpr float32     kSocketListHeight         = 140.0f;
            static constexpr float32     kBoneTreeHeight           = 200.0f;
            static constexpr float32     kPresetListHeight         = 90.0f;
            static constexpr float32     kExplanationListHeight    = 130.0f;
            static constexpr float32     kPreviewHeight            = 260.0f;  ///< 미리보기 뷰포트 높이(96 DPI 픽셀)
            static constexpr float32     kPreviewFrameRadius       = 1.4f;    ///< 미리보기 카메라가 담는 반지름(m) — 사람 키 하나
            static constexpr float32     kSocketAxisLength         = 0.08f;   ///< 미리보기 소켓 축 길이(m)
            static constexpr float32     kSelectedSocketAxisLength = 0.18f;   ///< 고른 소켓의 축 길이(m)
            static constexpr float32     kPenetrationMarkLength    = 0.01f;   ///< 관통 점 십자의 한 팔 길이(m)
            static constexpr float32     kBoneGizmoTolerance       = 1.0e-4f; ///< 놓은 월드 변환과 이만큼 다르면 기즈모가 옮긴 것이다(행렬 성분)

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

            /** @brief 자동화 탐침이 읽는 값입니다. 패널이 그릴 때마다 게시합니다(게임 스레드). */
            struct Published
            {
                uint32 _boneCount{ 0 };
                uint32 _socketCount{ 0 };
                uint32 _equippedSlotCount{ 0 };
                uint32 _traceCount{ 0 };
                uint32 _presetCount{ 0 };
                uint32 _exportCount{ 0 };          ///< 이 실행에서 데이터 프리셋으로 내보낸 수
                uint32 _previewAssembleCount{ 0 }; ///< 미리보기 몸을 조립한 수(미리보기 탭을 그릴 때 게시)
                uint32 _clipPlaying{ 0 };          ///< 미리보기 몸이 고른 클립을 재생 중이면 1(미리보기 탭을 그릴 때 게시)
                uint32 _penetrationMarkCount{ 0 }; ///< 지난 그리기에 뷰포트에 찍은 관통 점 수
                uint32 _boneGizmoPlaced{ 0 };      ///< 고른 본에 기즈모 표식을 놓았으면 1
            };

            [[nodiscard]] static bool readBoneGizmoPlaced( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._boneGizmoPlaced );
                return true;
            }

            [[nodiscard]] static bool readPenetrationMarkCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._penetrationMarkCount );
                return true;
            }
            static Published& getPublished()
            {
                static Published s_published;
                return s_published;
            }

            [[nodiscard]] static bool readBoneCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._boneCount );
                return true;
            }
            [[nodiscard]] static bool readSocketCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._socketCount );
                return true;
            }
            [[nodiscard]] static bool readEquippedSlotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._equippedSlotCount );
                return true;
            }
            [[nodiscard]] static bool readTraceCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._traceCount );
                return true;
            }
            [[nodiscard]] static bool readExportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._exportCount );
                return true;
            }
            [[nodiscard]] static bool readPresetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._presetCount );
                return true;
            }
            [[nodiscard]] static bool readPreviewAssembleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._previewAssembleCount );
                return true;
            }
            [[nodiscard]] static bool readClipPlaying( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getPublished()._clipPlaying );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( CharacterEditorPanel, CharacterEditorPanel::kPanelID, EditorPanelCategory::Custom, 9020 );

    SW_AUTOMATION_PROBE( editorCharacterEditorBoneCount, "Editor.CharacterEditorBoneCount", "Bones of the skeleton the Character Editor has loaded", &CharacterEditorPanelInternal::readBoneCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorSocketCount, "Editor.CharacterEditorSocketCount", "Sockets in the socket file the Character Editor has loaded", &CharacterEditorPanelInternal::readSocketCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorEquippedSlotCount, "Editor.CharacterEditorEquippedSlotCount", "Slots that hold an item in the appearance selection",
                         &CharacterEditorPanelInternal::readEquippedSlotCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorTraceCount, "Editor.CharacterEditorTraceCount", "Rule explanation lines for the current appearance selection",
                         &CharacterEditorPanelInternal::readTraceCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorExportCount, "Editor.CharacterEditorExportCount", "Data presets the Character Editor exported in this run",
                         &CharacterEditorPanelInternal::readExportCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorPresetCount, "Editor.CharacterEditorPresetCount", "Presets in the Character Editor's preset store", &CharacterEditorPanelInternal::readPresetCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorPreviewAssembleCount, "Editor.CharacterEditorPreviewAssembleCount",
                         "Times the Character Editor preview body was assembled (published while the Preview tab draws)", &CharacterEditorPanelInternal::readPreviewAssembleCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorPenetrationMarks, "Editor.CharacterEditorPenetrationMarks", "Penetration points the Character Editor preview drew last frame (red crosses on the body)",
                         &CharacterEditorPanelInternal::readPenetrationMarkCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorBoneGizmoPlaced, "Editor.CharacterEditorBoneGizmoPlaced", "1 while the Character Editor preview holds a gizmo marker on the selected bone",
                         &CharacterEditorPanelInternal::readBoneGizmoPlaced );
    SW_AUTOMATION_PROBE( editorCharacterEditorPreviewClipPlaying, "Editor.CharacterEditorPreviewClipPlaying", "1 while the Character Editor preview body plays the selected clip (published while the Preview tab draws)",
                         &CharacterEditorPanelInternal::readClipPlaying );
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorPanel::CharacterEditorPanel()
        : EditorPreviewPanel{ EditorAssetType::Data, true }
        , _sockets{}
        , _pose{}
        , _appearance{}
        , _reloadGuard{}
        , _fit{}
        , _pItems{}
        , _database{}
        , _presetStore{}
        , _path{}
        , _skeletonPath{}
        , _refPosePath{}
        , _itemsPath{}
        , _appearanceFolder{}
        , _clipFolder{}
        , _presetPath{ CharacterEditorPanelInternal::kDefaultPresetPath }
        , _presetName{ "MyLook" }
        , _exportFolder{}
        , _exportID{ CharacterEditorPanelInternal::kDefaultExportID }
        , _newName{}
        , _renameBuffer{}
        , _status{}
        , _selectedSocket{}
        , _selectedBone{}
        , _listClipName{}
        , _selectedClip{}
        , _bClipLoop{ true }
        , _bClipNamesLoaded{ false }
        , _bKindTableReady{ false }
        , _bDatabaseLoaded{ false }
        , _bHasFit{ false }
        , _bExternalChange{ false }
        , _preview{}
        , _previewReport{}
        , _previewRevision{ 0 }
        , _bPreviewValid{ false }
        , _packRoot{}
        , _packDataPath{}
        , _packDataMessage{}
        , _previewPresetID{}
        , _previewBody{}
        , _previewBodyRevision{ 0 }
        , _previewDatabaseRevision{ 0 }
        , _bPreviewBodyDressed{ false }
        , _bModelsWereDirty{ false }
        , _bCustomizationActive{ false }
        , _seenFitRevision{ 0 }
        , _gizmo{}
        , _boneGizmoTarget{}
        , _placedBoneWorld{}
        , _bBoneGizmoPlaced{ false }
        , _rigPath{}
        , _bRigPreview{ false }
        , _packDataState{ PackDataState::Unread }
    {
        _presetStore.setDatabase( &_database );
    }

    void CharacterEditorPanel::drawDocumentContent()
    {
        ensurePackData();
        beginPreviewFrame();
        updateFocusedDocument();
        if ( _packDataState == PackDataState::Ready )
            ensureDocumentLoaded(); // 팩 데이터가 경로 칸을 채운 뒤에 읽는다
        if ( _packDataState == PackDataState::Missing )
        {
            const string hint = "This game has no character editor data (" + _packDataPath + ")";
            EditorWidgets::drawEmptyHint( hint.c_str() );
            return;
        }
        if ( _packDataState == PackDataState::Invalid )
        {
            EditorWidgets::drawEmptyHint( _packDataMessage.c_str() );
            return;
        }
        ensureKindTable();
        if ( _reloadGuard.pollExternalChange( SharedAssetCache<SocketSet>::getReloadCount(), ImGui::GetTime() ) )
            _bExternalChange = true;
        // 고치지 않은 창은 바뀐 파일을 바로 모델에 다시 읽는다. 고친 것이 있으면 버릴지 사람이 정한다(아래 단추) — 말없이 덮지 않는다.
        if ( _bExternalChange && _path.empty() )
            _bExternalChange = false; // 열어 둔 소켓 파일이 없으면 반영할 모델이 없다
        const bool bCanAutoReload = _bExternalChange && _sockets.isDirty() == false;
        if ( bCanAutoReload && loadSocketFile() )
            _bExternalChange = false;
        drawFileRow();
        drawHistoryRow();
        if ( _status.empty() == false )
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "%s", _status.c_str() );
        if ( _bExternalChange )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getWarningColor() ), "A socket file was changed on disk while this window has unsaved edits." );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Reload from disk (discard edits)" ) && loadSocketFile() == false )
                SW_LOG_WARNING( "Character Editor: the socket file '%#' could not be reloaded - the edits stay", _path.c_str() ); // 까닭은 상태 줄에 남았다
        }
        ImGui::Separator();

        CharacterEditorPanelInternal::Published& published = CharacterEditorPanelInternal::getPublished();
        published._boneCount                               = _pose.getSkeleton().getBoneCount();
        published._socketCount                             = static_cast<uint32>( _sockets.getSockets().getSockets().size() );
        published._presetCount                             = static_cast<uint32>( _presetStore.getPresets().size() );
        uint32 equipped                                    = 0;
        for ( const AppearanceSlotRequest& request : _appearance.getSelection()._listSlot )
        {
            if ( request._itemID.empty() == false )
                ++equipped;
        }
        published._equippedSlotCount = equipped;

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
        bool bSaved = true;
        if ( _sockets.isDirty() )
        {
            const string writePath = ResourcePaths::getWritePath( string_view( _path.c_str() ) );
            if ( writePath.empty() || _sockets.saveToFile( writePath ) == false )
            {
                _status = "Socket file could not be saved";
                bSaved  = false;
            }
            else
            {
                _reloadGuard.onSaved( SharedAssetCache<SocketSet>::getReloadCount(), ImGui::GetTime() ); // 파일 감시가 곧 같은 파일을 다시 읽는다 — 그건 외부 변경이 아니다
                SW_LOG_INFO( "Socket file saved to %#", writePath.c_str() );
            }
        }
        if ( _pose.isDirty() )
        {
            const string writePath = ResourcePaths::getWritePath( string_view( _refPosePath.c_str() ) );
            if ( writePath.empty() || _pose.saveToFile( writePath ) == false )
            {
                _status = "Reference pose file could not be saved";
                bSaved  = false;
            }
            else
                SW_LOG_INFO( "Reference pose file saved to %#", writePath.c_str() );
        }
        if ( bSaved )
        {
            _status.clear();
            acknowledgePreviewEdits();
        }
        return bSaved;
    }

    void CharacterEditorPanel::syncDocumentDirty()
    {
        // 소켓 · 포즈 편집은 모델 안의 기록이고, 미리보기 씬 편집(인스펙터로 몸을 고친 것)은 창 스택이다 — 둘 다 문서의 미저장이다.
        // 모델이 되돌리기로 저장 상태에 돌아오면 dirty 를 내리고, 그 뒤 기반 클래스가 창 스택의 새 편집을 다시 올린다.
        const bool bModelsDirty = _sockets.isDirty() || _pose.isDirty();
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
            std::snprintf( _path.data(), _path.max_size(), "%s", string( focused ).c_str() );
        }
        if ( _path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( ResourcePaths::findLooseFile( string_view( _path.c_str() ) ).getDiskPath().empty() )
            return ToolAssetLoadResult::Missing; // 새 소켓 파일 — 저장하면 만든다
        return loadSocketFile() ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    bool CharacterEditorPanel::loadSocketFile()
    {
        ensureKindTable(); // 문서는 첫 그리기 전에 열릴 수 있다(시나리오 · 콘텐츠 브라우저) — 종류 표 없이 읽으면 모든 kind 가 모르는 종류다
        const string readPath = ResourcePaths::findLooseFile( string_view( _path.c_str() ) ).getDiskPath();
        if ( readPath.empty() || _sockets.loadFromFile( readPath ) == false )
        {
            _status = "Socket file could not be loaded (see log)";
            return false;
        }
        _status.clear();
        _selectedSocket  = hashed_string{};
        _bExternalChange = false;
        _reloadGuard.reset( SharedAssetCache<SocketSet>::getReloadCount() );
        return true;
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
        if ( _bDatabaseLoaded == false )
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
        if ( _rigPath.empty() == false )
        {
            ImGui::Checkbox( "Preview constraint rig", &_bRigPreview );
            EditorSelfTestMarks::note( "charactereditor.rigPreview" );
            if ( pMutableBody != nullptr )
                syncRigPreview( *pMutableBody );
        }
        const AppearanceSelection& selection = _appearance.getSelection();
        ImGui::TextDisabled( "Wearing %s on base preset '%s' (assembled %u times)", selection._listSlot.empty() ? "no slot items" : "the selection",
                             selection._basePresetID.empty() ? _previewPresetID.c_str() : selection._basePresetID.c_str(), pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u );
        CharacterEditorPanelInternal::getPublished()._previewAssembleCount = pAppearance != nullptr ? pAppearance->getAssembleCount() : 0u;
        const float2  available                                            = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight                                            = CharacterEditorPanelInternal::kPreviewHeight * EditorThemeUtil::getDpiScale();
        syncBoneGizmo();
        drawPreviewViewport( float2{ available._x, available._y > minHeight ? available._y : minHeight }, _gizmo.hasMouse() );
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
            setFitResult( *pAppearance->getLastFitResult() );
            _seenFitRevision = pAppearance->getFitResultRevision();
        }
        // 슬라이더는 뷰포트 아래에 둔다 — 모습이 바뀌는 것을 보며 끈다.
        if ( ImGui::CollapsingHeader( "Customize (release a slider to refit)", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawCustomizationSliders( selection );
        else
            _bCustomizationActive = false;
    }

    void CharacterEditorPanel::syncBoneGizmo()
    {
        GameObjectManager* pManager   = findPreviewManager();
        GameObject*        pBody      = pManager != nullptr ? pManager->resolveGameObject( _previewBody ) : nullptr;
        GameObject*        pTarget    = pManager != nullptr ? pManager->resolveGameObject( _boneGizmoTarget ) : nullptr;
        const Skeleton&    skeleton   = _pose.getSkeleton();
        const int32        boneIndex  = _selectedBone.empty() ? -1 : skeleton.findBoneIndex( _selectedBone );
        SceneComponent*    pBodyScene = pBody != nullptr ? pBody->getPrimarySceneComponent() : nullptr;
        if ( boneIndex < 0 || pBodyScene == nullptr )
        {
            if ( pTarget != nullptr )
                pManager->destroyObject( pTarget );
            _boneGizmoTarget                                              = GameObjectHandle{};
            _bBoneGizmoPlaced                                             = false;
            CharacterEditorPanelInternal::getPublished()._boneGizmoPlaced = 0;
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
            if ( _pose.getEffectiveTransform( skeleton.getBone( static_cast<uint32>( listChain[chainIndex - 1] ) )._name, local ) == false )
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
                _status = "The bone gizmo target could not be created";
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
            (void)_pose.setTranslation( bone, local._translation, true, error ); // 같은 값이면 false 이고 오류는 비어 있다
            (void)_pose.setRotationDegrees( bone, degrees, true, error );        // 같은 값이면 false 이고 오류는 비어 있다
            _status = error;
        }
        else if ( _bBoneGizmoPlaced && _gizmo.hasMouse() == false )
        {
            _pose.endContinuousEdit(); // 끌기가 끝났다 — 다음 편집은 새 되돌리기 단계
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
        _bBoneGizmoPlaced                                             = true;
        CharacterEditorPanelInternal::getPublished()._boneGizmoPlaced = 1;
    }

    void CharacterEditorPanel::drawPenetrationPoints( const GameObject& body )
    {
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        const SceneComponent*        pScene   = body.getPrimarySceneComponent();
        const bool                   bNothing = _bHasFit == false || _fit.hasPenetration() == false || pScene == nullptr;
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
        for ( const FitPenetrationSummary& penetration : _fit._listPenetration )
        {
            for ( const float3& point : penetration._listPoint )
            {
                const float3 center = float3::transform( point, world );
                renderer.drawLine( center - float3{ arm, 0.0f, 0.0f }, center + float3{ arm, 0.0f, 0.0f }, color );
                renderer.drawLine( center - float3{ 0.0f, arm, 0.0f }, center + float3{ 0.0f, arm, 0.0f }, color );
                renderer.drawLine( center - float3{ 0.0f, 0.0f, arm }, center + float3{ 0.0f, 0.0f, arm }, color );
            }
        }
        CharacterEditorPanelInternal::getPublished()._penetrationMarkCount = renderer.getDrawnSegmentCount() / 3;
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
                _status      = "The preview body could not take the constraint rig";
                _bRigPreview = false;
                return;
            }
        }
        if ( string_view( pRig->getRigPath() ) != string_view( _rigPath.c_str() ) )
            pRig->setRigPath( string_view( _rigPath.c_str() ) );
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
        const bool bResolved = table.addUnit( hashed_string{}, 0, _sockets.getSockets(), bindBones, nullptr, &resolveError );
        table.endResolve();
        if ( bResolved == false )
        {
            _status = resolveError; // 모르는 부모 본 같은 편집 오류 — 축을 그리지 않고 알린다
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
        for ( const SocketDef& socket : _sockets.getSockets().getSockets() )
        {
            const SocketID socketID = table.findSocket( socket._name );
            float4x4       unitTransform{};
            if ( socketID == kInvalidSocketID || table.computeUnitTransform( socketID, poseBones, unitTransform ) == false )
                continue;
            const float4x4   world     = unitTransform * bodyWorld;
            const bool       bSelected = socket._name == _selectedSocket;
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
                _status = "The preview body could not be created";
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
                _status = "The preview body could not take its components";
                return;
            }
            pAnimator->setClipFolder( string_view( _clipFolder.c_str() ) );
            _previewBody         = pBody->getHandle();
            _bPreviewBodyDressed = false;
        }
        CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
        const bool                    bChanged    = _bPreviewBodyDressed == false || _previewBodyRevision != _appearance.getRevision() || _previewDatabaseRevision != _database.getRevision();
        // 슬라이더를 끄는 동안은 다시 입히지 않는다 — 피팅이 무거워 놓을 때 한 번 푼다(첫 입히기는 기다리지 않는다).
        const bool bDeferred = _bCustomizationActive && _bPreviewBodyDressed;
        const bool bSkip     = pAppearance == nullptr || bChanged == false;
        if ( bSkip || bDeferred )
            return;
        // 기준 프리셋이 없는 선택은 팩 데이터의 미리보기 몸 위에 입힌다(데이터가 정한 몸 — 코드에 몸을 두지 않는다).
        AppearanceSelection selection = _appearance.getSelection();
        if ( selection._basePresetID.empty() )
            selection._basePresetID = _previewPresetID;
        pAppearance->previewSelection( selection, _database );
        _previewBodyRevision     = _appearance.getRevision();
        _previewDatabaseRevision = _database.getRevision();
        _bPreviewBodyDressed     = true;
    }

    void CharacterEditorPanel::loadClipNames()
    {
        _listClipName.clear();
        _bClipNamesLoaded = true;
        if ( _clipFolder.empty() )
            return;
        // 클립 목록은 디스크의 폴더를 훑어 얻는다 — 에디터는 원본 트리에서 도는 도구다(배포 팩에는 폴더 목록이 없어 목록이 빈다).
        const string   diskFolder = ResourcePaths::makeAbsolutePath( string_view( _clipFolder.c_str() ) );
        vector<string> listFile;
        if ( diskFolder.empty() || FileUtil::collectFiles( diskFolder, string( AnimationClip::kExtension ), listFile, false ) == false )
            return;
        const size_t extensionLength = AnimationClip::kExtension.size();
        for ( const string& filePath : listFile )
        {
            const string fileName = FileUtil::getFileNamePart( filePath );
            if ( fileName.size() > extensionLength )
                _listClipName.push_back( hashed_string( string_view( fileName ).substr( 0, fileName.size() - extensionLength ) ) );
        }
        std::sort( _listClipName.begin(), _listClipName.end(), []( const hashed_string& left, const hashed_string& right )
        { return string_view( left.c_str() ) < string_view( right.c_str() ); } );
    }

    void CharacterEditorPanel::drawAnimationControls( SkeletalAnimatorComponent& animator )
    {
        if ( _bClipNamesLoaded == false )
            loadClipNames();
        if ( _listClipName.empty() )
        {
            ImGui::TextDisabled( "No animation clips in '%s'", _clipFolder.c_str() );
            return;
        }
        ImGui::SetNextItemWidth( 240.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::BeginCombo( "##PreviewClip", _selectedClip.empty() ? "(select a clip)" : _selectedClip.c_str() ) )
        {
            bool bFirst = true;
            for ( const hashed_string& name : _listClipName )
            {
                if ( ImGui::Selectable( name.c_str(), name == _selectedClip ) )
                    _selectedClip = name;
                if ( bFirst )
                    EditorSelfTestMarks::note( "charactereditor.clip.first" ); // 자동화가 첫 클립을 누를 자리
                bFirst = false;
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( "charactereditor.clip" );
        ImGui::SameLine();
        ImGui::Checkbox( "Loop##PreviewClip", &_bClipLoop );
        ImGui::SameLine();
        ImGui::BeginDisabled( _selectedClip.empty() );
        if ( ImGui::Button( "Play##PreviewClip" ) )
        {
            if ( animator.preloadClip( _selectedClip ) && animator.play( _selectedClip, _bClipLoop, 0.0f ) )
                _status.clear();
            else
                _status = "The clip could not be played (see log)";
        }
        EditorSelfTestMarks::note( "charactereditor.playClip" );
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::Button( "Stop##PreviewClip" ) )
            animator.stop();
        const bool bPlaying                                       = animator.getCurrentStateName() == _selectedClip && _selectedClip.empty() == false && animator.isAnimationActive();
        CharacterEditorPanelInternal::getPublished()._clipPlaying = bPlaying ? 1u : 0u;
    }

    GameObjectManager* CharacterEditorPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void CharacterEditorPanel::revertDocument()
    {
        if ( _sockets.isDirty() && _sockets.revertToSaved() == false )
            SW_LOG_ERROR( "Character Editor: the socket edits could not be discarded - the saved state did not read back" );
        if ( _pose.isDirty() && _pose.revertToSaved() == false )
            SW_LOG_ERROR( "Character Editor: the reference pose edits could not be discarded - the saved state did not read back" );
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorPanel::setFitResult( const FitResult& result )
    {
        _fit     = FitCheckSummary::fromResult( result );
        _bHasFit = true;
    }

    void CharacterEditorPanel::drawFileRow()
    {
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##SocketPath", _path.data(), _path.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load" ) )
            reloadDocument(); // 경로 칸의 소켓 파일을 다시 읽는다 — 읽음 · 못 읽음 표시와 미저장 기준은 문서 계약이 맞춘다
        EditorSelfTestMarks::note( "charactereditor.loadSockets" );
        ImGui::SameLine();
        ImGui::BeginDisabled( canSaveDocument() == false );
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( ImGui::Button( "New" ) )
        {
            _sockets.clear();
            _selectedSocket = hashed_string{};
            _status.clear();
        }
        if ( canSaveDocument() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(modified)" );
        }
    }

    void CharacterEditorPanel::drawHistoryRow()
    {
        ImGui::BeginDisabled( _sockets.canUndo() == false );
        if ( ImGui::Button( "Undo" ) )
            (void)_sockets.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _sockets.canRedo() == false );
        if ( ImGui::Button( "Redo" ) )
            (void)_sockets.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::drawSocketTab()
    {
        drawSocketList();
        const SocketDef* pSelected = _sockets.findSocket( _selectedSocket );
        if ( pSelected != nullptr )
        {
            ImGui::Separator();
            const SocketDef selected = *pSelected; // 편집이 표를 바꾸므로 복사본으로 그린다
            drawSocketDetail( selected );
        }

        ImGui::Separator();
        vector<string> listIssue;
        if ( _sockets.validate( listIssue ) == false )
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
        ImGui::SameLine();
        if ( ImGui::Button( "Add" ) )
        {
            const hashed_string name   = CharacterEditorPanelInternal::makeName( _newName.c_str() );
            const bool          bAdded = _sockets.addSocket( name, hashed_string{}, hashed_string{}, _status );
            reportEdit( bAdded );
            if ( bAdded )
                _selectedSocket = name;
        }

        if ( ImGui::BeginChild( "##SocketList", ImVec2( 0.0f, CharacterEditorPanelInternal::kSocketListHeight ), ImGuiChildFlags_Borders ) )
        {
            bool bFirst = true;
            for ( const SocketDef& socket : _sockets.getSockets().getSockets() )
            {
                const bool bSelected = socket._name == _selectedSocket;
                if ( ImGui::Selectable( socket._name.c_str(), bSelected ) )
                {
                    _selectedSocket = socket._name;
                    std::snprintf( _renameBuffer.data(), _renameBuffer.max_size(), "%s", socket._name.c_str() );
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
        const hashed_string name = socket._name;

        ImGui::SetNextItemWidth( 180.0f );
        ImGui::InputText( "##Rename", _renameBuffer.data(), _renameBuffer.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Rename" ) )
        {
            const hashed_string newName = CharacterEditorPanelInternal::makeName( _renameBuffer.c_str() );
            const bool          bDone   = _sockets.renameSocket( name, newName, _status );
            reportEdit( bDone );
            if ( bDone )
                _selectedSocket = newName;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Mirror" ) )
        {
            hashed_string mirrorName;
            const bool    bDone = _sockets.mirrorSocket( name, mirrorName, _status );
            reportEdit( bDone );
            if ( bDone )
                _selectedSocket = mirrorName;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Remove" ) )
        {
            reportEdit( _sockets.removeSocket( name, _status ) );
            _selectedSocket = hashed_string{};
            return;
        }

        utf8 arrParentBuffer[constant::kMaxBuffer128];
        std::snprintf( arrParentBuffer, sizeof( arrParentBuffer ), "%s", socket._parent.c_str() );
        if ( ImGui::InputText( "Parent", arrParentBuffer, sizeof( arrParentBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            reportEdit( _sockets.setParent( name, CharacterEditorPanelInternal::makeName( arrParentBuffer ), _status ) );

        hashed_string pickedKind;
        if ( CharacterEditorPanelInternal::drawNameCombo( "Kind", socket._kind, _sockets.getKindTable().getKinds(), pickedKind ) )
            reportEdit( _sockets.setKind( name, pickedKind, _status ) );

        float3 translation = socket._translation;
        if ( ImGui::DragFloat3( "Translation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            reportEdit( _sockets.setTranslation( name, translation, true, _status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _sockets.endContinuousEdit();

        float3 degrees = socket._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        if ( ImGui::DragFloat3( "Rotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            reportEdit( _sockets.setRotationDegrees( name, degrees, true, _status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _sockets.endContinuousEdit();

        float3 scale = socket._scale;
        if ( ImGui::DragFloat3( "Scale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            reportEdit( _sockets.setScale( name, scale, true, _status ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _sockets.endContinuousEdit();

        int32 anchorIndex = socket._anchor == SocketAnchor::Surface ? 1 : 0;
        if ( ImGui::Combo( "Anchor", &anchorIndex, "Bone\0Surface\0" ) )
            reportEdit( _sockets.setAnchor( name, anchorIndex == 1 ? SocketAnchor::Surface : SocketAnchor::Bone, _status ) );

        utf8 arrPreviewBuffer[constant::kMaxPathSize];
        std::snprintf( arrPreviewBuffer, sizeof( arrPreviewBuffer ), "%s", socket._previewMesh.c_str() );
        if ( ImGui::InputText( "Preview mesh", arrPreviewBuffer, sizeof( arrPreviewBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            reportEdit( _sockets.setPreviewMesh( name, arrPreviewBuffer, _status ) );
    }

    void CharacterEditorPanel::drawSkeletonTab()
    {
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##SkeletonPath", _skeletonPath.data(), _skeletonPath.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load skeleton" ) )
            loadSkeleton();
        EditorSelfTestMarks::note( "charactereditor.loadSkeleton" );

        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##RefPosePath", _refPosePath.data(), _refPosePath.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load pose" ) )
        {
            const string readPath = ResourcePaths::findLooseFile( string_view( _refPosePath.c_str() ) ).getDiskPath();
            if ( readPath.empty() )
                _status = "Reference pose file was not found - edit bones and Save to create it";
            else if ( _pose.loadFromFile( readPath ) == false )
                _status = "Reference pose file could not be loaded (see log)";
            else
                _status.clear();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( _pose.isDirty() == false );
        if ( ImGui::Button( "Save pose" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다 — 실패 이유는 상태 줄에 남는다
        ImGui::EndDisabled();

        ImGui::BeginDisabled( _pose.canUndo() == false );
        if ( ImGui::Button( "Undo pose" ) )
            (void)_pose.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _pose.canRedo() == false );
        if ( ImGui::Button( "Redo pose" ) )
            (void)_pose.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "%u bones, %u overridden%s", _pose.getSkeleton().getBoneCount(), static_cast<uint32>( _pose.getOverride().getOverrides().size() ),
                             _pose.isDirty() ? " (modified)" : "" );

        ImGui::Separator();
        if ( _pose.getSkeleton().getBoneCount() == 0 )
        {
            ImGui::TextDisabled( "Load a skeleton to see its bones. The skeleton is read-only; edits go to the reference pose file." );
            return;
        }
        if ( ImGui::BeginChild( "##BoneTree", ImVec2( 0.0f, CharacterEditorPanelInternal::kBoneTreeHeight ), ImGuiChildFlags_Borders ) )
        {
            vector<uint32> listRoot;
            _pose.collectChildren( -1, listRoot );
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
        const hashed_string bone = _pose.getSkeleton().getBone( boneIndex )._name;
        vector<uint32>      listChild;
        _pose.collectChildren( static_cast<int32>( boneIndex ), listChild );

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if ( listChild.empty() )
            flags |= ImGuiTreeNodeFlags_Leaf;
        if ( bone == _selectedBone )
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool bOverridden = _pose.hasOverride( bone );
        ImGui::PushID( static_cast<int32>( boneIndex ) );
        const bool bOpen = ImGui::TreeNodeEx( bone.c_str(), flags, "%s%s", bone.c_str(), bOverridden ? " *" : "" );
        if ( ImGui::IsItemClicked() )
            _selectedBone = bone;
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
        BoneTransform current;
        if ( _selectedBone.empty() || _pose.getEffectiveTransform( _selectedBone, current ) == false )
        {
            ImGui::TextDisabled( "Select a bone to edit its reference pose." );
            return;
        }
        ImGui::TextUnformatted( _selectedBone.c_str() );
        string error;

        float3 translation = current._translation;
        if ( ImGui::DragFloat3( "Bone translation", &translation._x, CharacterEditorPanelInternal::kDragSpeed ) )
            reportEdit( _pose.setTranslation( _selectedBone, translation, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _pose.endContinuousEdit();
        EditorSelfTestMarks::note( "charactereditor.bone.translation" );

        float3 degrees = current._rotation.getEulerAngles() * MathUtil::kRadianToDegree;
        if ( ImGui::DragFloat3( "Bone rotation", &degrees._x, CharacterEditorPanelInternal::kRotationDragSpeed ) )
            reportEdit( _pose.setRotationDegrees( _selectedBone, degrees, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _pose.endContinuousEdit();

        float3 scale = current._scale;
        if ( ImGui::DragFloat3( "Bone scale", &scale._x, CharacterEditorPanelInternal::kDragSpeed ) )
            reportEdit( _pose.setScale( _selectedBone, scale, true, error ) || error.empty() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            _pose.endContinuousEdit();

        ImGui::BeginDisabled( _pose.hasOverride( _selectedBone ) == false );
        if ( ImGui::Button( "Reset bone" ) )
            reportEdit( _pose.clearBone( _selectedBone, error ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Mirror bone" ) )
        {
            hashed_string mirrored;
            if ( _pose.mirrorBone( _selectedBone, _sockets.getMirrorRules(), mirrored, error ) )
                _selectedBone = mirrored;
        }
        ImGui::EndDisabled();
        if ( error.empty() == false )
            _status = error;
    }

    void CharacterEditorPanel::drawAppearanceTab()
    {
        drawAppearanceDataRow();
        // 데이터 프리셋 내보내기는 데이터 줄 바로 아래 — 씬이 쓰는 결과물이라 창을 열면 보이는 자리에 둔다.
        drawDataPresetRow();
        ImGui::Separator();
        ImGui::TextDisabled( "Selection (base preset, body, slots, customization values)" );
        const AppearanceSelection selection = _appearance.getSelection();
        drawSelectionHeader( selection );
        ImGui::Separator();
        drawSlotRows( selection );
        ImGui::Separator();
        drawCustomizationRows( selection );

        ImGui::BeginDisabled( _appearance.canUndo() == false );
        if ( ImGui::Button( "Undo appearance" ) )
            (void)_appearance.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _appearance.canRedo() == false );
        if ( ImGui::Button( "Redo appearance" ) )
            (void)_appearance.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();

        ImGui::Separator();
        drawExplanation();
        drawFitCheck();
        drawPresetRow();
    }

    void CharacterEditorPanel::drawAppearanceDataRow()
    {
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##ItemsPath", _itemsPath.data(), _itemsPath.max_size() );
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##AppearanceFolder", _appearanceFolder.data(), _appearanceFolder.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load data" ) )
            loadAppearanceData();
        EditorSelfTestMarks::note( "charactereditor.loadData" );
        if ( _bDatabaseLoaded )
            ImGui::TextDisabled( "%u slots, %u sets, %u presets", static_cast<uint32>( _database.getSlotTable().getSlots().size() ), static_cast<uint32>( _database.getSets().getSets().size() ),
                                 static_cast<uint32>( _database.getPresets().getPresets().size() ) );
        else
            ImGui::TextDisabled( "Load the items file and the appearance folder to pick items and see rule explanations." );
    }

    void CharacterEditorPanel::drawSelectionHeader( const AppearanceSelection& selection )
    {
        utf8 arrTextBuffer[constant::kMaxBuffer128];
        if ( _bDatabaseLoaded )
        {
            vector<hashed_string> listPreset;
            for ( const CharacterAppearanceDef& preset : _database.getPresets().getPresets() )
            {
                listPreset.push_back( preset._id );
            }
            hashed_string pickedPreset;
            const bool    bPresetPicked = CharacterEditorPanelInternal::drawNameCombo( "Base preset", selection._basePresetID, listPreset, pickedPreset, "charactereditor.basePreset.first" );
            EditorSelfTestMarks::note( "charactereditor.basePreset" );
            if ( bPresetPicked )
                (void)_appearance.setBasePreset( pickedPreset ); // 같은 값이면 false 이고 그대로 둔다
        }
        else
        {
            std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._basePresetID.c_str() );
            if ( ImGui::InputText( "Base preset", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
                (void)_appearance.setBasePreset( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        }
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._bodyType.c_str() );
        if ( ImGui::InputText( "Body type", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setBodyType( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._bodyShape.c_str() );
        if ( ImGui::InputText( "Body shape", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setBodyShape( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._face.c_str() );
        if ( ImGui::InputText( "Face", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setFace( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
    }

    void CharacterEditorPanel::drawSlotRows( const AppearanceSelection& selection )
    {
        ImGui::TextUnformatted( "Slots" );
        if ( _bDatabaseLoaded == false )
        {
            utf8 arrTextBuffer[constant::kMaxBuffer128];
            for ( const AppearanceSlotRequest& request : selection._listSlot )
            {
                ImGui::PushID( request._slot.c_str() );
                std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", request._itemID.c_str() );
                if ( ImGui::InputText( request._slot.c_str(), arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
                    (void)_appearance.setSlotItem( request._slot, CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
                ImGui::PopID();
            }
            ImGui::InputText( "##NewSlot", _newName.data(), _newName.max_size() );
            ImGui::SameLine();
            if ( ImGui::Button( "Add slot" ) )
                (void)_appearance.setSlotItem( CharacterEditorPanelInternal::makeName( _newName.c_str() ), hashed_string{} ); // 이미 있으면 false 이고 그대로 둔다
            return;
        }

        // 세트 고르기는 칸 목록 위에 둔다 — 패널이 낮아도 보이게.
        vector<hashed_string> listSet;
        for ( const EquipSetDef& set : _database.getSets().getSets() )
        {
            listSet.push_back( set._id );
        }
        hashed_string pickedSet;
        const bool    bSetPicked = CharacterEditorPanelInternal::drawNameCombo( "Equip set", hashed_string{}, listSet, pickedSet, "charactereditor.equipSet.first" );
        EditorSelfTestMarks::note( "charactereditor.equipSet" );
        if ( bSetPicked && pickedSet.empty() == false )
            (void)_appearance.equipSet( _database, pickedSet ); // 이미 입고 있으면 false 이고 그대로 둔다

        // 데이터가 있으면 슬롯 표의 칸마다 그 칸에 낄 수 있는 아이템만 고르게 한다.
        vector<hashed_string> listItem;
        bool                  bFirstSlot = true;
        for ( const AppearanceSlotDef& def : _database.getSlotTable().getSlots() )
        {
            AppearanceEditModel::collectSlotItems( _database, def._name, listItem );
            hashed_string picked;
            ImGui::PushID( def._name.c_str() );
            if ( CharacterEditorPanelInternal::drawNameCombo( def._name.c_str(), _appearance.findSlotItem( def._name ), listItem, picked ) )
                (void)_appearance.setSlotItem( def._name, picked );
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
        ImGui::TextUnformatted( "Customization values" );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            float32 number = value._number._x;
            if ( ImGui::DragFloat( value._parameter.c_str(), &number, CharacterEditorPanelInternal::kDragSpeed ) )
                (void)_appearance.setNumber( value._parameter, number, true ); // 같은 값이면 false 이고 그대로 둔다
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _appearance.endContinuousEdit();
        }
        ImGui::TextDisabled( "Sliders, colours and choices of the schema are in the Preview tab (the body updates when you release a slider)." );
    }

    void CharacterEditorPanel::drawCustomizationSliders( const AppearanceSelection& selection )
    {
        // 스키마는 선택의 것, 없으면 기준 프리셋(없으면 미리보기 몸 프리셋)의 것이다.
        hashed_string schemaID = selection._schema;
        if ( schemaID.empty() && _bDatabaseLoaded )
        {
            const CharacterAppearanceDef* pPreset = _database.getPresets().findPreset( selection._basePresetID.empty() ? _previewPresetID : selection._basePresetID );
            if ( pPreset != nullptr )
                schemaID = pPreset->_schema;
        }
        const CustomizationSchemaDef* pSchema = _bDatabaseLoaded && schemaID.empty() == false ? _database.getSchemas().findSchema( schemaID ) : nullptr;
        if ( pSchema == nullptr )
        {
            _bCustomizationActive = false;
            ImGui::TextDisabled( "No customization schema for this selection (load the appearance data and pick a base preset with a schema)." );
            return;
        }
        CustomizationValueSet normalized;
        CustomizationUtil::normalize( *pSchema, selection._customization, normalized, nullptr );
        bool          bAnyActive = false;
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
                        (void)_appearance.setNumber( param._name, number, true ); // 같은 값이면 false 이고 그대로 둔다
                        // 대칭 그룹(좌우 귀)의 짝은 같은 값으로 함께 바뀐다 — 자기 범위로 자른다.
                        for ( const CustomizationParamDef& other : pSchema->_listParameter )
                        {
                            if ( CharacterEditorPanelInternal::isSymmetryPartner( param, other ) )
                                (void)_appearance.setNumber( other._name, MathUtil::clamp( number, other._min, other._max ), true );
                        }
                    }
                    bAnyActive = bAnyActive || ImGui::IsItemActive();
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        _appearance.endContinuousEdit();
                    EditorSelfTestMarks::note( "charactereditor.slider.last" ); // 마지막으로 그린 슬라이더 — 자동화가 누를 자리
                    break;
                }
                case CustomizationKind::Color:
                {
                    const float4 current     = pValue != nullptr ? pValue->_number : param._defaultColor;
                    float32      arrColor[4] = { current._x, current._y, current._z, current._w };
                    if ( ImGui::ColorEdit4( param._name.c_str(), arrColor ) )
                    {
                        const float4 edited{ arrColor[0], arrColor[1], arrColor[2], arrColor[3] };
                        (void)_appearance.setColor( param._name, edited, true ); // 같은 값이면 false 이고 그대로 둔다
                        for ( const CustomizationParamDef& other : pSchema->_listParameter )
                        {
                            if ( CharacterEditorPanelInternal::isSymmetryPartner( param, other ) )
                                (void)_appearance.setColor( other._name, edited, true );
                        }
                    }
                    bAnyActive = bAnyActive || ImGui::IsItemActive();
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        _appearance.endContinuousEdit();
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
                                (void)_appearance.setOption( param._name, option._name ); // 같은 값이면 false 이고 그대로 둔다
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
        const string name{ _presetName.c_str() };
        if ( _presetStore.findPresetByName( name ) == nullptr )
        {
            _status = "Save the preset first - a thumbnail belongs to a saved preset";
            return;
        }
        // 파일 이름에 쓸 수 없는 글자는 밑줄로 바꾼다(프리셋 이름은 저장소가 정리한 그대로 둔다).
        string safeName = name;
        for ( size_t index = 0; index < safeName.size(); ++index )
        {
            const utf8 character = safeName[index];
            const bool bAllowed  = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) || character == '_' || character == '-';
            if ( bAllowed == false )
                safeName[index] = '_';
        }
        const string thumbnailID = FileUtil::removeExtension( string_view( _presetPath.c_str() ) ) + "." + safeName + ".png";
        const string imagePath   = ResourcePaths::getWritePath( thumbnailID );
        if ( imagePath.empty() || getPreviewViewport().requestImageFile( imagePath ) == false )
        {
            _status = "The thumbnail needs a write path and the Preview tab drawn at least once";
            return;
        }
        const string presetFile = ResourcePaths::getWritePath( string_view( _presetPath.c_str() ) );
        const bool   bAttached  = presetFile.empty() == false && _presetStore.setThumbnailPath( name, thumbnailID );
        if ( bAttached == false || _appearance.saveToPresetFile( _presetStore, name, presetFile ) == false )
        {
            _status = "The thumbnail was requested but the preset file could not be updated";
            return;
        }
        _status.clear();
        SW_LOG_INFO( "Character Editor: thumbnail of preset '%#' requested -> %#", name.c_str(), imagePath.c_str() );
    }

    void CharacterEditorPanel::drawExplanation()
    {
        CharacterEditorPanelInternal::Published& published = CharacterEditorPanelInternal::getPublished();
        published._traceCount                              = 0;
        if ( ImGui::CollapsingHeader( "Why does it look like this? (rules)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        if ( _bDatabaseLoaded == false )
        {
            ImGui::TextDisabled( "Load appearance data to see which rules hid or changed what." );
            return;
        }
        // 기준 프리셋이 칸 · 몸 종류의 시드라서, 없으면 펼칠 것이 없고 해석기가 칸마다 경고를 낸다 — 고르게 하고 넘어간다.
        if ( _database.getPresets().findPreset( _appearance.getSelection()._basePresetID ) == nullptr )
        {
            ImGui::TextDisabled( "Pick a base preset to see which rules hid or changed what." );
            return;
        }
        // 해석기는 경고를 로그로 남기므로 프레임마다 돌리지 않고 선택 · 데이터가 바뀔 때만 다시 푼다.
        const bool bStale = _bPreviewValid == false || _previewRevision != _appearance.getRevision();
        if ( bStale )
        {
            _preview       = ResolvedAppearance{};
            _previewReport = AppearanceSelectionReport{};
            _appearance.previewResolved( _database, _preview, _previewReport );
            _previewRevision = _appearance.getRevision();
            _bPreviewValid   = true;
        }
        const ResolvedAppearance&        resolved = _preview;
        const AppearanceSelectionReport& report   = _previewReport;
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
        if ( ImGui::BeginChild( "##RuleTrace", ImVec2( 0.0f, CharacterEditorPanelInternal::kExplanationListHeight ), ImGuiChildFlags_Borders ) )
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
        if ( _bHasFit == false )
        {
            ImGui::TextDisabled( "No fit result yet. The preview scene solves the fit when a slider is released." );
            return;
        }
        for ( const FitPartSummary& part : _fit._listPart )
        {
            ImGui::Text( "%s: cut %u/%u (%.0f%%), %u verts moved, max push %.3f", part._name.c_str(), part._cutTriangleCount, part._triangleCount, static_cast<float64>( part._cutRatio * 100.0f ),
                         part._movedVertexCount, static_cast<float64>( part._maxDelta ) );
        }
        if ( _fit.hasPenetration() == false )
            ImGui::TextDisabled( "No penetration." );
        for ( const FitPenetrationSummary& penetration : _fit._listPenetration )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "Penetration %s / %s: %u points, depth %.3f", penetration._partA.c_str(), penetration._partB.c_str(),
                                penetration._vertexCount, static_cast<float64>( penetration._maxDepth ) );
        }
        for ( const string& message : _fit._listMessage )
        {
            ImGui::TextWrapped( "%s", message.c_str() );
        }
    }

    void CharacterEditorPanel::drawPresetRow()
    {
        if ( ImGui::CollapsingHeader( "Presets (file)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##PresetPath", _presetPath.data(), _presetPath.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load presets" ) )
        {
            const string readPath = ResourcePaths::findLooseFile( string_view( _presetPath.c_str() ) ).getDiskPath();
            if ( readPath.empty() || AppearanceEditModel::loadPresetFile( _presetStore, readPath ) == false )
                _status = "Preset file could not be loaded (needs appearance data; see log)";
            else
                _status.clear();
        }
        ImGui::SetNextItemWidth( 180.0f );
        ImGui::InputText( "##PresetName", _presetName.data(), _presetName.max_size() );
        ImGui::SameLine();
        ImGui::BeginDisabled( _bDatabaseLoaded == false );
        if ( ImGui::Button( "Save as preset" ) )
        {
            const string writePath = ResourcePaths::getWritePath( string_view( _presetPath.c_str() ) );
            if ( writePath.empty() || _appearance.saveToPresetFile( _presetStore, string_view( _presetName.c_str() ), writePath ) == false )
                _status = "Preset could not be saved (needs a name and appearance data)";
            else
                _status.clear();
        }
        EditorSelfTestMarks::note( "charactereditor.savePreset" );
        ImGui::EndDisabled();
        if ( _appearance.isDirty() )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(unsaved)" );
        }
        ImGui::BeginDisabled( _bDatabaseLoaded == false );
        if ( ImGui::Button( "Save thumbnail" ) )
            saveThumbnail();
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "charactereditor.saveThumbnail" );
        EditorWidgets::drawTooltip( "Writes the Preview tab's picture next to the preset file and attaches it to the preset of this name (save the preset first; it is saved again with the current selection)" );

        if ( ImGui::BeginChild( "##PresetList", ImVec2( 0.0f, CharacterEditorPanelInternal::kPresetListHeight ), ImGuiChildFlags_Borders ) )
        {
            for ( const UserAppearancePreset& preset : _presetStore.getPresets() )
            {
                if ( ImGui::Selectable( preset._name.c_str() ) )
                {
                    (void)_appearance.loadFromPreset( _presetStore, string_view( preset._name ) ); // 목록의 이름이라 항상 있다
                    std::snprintf( _presetName.data(), _presetName.max_size(), "%s", preset._name.c_str() );
                }
            }
        }
        ImGui::EndChild();
    }

    void CharacterEditorPanel::drawDataPresetRow()
    {
        if ( ImGui::CollapsingHeader( "Data preset (scene use)", ImGuiTreeNodeFlags_DefaultOpen ) == false )
            return;
        ImGui::TextDisabled( "A scene's Character Appearance component names this id." );
        ImGui::SetNextItemWidth( -CharacterEditorPanelInternal::kPathButtonsWidth );
        ImGui::InputText( "##ExportFolder", _exportFolder.data(), _exportFolder.max_size() );
        EditorSelfTestMarks::note( "charactereditor.exportFolder" );
        ImGui::SetNextItemWidth( 180.0f );
        ImGui::InputText( "##ExportID", _exportID.data(), _exportID.max_size() );
        EditorSelfTestMarks::note( "charactereditor.exportID" );
        ImGui::SameLine();
        ImGui::BeginDisabled( _bDatabaseLoaded == false );
        if ( ImGui::Button( "Export data preset" ) )
            exportDataPreset();
        EditorSelfTestMarks::note( "charactereditor.exportPreset" );
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::exportDataPreset()
    {
        const hashed_string    presetID( string_view( _exportID.c_str() ) );
        CharacterAppearanceDef def;
        string                 reason;
        XMLDocument            doc;
        const bool             bMade = AppearanceSelectionUtil::makePresetDef( _database, _appearance.getSelection(), presetID, def, reason ) &&
                           CharacterAppearanceCatalog::writePresetToNode( def, doc.appendRoot( "CharacterAppearance" ), reason );
        if ( bMade == false )
        {
            _status = "Data preset could not be made - " + reason;
            return;
        }
        const string resourceID = FileUtil::joinPath( string_view( _exportFolder.c_str() ), string( presetID.c_str() ) + AppearanceDatabase::kPresetExtension );
        const string writePath  = ResourcePaths::getWritePath( resourceID );
        const bool   bWritten   = writePath.empty() == false && FileUtil::ensureParentDirectoryExists( writePath ) && doc.saveFile( writePath );
        if ( bWritten == false )
        {
            _status = "Data preset could not be written to " + resourceID;
            return;
        }
        // 이 창의 데이터와 실행 중인 게임의 데이터에 올린다 — 씬의 컴포넌트는 게임 서비스(`AppearanceDatabase`)에서 프리셋을 푼다.
        // 쓰기는 디스크 경로, 읽기는 리소스 id 다(리소스 입구는 OS 경로를 받지 않는다).
        if ( _database.loadPresetFile( resourceID ) == false )
        {
            _status = "Exported preset has errors - " + _database.getReport().joined();
            return;
        }
        AppearanceDatabase* pGameData = game::findSubsystem<AppearanceDatabase>();
        if ( pGameData == nullptr )
            SW_LOG_WARNING( "Exported appearance preset '%#' - this game has no AppearanceDatabase service, so scenes use it after the game loads it", presetID.c_str() );
        else if ( pGameData->loadPresetFile( resourceID ) == false )
        {
            _status = "The game's appearance data rejected the preset - " + pGameData->getReport().joined();
            return;
        }
        ++CharacterEditorPanelInternal::getPublished()._exportCount;
        _status.clear();
        SW_LOG_INFO( "Exported appearance preset '%#' to '%#'", presetID.c_str(), resourceID.c_str() );
    }

    void CharacterEditorPanel::loadSkeleton()
    {
        if ( _pose.loadSkeleton( string_view( _skeletonPath.c_str() ) ) == false )
        {
            _status = "Skeleton could not be loaded (see log)";
            return;
        }
        _status.clear();
        _selectedBone = hashed_string{};
        vector<hashed_string> listBoneName;
        _pose.collectBoneNames( listBoneName );
        _sockets.setBoneNames( listBoneName ); // 이제 소켓 부모 본 이름을 검증한다
    }

    void CharacterEditorPanel::loadAppearanceData()
    {
        _bDatabaseLoaded = false;
        _bPreviewValid   = false;
        _database.clear();
        _pItems = make_unique<ItemCatalog>();
        if ( _pItems->loadFromResource( string_view( _itemsPath.c_str() ) ) == false )
        {
            _status = "Items file could not be loaded (see log)";
            return;
        }
        if ( _database.loadFromFolder( string_view( _appearanceFolder.c_str() ), _pItems.get() ) == false )
        {
            _status = "Appearance data has errors - " + _database.getReport().joined();
            return;
        }
        _status.clear();
        _bDatabaseLoaded = true;
    }

    void CharacterEditorPanel::ensurePackData()
    {
        const string& packRoot = GameConfig::getActive()._packRoot.get();
        if ( _packDataState != PackDataState::Unread && packRoot == _packRoot )
            return;
        _packRoot = packRoot;
        _packDataMessage.clear();
        string error;
        if ( GamePackDataUtil::tryMakeKindPath( packRoot, CharacterEditorPackData::kDataKind, _packDataPath, error ) == false )
        {
            _packDataState   = PackDataState::Invalid;
            _packDataMessage = "Character editor data has no path: " + error;
            return;
        }
        if ( ResourceIO::hasResource( _packDataPath ) == false )
        {
            _packDataState = PackDataState::Missing;
            return;
        }
        CharacterEditorPackData data;
        if ( data.loadFromResource( _packDataPath ) == false )
        {
            _packDataState   = PackDataState::Invalid;
            _packDataMessage = _packDataPath + " could not be read (see log)";
            return;
        }
        _path             = data._socketPath;
        _skeletonPath     = data._skeletonPath;
        _refPosePath      = data._refPosePath;
        _itemsPath        = data._itemsPath;
        _appearanceFolder = data._appearanceFolder;
        _clipFolder       = data._clipFolder;
        _bClipNamesLoaded = false; // 팩 데이터가 바뀌면 클립 목록을 다시 모은다
        _selectedClip     = hashed_string{};
        _previewPresetID  = hashed_string( string_view( data._previewPresetID ) );
        _rigPath          = data._rigPath;
        _bRigPreview      = _bRigPreview && _rigPath.empty() == false; // 리그가 없는 게임으로 바뀌면 켜 둔 단추도 내린다
        // 프리셋 내보내기 폴더는 외형 폴더에서 파생한다(기준은 팩 데이터의 외형 폴더 하나).
        const string exportFolder = FileUtil::joinPath( data._appearanceFolder, AppearanceDatabase::kPresetFolder );
        std::snprintf( _exportFolder.data(), _exportFolder.max_size(), "%s", exportFolder.c_str() );
        _packDataState = PackDataState::Ready;
    }

    void CharacterEditorPanel::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = true;
        _reloadGuard.reset( SharedAssetCache<SocketSet>::getReloadCount() );
        SocketKindTable kinds;
        if ( kinds.loadFromResource( SocketKindTable::kDefaultPath ) == false )
            SW_LOG_WARNING( "Default socket kind table could not be loaded - kinds are not validated" );
        else
            _sockets.setKindTable( kinds );

        MirrorNameRules rules;
        if ( rules.loadFromResource( MirrorNameRules::kDefaultPath ) == false )
            SW_LOG_WARNING( "Default mirror name table could not be loaded - left/right pairs are not found" );
        else
            _sockets.setMirrorRules( rules );
    }

    void CharacterEditorPanel::reportEdit( bool bSucceeded )
    {
        if ( bSucceeded )
            _status.clear();
    }
} // namespace sw::editor
