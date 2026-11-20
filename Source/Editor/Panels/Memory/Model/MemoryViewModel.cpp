#include "pch.h"

#include "Editor/Panels/Memory/Model/MemoryViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorRemoteInspector.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    namespace
    {
        struct MemoryViewModelInternal
        {
            /** @brief 요약에 보이는 태그 막대 수입니다(큰 태그부터). */
            static constexpr uint32 kTagBarCount = 8;

            /** @brief 정렬 · 거르기 전의 칸 하나입니다(숫자 칸은 값으로 정렬한다). */
            struct SortCell
            {
                string _text;
                int64  _value{ 0 };
            };

            /** @brief 정렬 · 거르기 전의 줄 하나입니다. */
            struct SortRow
            {
                vector<SortCell> _listCell;
                string           _keyText; ///< 이름 거르기가 보는 글(키 칸을 이은 글)
                string           _filterText;
                string           _assetPath;
                MemoryDiffStatus _status{ MemoryDiffStatus::Same };
            };

            static string toCountText( uint64 value )
            {
                string text;
                StringUtil::appendNumber( text, value );
                return text;
            }

            /** @brief 거르고 정렬해 그릴 줄로 옮깁니다. 숫자 칸 @p listNumeric 은 값으로, 나머지는 글로 정렬한다(같으면 원래 순서 — 안정 정렬). */
            static void sortAndEmit( vector<SortRow>& inoutListRow, const vector<MemoryTableColumn>& listColumn, uint32 sortColumn, bool bDescending, string_view nameFilter,
                                     string_view filterText, vector<MemoryTableRow>& outListRow )
            {
                vector<SortRow*> listKept;
                listKept.reserve( inoutListRow.size() );
                for ( SortRow& row : inoutListRow )
                {
                    const bool bNameMatch   = nameFilter.empty() || StringUtil::contains( row._keyText, nameFilter, true );
                    const bool bFilterMatch = filterText.empty() || row._filterText == filterText;
                    if ( bNameMatch && bFilterMatch )
                        listKept.push_back( &row );
                }
                if ( sortColumn < listColumn.size() )
                {
                    const bool bNumeric = listColumn[sortColumn]._bNumeric != SW_FALSE;
                    VectorUtil::stableSort( listKept, [sortColumn, bNumeric, bDescending]( const SortRow* pLhs, const SortRow* pRhs )
                    {
                        const SortCell& lhs = pLhs->_listCell[sortColumn];
                        const SortCell& rhs = pRhs->_listCell[sortColumn];
                        if ( bNumeric )
                            return bDescending ? lhs._value > rhs._value : lhs._value < rhs._value;
                        return bDescending ? rhs._text < lhs._text : lhs._text < rhs._text;
                    } );
                }
                outListRow.clear();
                outListRow.reserve( listKept.size() );
                for ( const SortRow* pRow : listKept )
                {
                    MemoryTableRow& row = outListRow.emplace_back();
                    row._listCell.reserve( pRow->_listCell.size() );
                    for ( const SortCell& cell : pRow->_listCell )
                    {
                        row._listCell.push_back( cell._text );
                    }
                    row._assetPath = pRow->_assetPath;
                    row._status    = pRow->_status;
                }
            }

            /** @brief 크기 칸 종류에 맞춘 글입니다(바이트는 사람용 단위, 수는 그대로). */
            static string formatValue( MemoryColumnKind kind, uint64 value )
            {
                return kind == MemoryColumnKind::Bytes ? MemoryViewModel::formatBytes( value ) : toCountText( value );
            }
        };
    } // namespace

    void MemoryExportJob::request( MemoryExportRequest request )
    {
        const uint32 generation = beginRequest( std::move( request ) );
        submitJob( "MemoryExportWrite", SW_DELEGATE_FUNCTION( TaskArgsDelegate, MemoryExportJob::runJob ), MakeTaskArgs( _pState, generation ) );
    }

    void MemoryExportJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        MemoryExportRequest     input;
        if ( readInput( pState, generation, input ) == false )
            return;
        MemoryPanelExportResult jobResult{};
        jobResult._folder = input._folder;
        if ( input._bWholeSnapshot != SW_FALSE && input._pSnapshot != nullptr )
        {
            const expected<MemoryExportResult> result = MemorySnapshotExport::writeSnapshot( *input._pSnapshot, input._folder );
            if ( result.has_value() )
                jobResult._fileCount = static_cast<uint32>( result->_listFile.size() );
            else
                jobResult._error = string{ result.error().message() } + " - " + string{ result.error().detail() };
        }
        else
        {
            const expected<MemoryExportFile> file = MemorySnapshotExport::writeTable( input._table, FileUtil::joinPath( input._folder, input._fileName ) );
            if ( file.has_value() )
                jobResult._fileCount = 1;
            else
                jobResult._error = string{ file.error().message() } + " - " + string{ file.error().detail() };
        }
        publish( pState, generation, std::move( jobResult ) );
    }

    MemoryViewModel::MemoryViewModel()
        : _listSnapshot{}
        , _currentIndex{ kMemoryNoSnapshot }
        , _compareIndex{ kMemoryNoSnapshot }
        , _sheet{ MemorySheetKind::Tags }
        , _sortColumn{ getMemorySheetSchema( MemorySheetKind::Tags )._sizeColumn }
        , _nameFilter{}
        , _filterIndex{ 0 }
        , _statusLine{}
        , _visibleRowCount{ 0 }
        , _exportJob{}
        , _bSortDescending{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    string MemoryViewModel::formatBytes( uint64 bytes )
    {
        constexpr uint64 kKilobyte = 1024;
        string           text;
        if ( bytes < kKilobyte )
        {
            StringUtil::appendNumber( text, bytes );
            text += " B";
            return text;
        }
        static constexpr const utf8* kArrUnit[] = { " KB", " MB", " GB" };
        float64                      value      = static_cast<float64>( bytes ) / static_cast<float64>( kKilobyte );
        uint32                       unit       = 0;
        while ( value >= static_cast<float64>( kKilobyte ) && unit + 1 < 3 )
        {
            value /= static_cast<float64>( kKilobyte );
            ++unit;
        }
        StringUtil::appendNumber( text, value, 2 );
        text += kArrUnit[unit];
        return text;
    }

    string MemoryViewModel::formatSignedBytes( int64 bytes )
    {
        if ( bytes == 0 )
            return string{ "0 B" };
        const uint64 magnitude = bytes < 0 ? static_cast<uint64>( -bytes ) : static_cast<uint64>( bytes );
        return ( bytes < 0 ? string{ "-" } : string{ "+" } ) + formatBytes( magnitude );
    }

    void MemoryViewModel::buildSheetTable( const MemorySnapshot& snapshot, MemorySheetKind sheet, uint32 sortColumn, bool bDescending, string_view nameFilter, string_view filterText,
                                           vector<MemoryTableColumn>& outListColumn, vector<MemoryTableRow>& outListRow )
    {
        using Internal                  = MemoryViewModelInternal;
        const MemorySheetSchema& schema = getMemorySheetSchema( sheet );
        outListColumn.clear();
        for ( uint32 column = 0; column < schema._columnCount; ++column )
        {
            outListColumn.push_back( MemoryTableColumn{ schema._pColumn[column]._pName, static_cast<uint8>( schema._pColumn[column]._kind != MemoryColumnKind::Text ? SW_TRUE : SW_FALSE ) } );
        }
        vector<Internal::SortRow> listRow;
        listRow.reserve( snapshot.getSheet( sheet )._listRow.size() );
        for ( const MemoryRow& source : snapshot.getSheet( sheet )._listRow )
        {
            if ( source._listCell.size() != schema._columnCount )
                continue;
            Internal::SortRow& row = listRow.emplace_back();
            for ( uint32 column = 0; column < schema._columnCount; ++column )
            {
                const MemoryColumn& columnSchema = schema._pColumn[column];
                const MemoryCell&   cell         = source._listCell[column];
                if ( columnSchema._kind == MemoryColumnKind::Text )
                {
                    row._listCell.push_back( Internal::SortCell{ cell._text, 0 } );
                    if ( columnSchema._role == MemoryColumnRole::Key )
                    {
                        row._keyText += cell._text;
                        row._keyText += ' ';
                    }
                }
                else
                    row._listCell.push_back( Internal::SortCell{ Internal::formatValue( columnSchema._kind, cell._value ), static_cast<int64>( cell._value ) } );
            }
            if ( schema._filterColumn != kMemoryNoColumn )
                row._filterText = source._listCell[schema._filterColumn]._text;
            if ( sheet == MemorySheetKind::Assets )
                row._assetPath = source._listCell[1]._text; // Path 칸
        }
        Internal::sortAndEmit( listRow, outListColumn, sortColumn, bDescending, nameFilter, filterText, outListRow );
    }

    void MemoryViewModel::buildDiffTable( const MemorySheetDiff& diff, uint32 sortColumn, bool bDescending, string_view nameFilter, string_view filterText,
                                          vector<MemoryTableColumn>& outListColumn, vector<MemoryTableRow>& outListRow )
    {
        using Internal                  = MemoryViewModelInternal;
        const MemorySheetSchema& schema = getMemorySheetSchema( diff._kind );
        const MemoryColumnKind   kind   = schema._pColumn[schema._sizeColumn]._kind;
        outListColumn.clear();
        uint32 keyCount{ 0 };
        uint32 filterKeyIndex{ kMemoryNoColumn }; // 거르기 칸이 키 칸이면 키 글 안의 자리
        uint32 pathKeyIndex{ kMemoryNoColumn };
        for ( uint32 column = 0; column < schema._columnCount; ++column )
        {
            if ( schema._pColumn[column]._role != MemoryColumnRole::Key )
                continue;
            if ( column == schema._filterColumn )
                filterKeyIndex = keyCount;
            if ( diff._kind == MemorySheetKind::Assets && column == 1 )
                pathKeyIndex = keyCount;
            outListColumn.push_back( MemoryTableColumn{ schema._pColumn[column]._pName, SW_FALSE } );
            ++keyCount;
        }
        for ( const utf8* pName : { "Before", "After", "Delta" } )
        {
            outListColumn.push_back( MemoryTableColumn{ pName, SW_TRUE } );
        }
        outListColumn.push_back( MemoryTableColumn{ "Status", SW_FALSE } );

        vector<Internal::SortRow> listRow;
        listRow.reserve( diff._listRow.size() );
        for ( const MemoryDiffRow& source : diff._listRow )
        {
            Internal::SortRow& row = listRow.emplace_back();
            for ( const string& keyText : source._listKeyText )
            {
                row._listCell.push_back( Internal::SortCell{ keyText, 0 } );
                row._keyText += keyText;
                row._keyText += ' ';
            }
            row._listCell.push_back( Internal::SortCell{ Internal::formatValue( kind, source._before ), static_cast<int64>( source._before ) } );
            row._listCell.push_back( Internal::SortCell{ Internal::formatValue( kind, source._after ), static_cast<int64>( source._after ) } );
            string deltaText = kind == MemoryColumnKind::Bytes ? formatSignedBytes( source._delta ) : string{};
            if ( kind != MemoryColumnKind::Bytes )
            {
                if ( source._delta > 0 )
                    deltaText += '+';
                StringUtil::appendNumber( deltaText, source._delta );
            }
            row._listCell.push_back( Internal::SortCell{ std::move( deltaText ), source._delta } );
            row._listCell.push_back( Internal::SortCell{ getMemoryDiffStatusName( source._status ), static_cast<int64>( source._status ) } );
            row._status = source._status;
            if ( filterKeyIndex < source._listKeyText.size() )
                row._filterText = source._listKeyText[filterKeyIndex];
            if ( pathKeyIndex < source._listKeyText.size() )
                row._assetPath = source._listKeyText[pathKeyIndex];
        }
        Internal::sortAndEmit( listRow, outListColumn, sortColumn, bDescending, nameFilter, filterText, outListRow );
    }

    void MemoryViewModel::collectFilterOptions( vector<string>& outListOption ) const
    {
        outListOption.clear();
        const MemorySheetSchema& schema = getMemorySheetSchema( _sheet );
        if ( schema._filterColumn == kMemoryNoColumn || _currentIndex < 0 )
            return;
        if ( isCompareMode() && schema._pColumn[schema._filterColumn]._role != MemoryColumnRole::Key )
            return; // 비교 표에는 키 칸만 남는다
        vector<string> listValue;
        for ( const MemoryRow& row : _listSnapshot[static_cast<size_t>( _currentIndex )]->getSheet( _sheet )._listRow )
        {
            if ( schema._filterColumn < row._listCell.size() )
                listValue.push_back( row._listCell[schema._filterColumn]._text );
        }
        VectorUtil::sortUnique( listValue );
        outListOption.push_back( SW_LOCKEY( "EditorMemory", "AllKinds" ) );
        for ( string& value : listValue )
        {
            outListOption.push_back( std::move( value ) );
        }
    }

    void MemoryViewModel::gather( MemoryViewState& outState )
    {
        MemoryPanelExportResult jobResult;
        const EditorTaskPoll    poll = _exportJob.take( jobResult );
        if ( poll == EditorTaskPoll::Ready )
        {
            if ( jobResult._error.empty() )
            {
                SW_LOG_INFO_ID( "MemoryPanel.Exported", "Memory panel exported %# file(s) to %#", jobResult._fileCount, jobResult._folder );
                _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "ExportedStatus", TextArgumentList().addInteger( "fileCount", jobResult._fileCount ).addText( "folder", jobResult._folder ) );
            }
            else
            {
                SW_LOG_WARNING_ID( "MemoryPanel.ExportFailed", "Memory panel export failed: %#", jobResult._error );
                _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "ExportFailedStatus", TextArgumentList().addText( "reason", jobResult._error ) );
            }
        }
        outState._bExportPending = poll == EditorTaskPoll::Running ? SW_TRUE : SW_FALSE;

        outState._listSnapshotLabel.clear();
        for ( const shared_ptr<const MemorySnapshot>& pSnapshot : _listSnapshot )
        {
            outState._listSnapshotLabel.push_back( pSnapshot->_label + " · " + pSnapshot->_source );
        }
        outState._currentIndex     = _currentIndex;
        outState._compareIndex     = isCompareMode() ? _compareIndex : kMemoryNoSnapshot;
        outState._bCompareMode     = isCompareMode() ? SW_TRUE : SW_FALSE;
        outState._sheet            = _sheet;
        outState._sortColumn       = _sortColumn;
        outState._bSortDescending  = _bSortDescending;
        outState._statusLine       = _statusLine;
        outState._bRemoteAvailable = getEditorState<EditorRemoteInspector>().isConnected() ? SW_TRUE : SW_FALSE;
        outState._listTagBar.clear();
        outState._listColumn.clear();
        outState._listRow.clear();
        outState._listFilterOption.clear();
        outState._sheetRowCount = 0;
        _visibleRowCount        = 0;
        if ( _currentIndex < 0 )
            return;

        const MemorySnapshot& snapshot = *_listSnapshot[static_cast<size_t>( _currentIndex )];
        outState._cpuText              = formatBytes( snapshot._cpuLiveBytes );
        outState._gpuText              = formatBytes( snapshot._gpuLiveBytes );
        outState._gpuDescription       = snapshot._gpuDescription;
        outState._source               = snapshot._source;

        // 태그 막대 — 살아 있는 바이트가 큰 태그부터(로그 보고 · Profiler 메모리 탭과 같은 순서).
        vector<const MemoryRow*> listTag;
        for ( const MemoryRow& row : snapshot.getSheet( MemorySheetKind::Tags )._listRow )
        {
            listTag.push_back( &row );
        }
        VectorUtil::stableSort( listTag, []( const MemoryRow* pLhs, const MemoryRow* pRhs )
        { return pLhs->_listCell[1]._value > pRhs->_listCell[1]._value; } );
        for ( size_t index = 0; index < listTag.size() && index < MemoryViewModelInternal::kTagBarCount; ++index )
        {
            const uint64 bytes = listTag[index]->_listCell[1]._value;
            if ( bytes == 0 )
                break;
            const float32 fraction = snapshot._cpuLiveBytes == 0 ? 0.0f : static_cast<float32>( static_cast<float64>( bytes ) / static_cast<float64>( snapshot._cpuLiveBytes ) );
            outState._listTagBar.push_back( MemoryTagBar{ listTag[index]->_listCell[0]._text, formatBytes( bytes ), fraction } );
        }

        collectFilterOptions( outState._listFilterOption );
        if ( _filterIndex >= outState._listFilterOption.size() )
            _filterIndex = 0;
        outState._filterIndex        = _filterIndex;
        const string_view filterText = _filterIndex > 0 ? string_view{ outState._listFilterOption[_filterIndex] } : string_view{};
        if ( isCompareMode() )
        {
            const MemorySheetDiff diff = MemorySheetDiff::compare( *_listSnapshot[static_cast<size_t>( _compareIndex )], snapshot, _sheet );
            outState._sheetRowCount    = static_cast<uint32>( diff._listRow.size() );
            buildDiffTable( diff, _sortColumn, _bSortDescending != 0, _nameFilter, filterText, outState._listColumn, outState._listRow );
        }
        else
        {
            outState._sheetRowCount = static_cast<uint32>( snapshot.getSheet( _sheet )._listRow.size() );
            buildSheetTable( snapshot, _sheet, _sortColumn, _bSortDescending != 0, _nameFilter, filterText, outState._listColumn, outState._listRow );
        }
        _visibleRowCount = static_cast<uint32>( outState._listRow.size() );
    }

    void MemoryViewModel::addSnapshot( MemorySnapshot snapshot )
    {
        if ( _listSnapshot.size() >= kMaxSnapshotCount )
        {
            _listSnapshot.erase( _listSnapshot.begin() );
            _compareIndex = _compareIndex > 0 ? _compareIndex - 1 : kMemoryNoSnapshot;
        }
        _listSnapshot.push_back( make_shared<const MemorySnapshot>( std::move( snapshot ) ) );
        _currentIndex = static_cast<int32>( _listSnapshot.size() ) - 1;
    }

    void MemoryViewModel::captureLocal()
    {
        const expected<MemorySnapshotPolicy> policy = MemorySnapshotPolicy::load();
        if ( policy.has_value() == false )
        {
            _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "NoPolicyStatus", TextArgumentList().addText( "reason", string{ policy.error().detail() } ) );
            return;
        }
        addSnapshot( MemorySnapshotSources::makeEngine( policy->_allocationSiteLimit ).collect( MemorySnapshot::makeTimeLabel(), "local" ) );
    }

    void MemoryViewModel::captureRemote()
    {
        EditorRemoteInspector& inspector = getEditorState<EditorRemoteInspector>();
        string                 body;
        MemorySnapshot         snapshot;
        // 요청은 루프백 채널에서 답을 기다리는 동기 방식이다 — 누를 때만 묻는다(Multiplayer 창 Inspector 와 같은 규칙).
        if ( inspector.isConnected() == false || inspector.fetchReply( "memory.snapshot", body ) == false || MemorySnapshot::readText( body, snapshot ) == false )
        {
            const string reason = inspector.isConnected() == false ? string{ "not attached (Multiplayer > Inspector)" } : inspector.getLastError().empty() ? body
                                                                                                                                                           : inspector.getLastError();
            SW_LOG_WARNING_ID( "MemoryPanel.RemoteFailed", "Memory panel could not read the remote snapshot: %#", reason );
            _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "RemoteFailedStatus", TextArgumentList().addText( "reason", reason ) );
            return;
        }
        snapshot._source = inspector.getRemoteAddress().toString();
        addSnapshot( std::move( snapshot ) );
    }

    void MemoryViewModel::selectSnapshot( int32 index )
    {
        if ( index < 0 || index >= static_cast<int32>( _listSnapshot.size() ) )
            return;
        _currentIndex = index;
    }

    void MemoryViewModel::setCompareIndex( int32 index )
    {
        _compareIndex = index >= 0 && index < static_cast<int32>( _listSnapshot.size() ) ? index : kMemoryNoSnapshot;
        selectSheet( _sheet ); // 칸이 바뀌므로 정렬 · 거르기를 처음으로
    }

    void MemoryViewModel::compareWithPrevious()
    {
        if ( _currentIndex > 0 )
            setCompareIndex( _currentIndex - 1 );
    }

    void MemoryViewModel::selectSheet( MemorySheetKind sheet )
    {
        _sheet           = sheet;
        _bSortDescending = SW_TRUE;
        _filterIndex     = 0;
        if ( isCompareMode() )
        {
            // 비교 표는 차이(Delta) 칸 내림차순 — 늘어난 것부터. 키 칸 수 + Before · After 다음이 Delta 다.
            uint32                   keyCount{ 0 };
            const MemorySheetSchema& schema = getMemorySheetSchema( sheet );
            for ( uint32 column = 0; column < schema._columnCount; ++column )
            {
                keyCount += schema._pColumn[column]._role == MemoryColumnRole::Key ? 1u : 0u;
            }
            _sortColumn = keyCount + 2;
            return;
        }
        _sortColumn = getMemorySheetSchema( sheet )._sizeColumn;
    }

    void MemoryViewModel::setSort( uint32 column, bool bDescending )
    {
        _sortColumn      = column;
        _bSortDescending = bDescending ? SW_TRUE : SW_FALSE;
    }

    void MemoryViewModel::setNameFilter( string_view text )
    {
        _nameFilter = string{ text };
    }

    void MemoryViewModel::setFilterIndex( uint32 index )
    {
        _filterIndex = index;
    }

    string MemoryViewModel::reserveExportFolder()
    {
        const expected<MemorySnapshotPolicy> policy = MemorySnapshotPolicy::load();
        if ( policy.has_value() == false )
        {
            _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "NoPolicyStatus", TextArgumentList().addText( "reason", string{ policy.error().detail() } ) );
            return string{};
        }
        const string folder = MemorySnapshotExport::reserveFolder( MemorySnapshotExport::getDefaultRoot( *policy ) );
        if ( folder.empty() )
            _statusLine = SW_LOCKEYFORMAT( "EditorMemory", "ExportFailedStatus", TextArgumentList().addText( "reason", "could not create the export folder" ) );
        return folder;
    }

    void MemoryViewModel::exportTable()
    {
        if ( _currentIndex < 0 )
            return;
        const string folder = reserveExportFolder();
        if ( folder.empty() )
            return;
        const MemorySnapshot& snapshot = *_listSnapshot[static_cast<size_t>( _currentIndex )];
        const string          name{ getMemorySheetSchema( _sheet )._pName };
        MemoryExportRequest   request{};
        request._folder = folder;
        if ( isCompareMode() )
        {
            request._table    = MemorySnapshotExport::makeDiffTable( MemorySheetDiff::compare( *_listSnapshot[static_cast<size_t>( _compareIndex )], snapshot, _sheet ) );
            request._fileName = name + "-Compare.csv";
        }
        else
        {
            request._table    = MemorySnapshotExport::makeSheetTable( snapshot, _sheet );
            request._fileName = name + ".csv";
        }
        _exportJob.request( std::move( request ) );
    }

    void MemoryViewModel::exportSnapshot()
    {
        if ( _currentIndex < 0 )
            return;
        const string folder = reserveExportFolder();
        if ( folder.empty() )
            return;
        MemoryExportRequest request{};
        request._pSnapshot      = _listSnapshot[static_cast<size_t>( _currentIndex )];
        request._folder         = folder;
        request._bWholeSnapshot = SW_TRUE;
        _exportJob.request( std::move( request ) );
    }
} // namespace sw::editor
