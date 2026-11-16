#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsTableView.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsJSONDocument.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ProjectSettingsTableViewInternal
        {
            /** @brief 이 글자 수를 넘거나 줄바꿈이 든 글은 여러 줄 칸으로 보인다(표의 `_comment` · 사유 글). */
            static constexpr size_t kSingleLineLimit = 80;
            /** @brief 여러 줄 칸의 한 줄 글자 수 어림과 줄 수 범위입니다. */
            static constexpr size_t kCharactersPerLine = 70;
            static constexpr uint32 kMinLines          = 2;
            static constexpr uint32 kMaxLines          = 6;

            static string makeChildPath( const string& path, const string& part ) { return path.empty() ? part : string( path + "." + part ); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProjectSettingsTableView::ProjectSettingsTableView()
        : _draftText{}
        , _pendingKey{}
        , _status{}
        , _draftNode{ kNoNode }
        , _pendingNode{ kNoNode }
        , _pendingIndex{ 0 }
        , _mapNewKeyText{}
        , _pendingEdit{ PendingEdit::None }
        , _bEdited{ 0 }
    {
    }

    void ProjectSettingsTableView::resetDraft()
    {
        _draftNode   = kNoNode;
        _draftText   = {};
        _pendingEdit = PendingEdit::None;
        _mapNewKeyText.clear();
    }

    bool ProjectSettingsTableView::draw( ProjectSettingsJSONDocument& document, string& outStatus )
    {
        _bEdited = 0;
        _status  = {};
        if ( document.isOpen() == false )
            return false;
        ImGui::BeginDisabled( document.isReadOnly() );
        const ProjectSettingsJSONNode& root = document.getNode( ProjectSettingsJSONDocument::kRootNode );
        // 루트 객체는 머리 행 없이 멤버를 바로 보인다(파일 하나가 곧 페이지다). 루트가 배열 · 말단이면 한 행으로 그린다.
        if ( root._type == JSONType::Object )
        {
            for ( size_t index = 0; index < root._listChild.size(); ++index )
            {
                const uint32 child = root._listChild[index];
                drawNode( document, child, document.getNode( child )._key, document.getNode( child )._key, ProjectSettingsJSONDocument::kRootNode, index );
            }
        }
        else
        {
            drawNode( document, ProjectSettingsJSONDocument::kRootNode, "(root)", {}, kNoNode, 0 );
        }
        ImGui::EndDisabled();
        const bool bApplied = applyPendingEdit( document );
        if ( _status.empty() == false )
            outStatus = _status;
        return bApplied || _bEdited != 0;
    }

    void ProjectSettingsTableView::drawNode( ProjectSettingsJSONDocument& document, uint32 node, const string& label, const string& path, uint32 parentNode, size_t childIndex )
    {
        using Internal                       = ProjectSettingsTableViewInternal;
        const ProjectSettingsJSONNode& value = document.getNode( node );
        ImGui::PushID( static_cast<int32>( node ) );
        if ( value._type != JSONType::Array && value._type != JSONType::Object )
        {
            EditorWidgets::drawPropertyRowLabel( label.c_str(), path.c_str() );
            drawRemoveButton( document, parentNode, childIndex, path );
            drawLeaf( document, node, path );
            ImGui::PopID();
            return;
        }

        const bool bOpen = ImGui::TreeNodeEx( "##node", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label.c_str() );
        EditorSelfTestMarks::record( ( "projectsettings.table." + path ).c_str() );
        ImGui::SameLine();
        fixed_string<constant::buffer::kMax32> countText;
        formatstring( countText.data(), countText.capacity(), value._type == JSONType::Array ? "%# items" : "%# keys", value._listChild.size() );
        EditorThemeUtil::textMuted( countText.c_str() );
        ImGui::SameLine();
        drawRemoveButton( document, parentNode, childIndex, path );
        ImGui::NewLine();
        if ( bOpen == false )
        {
            ImGui::PopID();
            return;
        }
        if ( value._type == JSONType::Array )
        {
            if ( ImGui::SmallButton( "+ Add" ) )
            {
                _pendingEdit = PendingEdit::AddItem;
                _pendingNode = node;
            }
            EditorSelfTestMarks::record( ( "projectsettings.table." + path + ".add" ).c_str() );
        }
        else
        {
            // 맵 키는 칸에 치고 Enter 로 더한다(프로퍼티 그리드의 맵 더하기 행과 같다). 값은 빈 글이다 — 형식이 다르면 파일을 고친 뒤 되돌리기로 다시 읽는다.
            fixed_string<constant::buffer::kMax256>& newKey = _mapNewKeyText[node];
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( EditorWidgets::inputTextWithHint( SW_WIDGET_ID( "##addkey" ), "New key, then Enter", newKey.data(), newKey.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) && newKey.empty() == false )
            {
                _pendingEdit = PendingEdit::AddMember;
                _pendingNode = node;
                _pendingKey  = newKey.c_str();
                newKey.clear();
            }
            EditorSelfTestMarks::record( ( "projectsettings.table." + path + ".add" ).c_str() );
        }
        for ( size_t index = 0; index < value._listChild.size(); ++index )
        {
            const uint32                   child      = value._listChild[index];
            const ProjectSettingsJSONNode& childValue = document.getNode( child );
            const string                   childPart  = value._type == JSONType::Array ? to_string( static_cast<uint64>( index ) ) : childValue._key;
            const string                   childLabel = value._type == JSONType::Array ? string( "[" + childPart + "]" ) : childPart;
            drawNode( document, child, childLabel, Internal::makeChildPath( path, childPart ), node, index );
        }
        ImGui::TreePop();
        ImGui::PopID();
    }

    void ProjectSettingsTableView::drawLeaf( ProjectSettingsJSONDocument& document, uint32 node, const string& path )
    {
        using Internal                       = ProjectSettingsTableViewInternal;
        const ProjectSettingsJSONNode& value = document.getNode( node );
        const string                   mark  = "projectsettings.table." + path;
        switch ( value._type )
        {
            case JSONType::String:
            {
                string     text      = value._text;
                const bool bLong     = text.size() > Internal::kSingleLineLimit || StringUtil::contains( text, "\n" );
                const auto lineCount = MathUtil::clamp( static_cast<uint32>( text.size() / Internal::kCharactersPerLine ) + 1u, Internal::kMinLines, Internal::kMaxLines );
                const bool bChanged  = bLong ? EditorWidgets::drawTextFieldMultiline( "##value", text, lineCount ) : EditorWidgets::drawTextField( "##value", text, -1.0f );
                if ( bChanged && document.setLeafText( node, text ) )
                    _bEdited = 1;
                break;
            }
            case JSONType::Number:
            {
                // 고치는 동안은 초안에 둔다 — 칸을 떠날 때 JSON 수인지 보고 넣는다.
                string     shown    = _draftNode == node ? _draftText : value._text;
                const bool bChanged = EditorWidgets::drawTextField( "##value", shown, -1.0f );
                if ( ImGui::IsItemActivated() )
                {
                    _draftNode = node;
                    _draftText = value._text;
                }
                if ( bChanged && _draftNode == node )
                    _draftText = shown;
                if ( ImGui::IsItemDeactivated() && _draftNode == node )
                {
                    if ( _draftText != value._text )
                    {
                        if ( document.setLeafText( node, _draftText ) )
                            _bEdited = 1;
                        else
                            _status = "'" + _draftText + "' is not a number - " + path + " keeps " + value._text;
                    }
                    _draftNode = kNoNode;
                }
                break;
            }
            case JSONType::Bool:
            {
                bool bValue = value._text == "true";
                if ( ImGui::Checkbox( "##value", &bValue ) && document.setLeafText( node, bValue ? "true" : "false" ) )
                    _bEdited = 1;
                break;
            }
            case JSONType::Null:
            case JSONType::Array:
            case JSONType::Object:
                EditorThemeUtil::textMuted( "null" );
                break;
        }
        EditorSelfTestMarks::record( mark.c_str() );
    }

    void ProjectSettingsTableView::drawRemoveButton( const ProjectSettingsJSONDocument& document, uint32 parentNode, size_t childIndex, const string& path )
    {
        if ( parentNode == kNoNode || document.isReadOnly() )
            return;
        if ( ImGui::SmallButton( "x" ) )
        {
            _pendingEdit  = PendingEdit::Remove;
            _pendingNode  = parentNode;
            _pendingIndex = childIndex;
        }
        EditorSelfTestMarks::record( ( "projectsettings.table." + path + ".remove" ).c_str() );
        EditorWidgets::drawTooltip( "Remove" );
        ImGui::SameLine();
    }

    bool ProjectSettingsTableView::applyPendingEdit( ProjectSettingsJSONDocument& document )
    {
        const PendingEdit edit = _pendingEdit;
        _pendingEdit           = PendingEdit::None;
        switch ( edit )
        {
            case PendingEdit::None:
                return false;
            case PendingEdit::AddItem:
                return document.addArrayItem( _pendingNode ).has_value();
            case PendingEdit::AddMember:
                if ( document.addObjectMember( _pendingNode, _pendingKey ) )
                    return true;
                _status = "'" + _pendingKey + "' is already a key here";
                return false;
            case PendingEdit::Remove:
                _draftNode = kNoNode; // 뺀 칸의 초안을 남기지 않는다
                return document.removeChild( _pendingNode, _pendingIndex );
        }
        return false;
    }
} // namespace sw::editor
