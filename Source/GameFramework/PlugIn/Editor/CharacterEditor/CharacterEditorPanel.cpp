#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr const utf8* kDefaultPresetPath     = "editor/characterpresets.txt";
            static constexpr const utf8* kDefaultExportID       = "MyCharacter";
            static constexpr float32     kDragSpeed             = 0.005f;
            static constexpr float32     kRotationDragSpeed     = 0.5f;
            static constexpr float32     kPathButtonsWidth      = 240.0f;
            static constexpr float32     kSocketListHeight      = 140.0f;
            static constexpr float32     kBoneTreeHeight        = 200.0f;
            static constexpr float32     kPresetListHeight      = 90.0f;
            static constexpr float32     kExplanationListHeight = 130.0f;

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

            /** @brief 자동화 탐침이 읽는 값입니다. 패널이 그릴 때마다 게시합니다(게임 스레드). */
            struct Published
            {
                uint32 _boneCount{ 0 };
                uint32 _socketCount{ 0 };
                uint32 _equippedSlotCount{ 0 };
                uint32 _traceCount{ 0 };
                uint32 _presetCount{ 0 };
                uint32 _exportCount{ 0 }; ///< 이 실행에서 데이터 프리셋으로 내보낸 수
            };
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
        };
    } // namespace

    SW_EDITOR_PANEL( CharacterEditorPanel, "charactereditor.main", EditorPanelCategory::Custom, 9020 );

    SW_AUTOMATION_PROBE( editorCharacterEditorBoneCount, "Editor.CharacterEditorBoneCount", "Bones of the skeleton the Character Editor has loaded", &CharacterEditorPanelInternal::readBoneCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorSocketCount, "Editor.CharacterEditorSocketCount", "Sockets in the socket file the Character Editor has loaded", &CharacterEditorPanelInternal::readSocketCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorEquippedSlotCount, "Editor.CharacterEditorEquippedSlotCount", "Slots that hold an item in the appearance selection",
                         &CharacterEditorPanelInternal::readEquippedSlotCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorTraceCount, "Editor.CharacterEditorTraceCount", "Rule explanation lines for the current appearance selection",
                         &CharacterEditorPanelInternal::readTraceCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorExportCount, "Editor.CharacterEditorExportCount", "Data presets the Character Editor exported in this run",
                         &CharacterEditorPanelInternal::readExportCount );
    SW_AUTOMATION_PROBE( editorCharacterEditorPresetCount, "Editor.CharacterEditorPresetCount", "Presets in the Character Editor's preset store", &CharacterEditorPanelInternal::readPresetCount );
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorPanel::CharacterEditorPanel()
        : IEditorPanel( false )
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
        , _presetPath{ CharacterEditorPanelInternal::kDefaultPresetPath }
        , _presetName{ "MyLook" }
        , _exportFolder{}
        , _exportID{ CharacterEditorPanelInternal::kDefaultExportID }
        , _newName{}
        , _renameBuffer{}
        , _status{}
        , _selectedSocket{}
        , _selectedBone{}
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
        , _packDataState{ PackDataState::Unread }
    {
        _presetStore.setDatabase( &_database );
    }

    void CharacterEditorPanel::drawContent()
    {
        ensurePackData();
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
        if ( _reloadGuard.pollExternalChange( SocketSetCache::getReloadCount(), ImGui::GetTime() ) )
            _bExternalChange = true;
        drawFileRow();
        drawHistoryRow();
        if ( _status.empty() == false )
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "%s", _status.c_str() );
        if ( _bExternalChange )
            ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "A socket file was reloaded from disk by someone else. Press Load to refresh this window." );
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
                _reloadGuard.onSaved( SocketSetCache::getReloadCount(), ImGui::GetTime() ); // 파일 감시가 곧 같은 파일을 다시 읽는다 — 그건 외부 변경이 아니다
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
            _status.clear();
        return bSaved;
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
        {
            const string readPath = ResourcePaths::findLooseFile( string_view( _path.c_str() ) ).getDiskPath();
            if ( readPath.empty() || _sockets.loadFromFile( readPath ) == false )
                _status = "Socket file could not be loaded (see log)";
            else
            {
                _status.clear();
                _selectedSocket  = hashed_string{};
                _bExternalChange = false;
                _reloadGuard.reset( SocketSetCache::getReloadCount() );
            }
        }
        EditorSelfTestMarks::note( "charactereditor.loadSockets" );
        ImGui::SameLine();
        ImGui::BeginDisabled( canSaveDocument() == false );
        if ( ImGui::Button( "Save" ) )
            (void)saveDocument(); // 실패 이유는 상태 줄에 남는다
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
            (void)saveDocument(); // 실패 이유는 상태 줄에 남는다
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
        ImGui::TextDisabled( "Body and face sliders need the GPU morph pool (D4-8 integration)." );
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
        ImGui::TextDisabled( "Thumbnail render needs the preview scene." );

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
        if ( _database.loadPresetFile( writePath ) == false )
        {
            _status = "Exported preset has errors - " + _database.getReport().joined();
            return;
        }
        AppearanceDatabase* pGameData = game::findSubsystem<AppearanceDatabase>();
        if ( pGameData == nullptr )
            SW_LOG_WARNING( "Exported appearance preset '%#' - this game has no AppearanceDatabase service, so scenes use it after the game loads it", presetID.c_str() );
        else if ( pGameData->loadPresetFile( writePath ) == false )
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
        // 프리셋 내보내기 폴더는 외형 폴더에서 파생한다(정본은 팩 데이터의 외형 폴더 하나).
        const string exportFolder = FileUtil::joinPath( data._appearanceFolder, AppearanceDatabase::kPresetFolder );
        std::snprintf( _exportFolder.data(), _exportFolder.max_size(), "%s", exportFolder.c_str() );
        _packDataState = PackDataState::Ready;
    }

    void CharacterEditorPanel::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = true;
        _reloadGuard.reset( SocketSetCache::getReloadCount() );
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
