#include "pch.h"

#include "Editor/Common/Commands/EditorCommandInfo.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Localization/EditorDataLocalization.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    void EditorCommandInfoSet::loadRegistered()
    {
        _listRow.clear();
        _listUsed.clear();
        _listMenu.clear();
        using TextRegistry = EditorExtensionRegistry<EditorCommandInfoRegistration>;
        for ( uint32 index = 0; index < TextRegistry::getCount(); ++index )
        {
            const EditorCommandInfoRegistration& registration = TextRegistry::getAt( index );
            EditorCommandInfoTable               table;
            if ( XMLSerializer::loadFromAsset( AssetID{ registration._pAssetPath }, &table, *EditorCommandInfoTable::StaticType() ) == false )
            {
                SW_LOG_ERROR_ID( "EditorCommand.TextCannotRead", "Editor command text '%#' could not be read strictly - its commands are not registered", registration._pAssetPath );
                continue;
            }
            for ( EditorCommandInfo& row : table._listCommand )
            {
                if ( VectorUtil::findBy<&EditorCommandInfo::_id>( _listRow, row._id ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "EditorCommand.TextDuplicate", "Editor command '%#' has a second text row in '%#'", row._id.c_str(), registration._pAssetPath );
                    continue;
                }
                _listRow.push_back( std::move( row ) );
            }
            for ( EditorMenuText& menu : table._listMenu )
            {
                if ( VectorUtil::findBy<&EditorMenuText::_path>( _listMenu, menu._path ) != nullptr )
                {
                    SW_LOG_ERROR_ID( "EditorCommand.MenuTextDuplicate", "Editor menu '%#' has a second name row in '%#'", menu._path.c_str(), registration._pAssetPath );
                    continue;
                }
                _listMenu.push_back( std::move( menu ) );
            }
        }
        _listUsed.assign( _listRow.size(), SW_FALSE );
    }

    bool EditorCommandInfoSet::apply( EditorCommandDesc& inoutDesc )
    {
        const EditorCommandInfo* pRow = VectorUtil::findBy<&EditorCommandInfo::_id>( _listRow, inoutDesc._id );
        if ( pRow == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorCommand.NoText", "Editor command '%#' has no row in any registered *.commands.xml - it is not registered", inoutDesc._id.c_str() );
            return false;
        }
        _listUsed[static_cast<size_t>( pRow - _listRow.data() )] = SW_TRUE;
        // 명령 글은 데이터라 컴파일이 아이콘 이름을 보지 못한다 — 명령 목록을 짓는 기동에서 알린다(그리기 전).
        const EditorIconGlyphTable* pIconTable = editor::findSubsystem<EditorIconGlyphTable>(); // 선택: 에디터 컨텍스트 없이 명령 글만 읽는 테스트에는 없다
        if ( pRow->_icon.empty() == false && pIconTable != nullptr && pIconTable->findGlyph( hashed_string{ string_view{ pRow->_icon } } ) == nullptr )
            SW_LOG_ERROR_ID( "EditorCommand.UnknownIcon", "Editor command '%#' names icon '%#' which the icon table does not have", inoutDesc._id.c_str(), pRow->_icon.c_str() );

        inoutDesc._label            = EditorDataLocalization::localize( pRow->_label );
        inoutDesc._iconName         = pRow->_icon;
        inoutDesc._category         = pRow->_category;
        inoutDesc._tooltip          = EditorDataLocalization::localize( pRow->_tooltip );
        inoutDesc._detail           = EditorDataLocalization::localize( pRow->_detail );
        inoutDesc._inputAction      = pRow->_inputAction;
        inoutDesc._menuPath         = pRow->_menuPath;
        inoutDesc._menuOrder        = pRow->_menuOrder;
        inoutDesc._contextMenuKind  = pRow->_contextMenu;
        inoutDesc._contextMenuOrder = pRow->_contextMenuOrder;
        inoutDesc._bPaletteVisible  = pRow->_bPaletteVisible;
        return true;
    }

    bool EditorCommandInfoSet::findMenuLabel( string_view menuPath, string& outLabel ) const
    {
        const EditorMenuText* pMenu = VectorUtil::findBy<&EditorMenuText::_path>( _listMenu, menuPath );
        if ( pMenu == nullptr )
            return false;
        outLabel = EditorDataLocalization::localize( pMenu->_label );
        return true;
    }

    void EditorCommandInfoSet::reportUnused() const
    {
        for ( size_t index = 0; index < _listRow.size(); ++index )
        {
            if ( _listUsed[index] == SW_FALSE )
                SW_LOG_ERROR_ID( "EditorCommand.TextUnused", "Editor command text row '%#' matches no command the code registers", _listRow[index]._id.c_str() );
        }
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorCommandInfoRegistration )
} // namespace sw::editor
