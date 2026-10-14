#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/CharacterEditorPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPanelManager.h"

#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPanelInternal
        {
            static constexpr const utf8* kDefaultSocketPath = "models/character.sockets.xml";
            static constexpr float32     kDragSpeed         = 0.005f;
            static constexpr float32     kRotationDragSpeed = 0.5f;

            /** @brief 입력칸의 글을 이름으로 바꿉니다. 비어 있으면 빈 이름입니다. */
            static hashed_string makeName( const utf8* pText )
            {
                return pText[0] == '\0' ? hashed_string{} : hashed_string( string_view( pText ) );
            }

            /** @brief 이름 고르기 콤보입니다. 바뀌었으면 true 이고 @p outName 에 고른 이름이 담깁니다. 첫 항목은 빈 이름입니다. */
            static bool drawNameCombo( const utf8* pLabel, const hashed_string& current, const vector<hashed_string>& listName, hashed_string& outName )
            {
                bool bChanged = false;
                if ( ImGui::BeginCombo( pLabel, current.empty() ? "(none)" : current.c_str() ) )
                {
                    if ( ImGui::Selectable( "(none)", current.empty() ) )
                    {
                        outName  = hashed_string{};
                        bChanged = true;
                    }
                    for ( const hashed_string& name : listName )
                    {
                        const bool bSelected = name == current;
                        if ( ImGui::Selectable( name.c_str(), bSelected ) )
                        {
                            outName  = name;
                            bChanged = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return bChanged;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( CharacterEditorPanel, "charactereditor.main", EditorPanelCategory::Custom, 9020 );
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorPanel::CharacterEditorPanel()
        : IEditorPanel( false )
        , _sockets{}
        , _appearance{}
        , _path{ CharacterEditorPanelInternal::kDefaultSocketPath }
        , _newName{}
        , _renameBuffer{}
        , _status{}
        , _selectedSocket{}
        , _bKindTableReady{ false }
    {
    }

    void CharacterEditorPanel::drawContent()
    {
        ensureKindTable();
        drawFileRow();
        drawHistoryRow();
        if ( _status.empty() == false )
            ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.35f, 1.0f ), "%s", _status.c_str() );
        ImGui::Separator();
        if ( ImGui::BeginTabBar( "##CharacterEditorTabs" ) )
        {
            if ( ImGui::BeginTabItem( "Sockets" ) )
            {
                drawSocketTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Appearance" ) )
            {
                drawAppearanceTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    bool CharacterEditorPanel::saveDocument()
    {
        const string writePath = ResourcePaths::getWritePath( string_view( _path.c_str() ) );
        if ( writePath.empty() || _sockets.saveToFile( writePath ) == false )
        {
            _status = "Socket file could not be saved";
            return false;
        }
        _status.clear();
        SW_LOG_INFO( "Socket file saved to %#", writePath.c_str() );
        return true;
    }

    void CharacterEditorPanel::drawFileRow()
    {
        ImGui::SetNextItemWidth( -240.0f );
        ImGui::InputText( "##SocketPath", _path.data(), _path.max_size() );
        ImGui::SameLine();
        if ( ImGui::Button( "Load" ) )
        {
            const string readPath = ResourcePaths::getResourcePath( string_view( _path.c_str() ) );
            if ( readPath.empty() || _sockets.loadFromFile( readPath ) == false )
                _status = "Socket file could not be loaded (see log)";
            else
            {
                _status.clear();
                _selectedSocket = hashed_string{};
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled( _sockets.isDirty() == false );
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
        if ( _sockets.isDirty() )
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

        if ( ImGui::BeginChild( "##SocketList", ImVec2( 0.0f, 140.0f ), ImGuiChildFlags_Borders ) )
        {
            for ( const SocketDef& socket : _sockets.getSockets().getSockets() )
            {
                const bool bSelected = socket._name == _selectedSocket;
                if ( ImGui::Selectable( socket._name.c_str(), bSelected ) )
                {
                    _selectedSocket = socket._name;
                    std::snprintf( _renameBuffer.data(), _renameBuffer.max_size(), "%s", socket._name.c_str() );
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

    void CharacterEditorPanel::drawAppearanceTab()
    {
        ImGui::TextDisabled( "Selection (base preset, body, slots, customization values)" );
        const AppearanceSelection selection = _appearance.getSelection();

        utf8 arrTextBuffer[constant::kMaxBuffer128];
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._basePresetID.c_str() );
        if ( ImGui::InputText( "Base preset", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setBasePreset( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._bodyType.c_str() );
        if ( ImGui::InputText( "Body type", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setBodyType( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._bodyShape.c_str() );
        if ( ImGui::InputText( "Body shape", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setBodyShape( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다
        std::snprintf( arrTextBuffer, sizeof( arrTextBuffer ), "%s", selection._face.c_str() );
        if ( ImGui::InputText( "Face", arrTextBuffer, sizeof( arrTextBuffer ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            (void)_appearance.setFace( CharacterEditorPanelInternal::makeName( arrTextBuffer ) ); // 같은 값이면 false 이고 그대로 둔다

        ImGui::Separator();
        ImGui::TextUnformatted( "Slots" );
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

        ImGui::Separator();
        ImGui::TextUnformatted( "Customization values" );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            float32 number = value._number._x;
            if ( ImGui::DragFloat( value._parameter.c_str(), &number, CharacterEditorPanelInternal::kDragSpeed ) )
                (void)_appearance.setNumber( value._parameter, number, true ); // 같은 값이면 false 이고 그대로 둔다
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                _appearance.endContinuousEdit();
        }

        ImGui::BeginDisabled( _appearance.canUndo() == false );
        if ( ImGui::Button( "Undo appearance" ) )
            (void)_appearance.undo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( _appearance.canRedo() == false );
        if ( ImGui::Button( "Redo appearance" ) )
            (void)_appearance.redo(); // 기록이 있을 때만 켜져 있다
        ImGui::EndDisabled();
    }

    void CharacterEditorPanel::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = true;
        SocketKindTable kinds;
        if ( kinds.loadFromResource( SocketKindTable::kDefaultPath ) == false )
        {
            SW_LOG_WARNING( "Default socket kind table could not be loaded - kinds are not validated" );
            return;
        }
        _sockets.setKindTable( kinds );
    }

    void CharacterEditorPanel::reportEdit( bool bSucceeded )
    {
        if ( bSucceeded )
            _status.clear();
    }
} // namespace sw::editor
