#include "pch.h"

#include "DevTools/Profiling/Memory/MemorySnapshot.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/WallClock.h"

namespace sw
{
    namespace
    {
        struct MemorySnapshotInternal
        {
            static constexpr MemoryColumn kArrTagColumn[] = {
                {       "Tag",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {      "Live", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {      "Peak", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {    "Budget", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                { "LiveCount", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {"TotalCount", MemoryColumnKind::Count, MemoryColumnRole::Value},
            };
            static constexpr MemoryColumn kArrSiteColumn[] = {
                {  "Location",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {      "Live", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                { "LiveCount", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {"TotalBytes", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {"TotalCount", MemoryColumnKind::Count, MemoryColumnRole::Value},
            };
            static constexpr MemoryColumn kArrAssetColumn[] = {
                {      "Kind",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {      "Path",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {     "Total", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {       "CPU", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {       "GPU", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
                {"References", MemoryColumnKind::Count, MemoryColumnRole::Value},
            };
            static constexpr MemoryColumn kArrGPUColumn[] = {
                {"Resource",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {    "Name",  MemoryColumnKind::Text, MemoryColumnRole::Value},
                {    "Kind",  MemoryColumnKind::Text, MemoryColumnRole::Value},
                {  "Format",  MemoryColumnKind::Text, MemoryColumnRole::Value},
                {   "Width", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {  "Height", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {  "Layers", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {    "Mips", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {   "Bytes", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
            };
            static constexpr MemoryColumn kArrTypeColumn[] = {
                {"Category",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {    "Type",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                {   "Count", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {   "Bytes", MemoryColumnKind::Bytes, MemoryColumnRole::Value},
            };
            static constexpr MemoryColumn kArrCacheColumn[] = {
                { "Cache",  MemoryColumnKind::Text,   MemoryColumnRole::Key},
                { "Items", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {  "Hits", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {"Misses", MemoryColumnKind::Count, MemoryColumnRole::Value},
                {"Clears", MemoryColumnKind::Count, MemoryColumnRole::Value},
            };

            /** @brief `MemorySheetKind` 순서의 서술입니다. */
            static constexpr MemorySheetSchema kArrSchema[] = {
                {           "Tags",   kArrTagColumn,   static_cast<uint32>( sizeof( kArrTagColumn ) / sizeof( MemoryColumn ) ), 1, kMemoryNoColumn},
                {"AllocationSites",  kArrSiteColumn,  static_cast<uint32>( sizeof( kArrSiteColumn ) / sizeof( MemoryColumn ) ), 1, kMemoryNoColumn},
                {         "Assets", kArrAssetColumn, static_cast<uint32>( sizeof( kArrAssetColumn ) / sizeof( MemoryColumn ) ), 2,               0},
                {   "GPUResources",   kArrGPUColumn,   static_cast<uint32>( sizeof( kArrGPUColumn ) / sizeof( MemoryColumn ) ), 8,               2},
                {          "Types",  kArrTypeColumn,  static_cast<uint32>( sizeof( kArrTypeColumn ) / sizeof( MemoryColumn ) ), 3,               0},
                {         "Caches", kArrCacheColumn, static_cast<uint32>( sizeof( kArrCacheColumn ) / sizeof( MemoryColumn ) ), 1, kMemoryNoColumn},
            };
            static_assert( sizeof( kArrSchema ) / sizeof( kArrSchema[0] ) == kMemorySheetCount, "kArrSchema needs one schema per MemorySheetKind" );

            /** @brief 전송 글의 첫 줄입니다(판이 바뀌면 숫자를 올린다). */
            static constexpr string_view kTextMagic = "swmem 1";

            /** @brief 글 칸을 전송 글로 씁니다 — 탭 · 줄바꿈 · 역슬래시만 바꾼다. */
            static void appendEscaped( string& outText, string_view value )
            {
                for ( const utf8 character : value )
                {
                    switch ( character )
                    {
                        case '\\':
                            outText += "\\\\";
                            break;
                        case '\t':
                            outText += "\\t";
                            break;
                        case '\n':
                            outText += "\\n";
                            break;
                        case '\r':
                            outText += "\\r";
                            break;
                        default:
                            outText += character;
                            break;
                    }
                }
            }

            /** @brief `appendEscaped` 를 되돌립니다. 모르는 이스케이프는 false 입니다. */
            [[nodiscard]] static bool readEscaped( string_view value, string& outText )
            {
                outText.clear();
                for ( size_t index = 0; index < value.size(); ++index )
                {
                    if ( value[index] != '\\' )
                    {
                        outText += value[index];
                        continue;
                    }
                    if ( ++index >= value.size() )
                        return false;
                    switch ( value[index] )
                    {
                        case '\\':
                            outText += '\\';
                            break;
                        case 't':
                            outText += '\t';
                            break;
                        case 'n':
                            outText += '\n';
                            break;
                        case 'r':
                            outText += '\r';
                            break;
                        default:
                            return false;
                    }
                }
                return true;
            }

            /** @brief @p line 을 탭으로 나눕니다. */
            static void splitTabs( string_view line, vector<string_view>& outListField )
            {
                outListField.clear();
                size_t start = 0;
                for ( size_t index = 0; index <= line.size(); ++index )
                {
                    if ( index == line.size() || line[index] == '\t' )
                    {
                        outListField.push_back( line.substr( start, index - start ) );
                        start = index + 1;
                    }
                }
            }

            /** @brief @p text 의 다음 줄을 @p outLine 에 담고 읽은 자리를 옮깁니다. 끝이면 false 입니다. */
            [[nodiscard]] static bool readLine( string_view text, size_t& inoutOffset, string_view& outLine )
            {
                if ( inoutOffset >= text.size() )
                    return false;
                const size_t end  = text.find( '\n', inoutOffset );
                const size_t stop = end == string_view::npos ? text.size() : end;
                outLine           = text.substr( inoutOffset, stop - inoutOffset );
                inoutOffset       = stop + 1;
                return true;
            }

            /** @brief `<이름>\t<값>` 머리 줄 하나를 읽습니다. */
            [[nodiscard]] static bool readHeaderField( string_view text, size_t& inoutOffset, string_view name, string& outValue )
            {
                string_view         line;
                vector<string_view> listField;
                if ( readLine( text, inoutOffset, line ) == false )
                    return false;
                splitTabs( line, listField );
                return listField.size() == 2 && listField[0] == name && readEscaped( listField[1], outValue );
            }

            /** @brief 같은 키의 줄을 더한 값입니다(비교의 한 쪽). */
            struct KeyedValue
            {
                vector<string> _listKeyText;
                uint64         _value{ 0 };
            };

            /** @brief 시트 @p sheet 의 줄을 키로 모아 크기 칸을 더합니다. */
            static void accumulate( const MemorySheetSchema& schema, const MemorySheet& sheet, unordered_map<string, KeyedValue>& outMapKeyToValue )
            {
                vector<string> listKeyText;
                for ( const MemoryRow& row : sheet._listRow )
                {
                    if ( row._listCell.size() != schema._columnCount )
                        continue;
                    row.collectKeyText( schema, listKeyText );
                    string key;
                    for ( const string& keyText : listKeyText )
                    {
                        key += keyText;
                        key += '\x1f'; // 키 글에 나올 수 없는 단위 구분자 — 두 칸을 이은 글이 다른 두 칸과 겹치지 않게
                    }
                    KeyedValue& value = outMapKeyToValue[key];
                    if ( value._listKeyText.empty() )
                        value._listKeyText = listKeyText;
                    value._value += row._listCell[schema._sizeColumn]._value;
                }
            }
        };
    } // namespace

    const MemorySheetSchema& getMemorySheetSchema( MemorySheetKind kind )
    {
        const uint32 index = static_cast<uint32>( kind );
        SW_ASSERT( index < kMemorySheetCount );
        return MemorySnapshotInternal::kArrSchema[index < kMemorySheetCount ? index : 0];
    }

    bool findMemorySheetKind( string_view name, MemorySheetKind& outKind )
    {
        for ( uint32 index = 0; index < kMemorySheetCount; ++index )
        {
            if ( name == MemorySnapshotInternal::kArrSchema[index]._pName )
            {
                outKind = static_cast<MemorySheetKind>( index );
                return true;
            }
        }
        return false;
    }

    string MemorySnapshot::writeText() const
    {
        string text{ MemorySnapshotInternal::kTextMagic };
        text += '\n';
        text += "label\t";
        MemorySnapshotInternal::appendEscaped( text, _label );
        text += "\nsource\t";
        MemorySnapshotInternal::appendEscaped( text, _source );
        text += "\ngpu\t";
        MemorySnapshotInternal::appendEscaped( text, _gpuDescription );
        text += "\ncpu\t";
        StringUtil::appendNumber( text, _cpuLiveBytes );
        text += "\ngpubytes\t";
        StringUtil::appendNumber( text, _gpuLiveBytes );
        text += '\n';
        for ( uint32 sheetIndex = 0; sheetIndex < kMemorySheetCount; ++sheetIndex )
        {
            const MemorySheetSchema& schema = MemorySnapshotInternal::kArrSchema[sheetIndex];
            const MemorySheet&       sheet  = _arrSheet[sheetIndex];
            text += "sheet\t";
            text += schema._pName;
            text += '\t';
            StringUtil::appendNumber( text, static_cast<uint64>( sheet._listRow.size() ) );
            text += '\n';
            for ( const MemoryRow& row : sheet._listRow )
            {
                for ( uint32 column = 0; column < schema._columnCount; ++column )
                {
                    if ( column > 0 )
                        text += '\t';
                    const MemoryCell* pCell = column < row._listCell.size() ? &row._listCell[column] : nullptr;
                    if ( schema._pColumn[column]._kind == MemoryColumnKind::Text )
                        MemorySnapshotInternal::appendEscaped( text, pCell != nullptr ? string_view{ pCell->_text } : string_view{} );
                    else
                        StringUtil::appendNumber( text, pCell != nullptr ? pCell->_value : 0ull );
                }
                text += '\n';
            }
        }
        return text;
    }

    bool MemorySnapshot::readText( string_view text, MemorySnapshot& outSnapshot )
    {
        outSnapshot = MemorySnapshot{};
        size_t      offset{ 0 };
        string_view line;
        if ( MemorySnapshotInternal::readLine( text, offset, line ) == false || line != MemorySnapshotInternal::kTextMagic )
            return false;
        string     cpuText;
        string     gpuBytesText;
        const bool bHeader = MemorySnapshotInternal::readHeaderField( text, offset, "label", outSnapshot._label ) &&
                             MemorySnapshotInternal::readHeaderField( text, offset, "source", outSnapshot._source ) &&
                             MemorySnapshotInternal::readHeaderField( text, offset, "gpu", outSnapshot._gpuDescription ) &&
                             MemorySnapshotInternal::readHeaderField( text, offset, "cpu", cpuText ) &&
                             MemorySnapshotInternal::readHeaderField( text, offset, "gpubytes", gpuBytesText );
        if ( bHeader == false || StringUtil::parseUint64( cpuText, outSnapshot._cpuLiveBytes ) == false ||
             StringUtil::parseUint64( gpuBytesText, outSnapshot._gpuLiveBytes ) == false )
            return false;

        vector<string_view> listField;
        for ( uint32 sheetIndex = 0; sheetIndex < kMemorySheetCount; ++sheetIndex )
        {
            const MemorySheetSchema& schema = MemorySnapshotInternal::kArrSchema[sheetIndex];
            if ( MemorySnapshotInternal::readLine( text, offset, line ) == false )
                return false;
            MemorySnapshotInternal::splitTabs( line, listField );
            uint64 rowCount{ 0 };
            if ( listField.size() != 3 || listField[0] != "sheet" || listField[1] != schema._pName || StringUtil::parseUint64( listField[2], rowCount ) == false )
                return false;
            MemorySheet& sheet = outSnapshot._arrSheet[sheetIndex];
            sheet._listRow.reserve( static_cast<size_t>( rowCount ) );
            for ( uint64 rowIndex = 0; rowIndex < rowCount; ++rowIndex )
            {
                if ( MemorySnapshotInternal::readLine( text, offset, line ) == false )
                    return false;
                MemorySnapshotInternal::splitTabs( line, listField );
                if ( listField.size() != schema._columnCount )
                    return false;
                MemoryRow& row = sheet._listRow.emplace_back();
                row._listCell.resize( schema._columnCount );
                for ( uint32 column = 0; column < schema._columnCount; ++column )
                {
                    MemoryCell& cell   = row._listCell[column];
                    const bool  bValid = schema._pColumn[column]._kind == MemoryColumnKind::Text ? MemorySnapshotInternal::readEscaped( listField[column], cell._text )
                                                                                                 : StringUtil::parseUint64( listField[column], cell._value );
                    if ( bValid == false )
                        return false;
                }
            }
        }
        return true;
    }

    const utf8* getMemoryDiffStatusName( MemoryDiffStatus status )
    {
        switch ( status )
        {
            case MemoryDiffStatus::New:
                return "New";
            case MemoryDiffStatus::Grown:
                return "Grown";
            case MemoryDiffStatus::Shrunk:
                return "Shrunk";
            case MemoryDiffStatus::Gone:
                return "Gone";
            case MemoryDiffStatus::Same:
                return "Same";
        }
        return "Invalid";
    }

    void MemoryRow::collectKeyText( const MemorySheetSchema& schema, vector<string>& outListKeyText ) const
    {
        outListKeyText.clear();
        for ( uint32 column = 0; column < schema._columnCount && column < _listCell.size(); ++column )
        {
            if ( schema._pColumn[column]._role == MemoryColumnRole::Key )
                outListKeyText.push_back( _listCell[column]._text );
        }
    }

    string MemorySnapshot::makeTimeLabel()
    {
        LocalCalendarTime localTime{};
        (void)WallClock::toLocalCalendar( WallClock::nowUnixMilliseconds().toSeconds(), localTime ); // 바꾸지 못하면 0 날짜 이름이다(스크린샷 이름과 같은 규칙)
        StringBuilder<constant::buffer::kMax32> label;
        label.appendFormat( "%04d%02d%02d-%02d%02d%02d", localTime._year, localTime._month, localTime._day, localTime._hour, localTime._minute, localTime._second );
        return string{ label.view() };
    }

    MemorySheetDiff MemorySheetDiff::compare( const MemorySnapshot& before, const MemorySnapshot& after, MemorySheetKind kind )
    {
        const MemorySheetSchema&                                  schema = getMemorySheetSchema( kind );
        unordered_map<string, MemorySnapshotInternal::KeyedValue> mapBefore;
        unordered_map<string, MemorySnapshotInternal::KeyedValue> mapAfter;
        MemorySnapshotInternal::accumulate( schema, before.getSheet( kind ), mapBefore );
        MemorySnapshotInternal::accumulate( schema, after.getSheet( kind ), mapAfter );

        MemorySheetDiff diff{};
        diff._kind = kind;
        diff._listRow.reserve( mapBefore.size() + mapAfter.size() );
        const auto addRow = [&diff]( const vector<string>& listKeyText, uint64 beforeValue, uint64 afterValue, MemoryDiffStatus status )
        {
            MemoryDiffRow& row = diff._listRow.emplace_back();
            row._listKeyText   = listKeyText;
            row._before        = beforeValue;
            row._after         = afterValue;
            row._delta         = static_cast<int64>( afterValue ) - static_cast<int64>( beforeValue );
            row._status        = status;
            diff._totalDelta += row._delta;
        };
        for ( const auto& [key, afterValue] : mapAfter )
        {
            const auto beforeIt = mapBefore.find( key );
            if ( beforeIt == mapBefore.end() )
            {
                addRow( afterValue._listKeyText, 0, afterValue._value, MemoryDiffStatus::New );
                continue;
            }
            const uint64           beforeBytes = beforeIt->second._value;
            const MemoryDiffStatus status      = afterValue._value > beforeBytes ? MemoryDiffStatus::Grown
                                               : afterValue._value < beforeBytes ? MemoryDiffStatus::Shrunk
                                                                                 : MemoryDiffStatus::Same;
            addRow( afterValue._listKeyText, beforeBytes, afterValue._value, status );
        }
        for ( const auto& [key, beforeValue] : mapBefore )
        {
            if ( mapAfter.find( key ) == mapAfter.end() )
                addRow( beforeValue._listKeyText, beforeValue._value, 0, MemoryDiffStatus::Gone );
        }
        // 늘어난 것 먼저 — 같은 차이는 키 글 순(해시 표의 순서가 결과에 나오지 않게).
        VectorUtil::sort( diff._listRow, []( const MemoryDiffRow& lhs, const MemoryDiffRow& rhs )
        { return lhs._delta != rhs._delta ? lhs._delta > rhs._delta : lhs._listKeyText < rhs._listKeyText; } );
        return diff;
    }
} // namespace sw
