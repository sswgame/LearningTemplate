#include "pch.h"

#include "Editor/Panels/ConsoleLogRows.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

namespace sw::editor
{
    void ConsoleLogRows::populate( const vector<const LogEntry*>& listVisible, bool bCollapse, vector<ConsoleLogRow>& outListRow )
    {
        outListRow.clear();
        outListRow.reserve( listVisible.size() );
        if ( bCollapse == false )
        {
            for ( const LogEntry* pEntry : listVisible )
            {
                if ( pEntry != nullptr )
                    outListRow.push_back( ConsoleLogRow{ pEntry, 1 } );
            }
            return;
        }

        // 수준 · 태그 · 카테고리 · 메시지를 이은 글이 키다. 글 사이에 넣는 0 바이트는 메시지에 나오지 않는다.
        unordered_map<string, uint32> mapRowByKey;
        mapRowByKey.reserve( listVisible.size() );
        string key;
        for ( const LogEntry* pEntry : listVisible )
        {
            if ( pEntry == nullptr )
                continue;
            key.clear();
            key += static_cast<utf8>( '0' + static_cast<uint8>( pEntry->_level ) );
            key += '\0';
            key += pEntry->_tag;
            key += '\0';
            key += pEntry->_caller;
            key += '\0';
            key += pEntry->_message;
            const auto found = mapRowByKey.find( key );
            if ( found != mapRowByKey.end() )
            {
                ++outListRow[found->second]._repeatCount;
                continue;
            }
            mapRowByKey.emplace( key, static_cast<uint32>( outListRow.size() ) );
            outListRow.push_back( ConsoleLogRow{ pEntry, 1 } );
        }
    }

    bool ConsoleLogRows::isScrolledToBottom( float32 scrollY, float32 scrollMaxY, float32 tolerance )
    {
        return scrollY + tolerance >= scrollMaxY;
    }
} // namespace sw::editor

namespace sw::editor
{
    ConsoleLogSelection::ConsoleLogSelection()
        : _anchorRow{ 0 }
        , _activeRow{ 0 }
        , _bHasSelection{ false }
        , _bDragging{ false }
    {
    }

    void ConsoleLogSelection::press( uint32 rowIndex, bool bExtend )
    {
        if ( bExtend == false || _bHasSelection == false )
            _anchorRow = rowIndex;
        _activeRow     = rowIndex;
        _bHasSelection = true;
        _bDragging     = true;
    }

    void ConsoleLogSelection::dragTo( uint32 rowIndex )
    {
        if ( _bDragging )
            _activeRow = rowIndex;
    }

    void ConsoleLogSelection::clear()
    {
        _anchorRow     = 0;
        _activeRow     = 0;
        _bHasSelection = false;
        _bDragging     = false;
    }

    void ConsoleLogSelection::clampTo( uint32 rowCount )
    {
        if ( _bHasSelection == false )
            return;
        if ( rowCount == 0 )
        {
            clear();
            return;
        }
        _anchorRow = _anchorRow < rowCount ? _anchorRow : rowCount - 1;
        _activeRow = _activeRow < rowCount ? _activeRow : rowCount - 1;
    }

    bool ConsoleLogSelection::isSelected( uint32 rowIndex ) const
    {
        return _bHasSelection && rowIndex >= getFirst() && rowIndex <= getLast();
    }

    uint32 ConsoleLogSelection::getFirst() const
    {
        return _anchorRow < _activeRow ? _anchorRow : _activeRow;
    }

    uint32 ConsoleLogSelection::getLast() const
    {
        return _anchorRow < _activeRow ? _activeRow : _anchorRow;
    }
} // namespace sw::editor
