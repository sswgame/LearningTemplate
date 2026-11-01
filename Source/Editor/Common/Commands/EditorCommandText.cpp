#include "pch.h"

#include "Editor/Common/Commands/EditorCommandText.h"

#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    void EditorCommandTextSet::loadRegistered()
    {
        _listRow.clear();
        _listUsed.clear();
        using TextRegistry = EditorExtensionRegistry<EditorCommandTextRegistration>;
        for ( uint32 index = 0; index < TextRegistry::getCount(); ++index )
        {
            const EditorCommandTextRegistration& registration = TextRegistry::getAt( index );
            EditorCommandTextTable               table;
            if ( XMLSerializer::loadFile( AssetID{ registration._pAssetPath }, &table, *EditorCommandTextTable::StaticType() ) == false )
            {
                SW_LOG_ERROR_ID( "EditorCommand.TextCannotRead", "Editor command text '%#' could not be read strictly - its commands are not registered", registration._pAssetPath );
                continue;
            }
            for ( EditorCommandText& row : table._listCommand )
            {
                if ( VectorUtil::findBy<&EditorCommandText::_id>( _listRow, row._id ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "EditorCommand.TextDuplicate", "Editor command '%#' has a second text row in '%#'", row._id.c_str(), registration._pAssetPath );
                    continue;
                }
                _listRow.push_back( std::move( row ) );
            }
        }
        _listUsed.assign( _listRow.size(), SW_FALSE );
    }

    bool EditorCommandTextSet::apply( EditorCommandDesc& inoutDesc )
    {
        const EditorCommandText* pRow = VectorUtil::findBy<&EditorCommandText::_id>( _listRow, inoutDesc._id );
        if ( pRow == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorCommand.NoText", "Editor command '%#' has no row in any registered *.commands.xml - it is not registered", inoutDesc._id.c_str() );
            return false;
        }
        _listUsed[static_cast<size_t>( pRow - _listRow.data() )] = SW_TRUE;

        inoutDesc._label            = pRow->_label;
        inoutDesc._iconName         = pRow->_icon;
        inoutDesc._category         = pRow->_category;
        inoutDesc._tooltip          = pRow->_tooltip;
        inoutDesc._detail           = pRow->_detail;
        inoutDesc._inputAction      = pRow->_inputAction;
        inoutDesc._menuPath         = pRow->_menuPath;
        inoutDesc._menuOrder        = pRow->_menuOrder;
        inoutDesc._contextMenuKind  = pRow->_contextMenu;
        inoutDesc._contextMenuOrder = pRow->_contextMenuOrder;
        inoutDesc._bPaletteVisible  = pRow->_bPaletteVisible;
        return true;
    }

    void EditorCommandTextSet::reportUnused() const
    {
        for ( size_t index = 0; index < _listRow.size(); ++index )
        {
            if ( _listUsed[index] == SW_FALSE )
                SW_LOG_ERROR_ID( "EditorCommand.TextUnused", "Editor command text row '%#' matches no command the code registers", _listRow[index]._id.c_str() );
        }
    }
} // namespace sw::editor
