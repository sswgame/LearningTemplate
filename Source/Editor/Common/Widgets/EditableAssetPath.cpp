#include "pch.h"

#include "Editor/Common/Widgets/EditableAssetPath.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Serialization/Base/DataPath.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "EditableAssetPath" );

    EditableAssetPath::EditableAssetPath( const DataPath& initial )
        : _committed{ initial.get() }
        , _draft{ initial.get() }
    {
    }

    bool EditableAssetPath::drawField( const utf8* pID, float32 width )
    {
        (void)EditorWidgets::drawTextField( pID, _draft, width ); // 글자마다 바뀌는 글 — 확정은 칸을 떠날 때 한 번
        if ( ImGui::IsItemDeactivatedAfterEdit() == false )
            return false;
        return commit( _draft );
    }

    bool EditableAssetPath::commit( string_view text )
    {
        if ( isAssetID( text ) == false )
        {
            const string rejected{ text };
            SW_LOG_WARNING( "'%#' is not a resource path (domain/folder/file) - keeping '%#'", rejected.c_str(), _committed.c_str() );
            return false;
        }
        const string path = FileUtil::normalizeSeparators( StringUtil::trim( text ) );
        _draft            = path;
        if ( path == _committed )
            return false;
        _committed = path;
        return true;
    }

    bool EditableAssetPath::isAssetID( string_view text )
    {
        const string_view trimmed = StringUtil::trim( text );
        return trimmed.empty() == false && FileUtil::isAbsolutePath( trimmed ) == false;
    }
} // namespace sw::editor
