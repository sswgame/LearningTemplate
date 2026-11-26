#include "pch.h"

#include "DevTools/Profiling/Memory/MemorySnapshotExport.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct MemorySnapshotExportInternal
        {
            /** @brief 같은 초에 잡을 수 있는 폴더 수입니다(`-2` … — BugReport 와 같은 상한). */
            static constexpr uint32 kMaxSameSecondSuffix = 100;

            /** @brief 칸 하나를 CSV 표 줄에 더합니다 — 바이트 칸은 원값과 MB 둘, 수는 원값, 글은 그대로. */
            static void appendCells( const MemoryColumn& column, const MemoryCell& cell, vector<string>& outListField )
            {
                switch ( column._kind )
                {
                    case MemoryColumnKind::Text:
                        outListField.push_back( cell._text );
                        return;
                    case MemoryColumnKind::Count:
                    {
                        string value;
                        StringUtil::appendNumber( value, cell._value );
                        outListField.push_back( std::move( value ) );
                        return;
                    }
                    case MemoryColumnKind::Bytes:
                    {
                        string value;
                        StringUtil::appendNumber( value, cell._value );
                        outListField.push_back( std::move( value ) );
                        outListField.push_back( MemorySnapshotExport::formatMegabytes( cell._value ) );
                        return;
                    }
                }
            }

            /** @brief 칸 서술 @p column 의 머리말(바이트는 둘)을 더합니다. */
            static void appendHeader( const MemoryColumn& column, vector<string>& outListHeader )
            {
                if ( column._kind != MemoryColumnKind::Bytes )
                {
                    outListHeader.push_back( column._pName );
                    return;
                }
                StringBuilder<constant::buffer::kMax128> header;
                header.appendFormat( "%# (bytes)", column._pName );
                outListHeader.push_back( string( header.view() ) );
                header.clear();
                header.appendFormat( "%# (MB)", column._pName );
                outListHeader.push_back( string( header.view() ) );
            }

            static void appendNamedRow( CSVTable& outTable, string_view name, string value )
            {
                outTable._listRow.push_back( vector<string>{ string{ name }, std::move( value ) } );
            }

            static string toText( uint64 value )
            {
                string text;
                StringUtil::appendNumber( text, value );
                return text;
            }
        };
    } // namespace

    void CSVTable::appendField( string& outText, string_view field )
    {
        const bool bQuote = field.find_first_of( ",\"\r\n" ) != string_view::npos;
        if ( bQuote == false )
        {
            outText += field;
            return;
        }
        outText += '"';
        for ( const utf8 character : field )
        {
            if ( character == '"' )
                outText += '"'; // RFC 4180 — 칸 안의 따옴표는 두 번
            outText += character;
        }
        outText += '"';
    }

    string CSVTable::writeText() const
    {
        string     text{ kByteOrderMark };
        const auto appendLine = [&text]( const vector<string>& listField )
        {
            for ( size_t index = 0; index < listField.size(); ++index )
            {
                if ( index > 0 )
                    text += ',';
                appendField( text, listField[index] );
            }
            text += "\r\n"; // RFC 4180 의 줄 끝 — 엑셀 · 스프레드시트가 둘 다 읽는다
        };
        appendLine( _listHeader );
        for ( const vector<string>& listField : _listRow )
        {
            appendLine( listField );
        }
        return text;
    }

    bool CSVTable::writeFile( const AbsolutePath& path ) const
    {
        return FileUtil::writeTextFile( path, writeText() );
    }

    expected<void> MemorySnapshotPolicy::validate() const
    {
        const bool bBadFolder = _exportFolder.empty() || _exportFolder.find_first_of( "/\\:" ) != string::npos || _exportFolder == ".." || _exportFolder == ".";
        if ( bBadFolder )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_exportFolder must be one folder name", kConfigFile.getPath() } );
        return {};
    }

    string MemorySnapshotExport::formatMegabytes( uint64 bytes )
    {
        string text;
        StringUtil::appendNumber( text, static_cast<float64>( bytes ) / static_cast<float64>( constant::memory::kBytesPerMegabyte ), 3 );
        return text;
    }

    string MemorySnapshotExport::formatSignedMegabytes( int64 bytes )
    {
        string text;
        StringUtil::appendNumber( text, static_cast<float64>( bytes ) / static_cast<float64>( constant::memory::kBytesPerMegabyte ), 3 );
        return text;
    }

    CSVTable MemorySnapshotExport::makeSheetTable( const MemorySnapshot& snapshot, MemorySheetKind kind )
    {
        using Internal                  = MemorySnapshotExportInternal;
        const MemorySheetSchema& schema = getMemorySheetSchema( kind );
        CSVTable                 table{};
        for ( uint32 column = 0; column < schema._columnCount; ++column )
        {
            Internal::appendHeader( schema._pColumn[column], table._listHeader );
        }
        const MemorySheet& sheet = snapshot.getSheet( kind );
        table._listRow.reserve( sheet._listRow.size() );
        for ( const MemoryRow& row : sheet._listRow )
        {
            vector<string>& listField = table._listRow.emplace_back();
            listField.reserve( table._listHeader.size() );
            for ( uint32 column = 0; column < schema._columnCount; ++column )
            {
                Internal::appendCells( schema._pColumn[column], column < row._listCell.size() ? row._listCell[column] : MemoryCell{}, listField );
            }
        }
        return table;
    }

    CSVTable MemorySnapshotExport::makeDiffTable( const MemorySheetDiff& diff )
    {
        const MemorySheetSchema& schema = getMemorySheetSchema( diff._kind );
        const bool               bBytes = schema._pColumn[schema._sizeColumn]._kind == MemoryColumnKind::Bytes;
        CSVTable                 table{};
        for ( uint32 column = 0; column < schema._columnCount; ++column )
        {
            if ( schema._pColumn[column]._role == MemoryColumnRole::Key )
                table._listHeader.push_back( schema._pColumn[column]._pName );
        }
        const string sizeName{ schema._pColumn[schema._sizeColumn]._pName };
        for ( const utf8* pSide : { "Before", "After", "Delta" } )
        {
            StringBuilder<constant::buffer::kMax128> header;
            header.appendFormat( "%# %#", sizeName, pSide );
            if ( bBytes )
                header.append( " (bytes)" );
            table._listHeader.push_back( string( header.view() ) );
            if ( bBytes )
            {
                header.clear();
                header.appendFormat( "%# %# (MB)", sizeName, pSide );
                table._listHeader.push_back( string( header.view() ) );
            }
        }
        table._listHeader.push_back( "Status" );

        table._listRow.reserve( diff._listRow.size() );
        for ( const MemoryDiffRow& row : diff._listRow )
        {
            vector<string>& listField = table._listRow.emplace_back( row._listKeyText );
            for ( const uint64 value : { row._before, row._after } )
            {
                listField.push_back( MemorySnapshotExportInternal::toText( value ) );
                if ( bBytes )
                    listField.push_back( formatMegabytes( value ) );
            }
            string delta;
            StringUtil::appendNumber( delta, row._delta );
            listField.push_back( std::move( delta ) );
            if ( bBytes )
                listField.push_back( formatSignedMegabytes( row._delta ) );
            listField.push_back( getMemoryDiffStatusName( row._status ) );
        }
        return table;
    }

    CSVTable MemorySnapshotExport::makeSummaryTable( const MemorySnapshot& snapshot )
    {
        using Internal = MemorySnapshotExportInternal;
        CSVTable table{};
        table._listHeader = { "Field", "Value" };
        Internal::appendNamedRow( table, "Label", snapshot._label );
        Internal::appendNamedRow( table, "Source", snapshot._source );
        Internal::appendNamedRow( table, "GPU", snapshot._gpuDescription );
        Internal::appendNamedRow( table, "CPU Live (bytes)", Internal::toText( snapshot._cpuLiveBytes ) );
        Internal::appendNamedRow( table, "CPU Live (MB)", formatMegabytes( snapshot._cpuLiveBytes ) );
        Internal::appendNamedRow( table, "GPU Live (bytes)", Internal::toText( snapshot._gpuLiveBytes ) );
        Internal::appendNamedRow( table, "GPU Live (MB)", formatMegabytes( snapshot._gpuLiveBytes ) );
        for ( uint32 sheetIndex = 0; sheetIndex < kMemorySheetCount; ++sheetIndex )
        {
            const MemorySheetKind                   kind = static_cast<MemorySheetKind>( sheetIndex );
            fixed_string<constant::buffer::kMax128> name;
            formatstring( name.data(), name.capacity(), "%# rows", getMemorySheetSchema( kind )._pName );
            Internal::appendNamedRow( table, name.view(), Internal::toText( snapshot.getSheet( kind )._listRow.size() ) );
        }
        return table;
    }

    AbsolutePath MemorySnapshotExport::getDefaultRoot( const MemorySnapshotPolicy& policy )
    {
        const ProcessRoots& roots = ProcessRoots::get();
        return ( roots._bRunFromParent == 1 ? roots._run : roots._saved ).join( policy._exportFolder );
    }

    AbsolutePath MemorySnapshotExport::reserveFolder( const AbsolutePath& root )
    {
        // 같은 초에 다른 프로세스(병렬 실행)가 같은 이름을 고를 수 있다 — 만들기가 곧 자리 잡기다.
        return FileUtil::reserveUniquePath( root.join( MemorySnapshot::makeTimeLabel() ), {}, true, MemorySnapshotExportInternal::kMaxSameSecondSuffix );
    }

    AbsolutePath MemorySnapshotExport::findLatestFolder( const AbsolutePath& root )
    {
        vector<AbsolutePath> listFolder;
        if ( FileUtil::collectFolders( root, listFolder, false ) == false || listFolder.empty() )
            return {};
        // 이름이 yyyyMMdd-HHmmss(-n) 이라 이름 순이 시간 순이다.
        return *std::max_element( listFolder.begin(), listFolder.end() );
    }

    expected<MemoryExportFile> MemorySnapshotExport::writeTable( const CSVTable& table, const AbsolutePath& path )
    {
        if ( table.writeFile( path ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not write the CSV file", path.str() } );
        MemoryExportFile file{};
        file._path     = string{ path.str() };
        file._rowCount = static_cast<uint32>( table._listRow.size() );
        SW_LOG_INFO_ID( "MemoryExport.SheetWritten", "memory export: %# rows=%# columns=%#", file._path, file._rowCount, table._listHeader.size() );
        return file;
    }

    expected<MemoryExportResult> MemorySnapshotExport::writeSnapshot( const MemorySnapshot& snapshot, const AbsolutePath& folder )
    {
        MemoryExportResult result{};
        result._folder                     = string{ folder.str() };
        expected<MemoryExportFile> summary = writeTable( makeSummaryTable( snapshot ), folder.join( kSummaryFileName ) );
        if ( summary.has_value() == false )
            return unexpected( summary.error() );
        result._listFile.push_back( std::move( *summary ) );
        for ( uint32 sheetIndex = 0; sheetIndex < kMemorySheetCount; ++sheetIndex )
        {
            const MemorySheetKind                   kind = static_cast<MemorySheetKind>( sheetIndex );
            fixed_string<constant::buffer::kMax128> fileName;
            formatstring( fileName.data(), fileName.capacity(), "%#.csv", getMemorySheetSchema( kind )._pName );
            expected<MemoryExportFile> file = writeTable( makeSheetTable( snapshot, kind ), folder.join( fileName.view() ) );
            if ( file.has_value() == false )
                return unexpected( file.error() );
            result._listFile.push_back( std::move( *file ) );
        }
        SW_LOG_INFO_ID( "MemoryExport.Written", "memory export: %# files in %#", result._listFile.size(), result._folder );
        return result;
    }
} // namespace sw
