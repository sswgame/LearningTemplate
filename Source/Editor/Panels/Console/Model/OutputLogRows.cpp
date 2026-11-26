#include "pch.h"

#include "Editor/Panels/Console/Model/OutputLogRows.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/ParsedLogLine.h"
#include "Core/String/StringBuilder.h"

namespace sw::editor
{
    string OutputLogRows::formatEntryLine( const LogEntry& entry )
    {
        StringBuilder<constant::buffer::kMax1024> lineBuilder;
        lineBuilder.appendFormat( "[%#] [%#] [%#] - %#", entry._timeStamp, entry._tag, ParsedLogLine::getLevelLabel( entry._level ), entry._message );
        return string( lineBuilder.view() );
    }

    string OutputLogRows::formatEntryLineWithSource( const LogEntry& entry )
    {
        string full = formatEntryLine( entry );
        if ( entry._file.empty() == false )
        {
            StringBuilder<constant::buffer::kMax256> fullPart;
            fullPart.appendFormat( " (%#:%#)", entry._file, entry._line );
            full += fullPart.c_str();
        }
        return full;
    }

    void OutputLogRows::populate( const vector<const OutputLogEntry*>& listVisible, bool bCollapse, vector<OutputLogRow>& outListRow )
    {
        outListRow.clear();
        outListRow.reserve( listVisible.size() );
        if ( bCollapse == false )
        {
            for ( const OutputLogEntry* pEntry : listVisible )
            {
                if ( pEntry != nullptr )
                    outListRow.push_back( OutputLogRow{ pEntry, 1 } );
            }
            return;
        }

        // 수준 · 태그 · 카테고리 · 메시지를 이은 글이 키다. 글 사이에 넣는 0 바이트는 메시지에 나오지 않는다.
        unordered_map<string, uint32> mapRowByKey;
        mapRowByKey.reserve( listVisible.size() );
        string key;
        for ( const OutputLogEntry* pEntry : listVisible )
        {
            if ( pEntry == nullptr )
                continue;
            key.clear();
            key += static_cast<utf8>( '0' + static_cast<uint8>( pEntry->_level ) );
            key += pEntry->_bExpected ? 'e' : 'u'; // 선언된 줄과 같은 글의 진짜 오류를 한 행에 접지 않는다
            key += '\0';
            key += pEntry->_tag;
            key += '\0';
            key += pEntry->getCategory();
            key += '\0';
            key += pEntry->_message;
            const auto found = mapRowByKey.find( key );
            if ( found != mapRowByKey.end() )
            {
                ++outListRow[found->second]._repeatCount;
                continue;
            }
            mapRowByKey.emplace( key, static_cast<uint32>( outListRow.size() ) );
            outListRow.push_back( OutputLogRow{ pEntry, 1 } );
        }
    }

    bool OutputLogRows::isScrolledToBottom( float32 scrollY, float32 scrollMaxY, float32 tolerance )
    {
        return scrollY + tolerance >= scrollMaxY;
    }
} // namespace sw::editor

namespace sw::editor
{
    OutputLogSelection::OutputLogSelection()
        : _anchorRow{ 0 }
        , _activeRow{ 0 }
        , _bHasSelection{ SW_FALSE }
        , _bDragging{ SW_FALSE }
    {
    }

    void OutputLogSelection::press( uint32 rowIndex, bool bExtend )
    {
        if ( bExtend == false || _bHasSelection == false )
            _anchorRow = rowIndex;
        _activeRow     = rowIndex;
        _bHasSelection = SW_TRUE;
        _bDragging     = SW_TRUE;
    }

    void OutputLogSelection::dragTo( uint32 rowIndex )
    {
        if ( _bDragging )
            _activeRow = rowIndex;
    }

    void OutputLogSelection::clear()
    {
        _anchorRow     = 0;
        _activeRow     = 0;
        _bHasSelection = SW_FALSE;
        _bDragging     = SW_FALSE;
    }

    void OutputLogSelection::clampTo( uint32 rowCount )
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

    bool OutputLogSelection::isSelected( uint32 rowIndex ) const
    {
        return _bHasSelection && rowIndex >= getFirst() && rowIndex <= getLast();
    }

    uint32 OutputLogSelection::getFirst() const
    {
        return _anchorRow < _activeRow ? _anchorRow : _activeRow;
    }

    uint32 OutputLogSelection::getLast() const
    {
        return _anchorRow < _activeRow ? _activeRow : _anchorRow;
    }
} // namespace sw::editor
