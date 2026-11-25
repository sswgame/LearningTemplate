#include "pch.h"

#include "Editor/Panels/DataTable/Model/DataTableViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/EngineChildProcess.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
#include "Editor/Common/Job/EditorExternalToolJob.h"
#include "Editor/Common/Localization/LocalizationTools.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Common/Workspace/Undo/EditorStateTextHistory.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"

#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    DataTableViewModel::DataTableViewModel( const DocumentEditRecorder& recorder )
        : _pLocalizationHistory{ make_unique<EditorStateTextHistory>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) ) }
        , _pLocalizationToolJob{ make_unique<EditorExternalToolJob>() }
        , _pendingRecord{}
        , _pendingRecordLabel{}
        , _localizationToolStatus{}
        , _runningTool{ LocalizationTool::CheckText }
        , _pRecorder{ &recorder }
        , _localizationSheet{}
        , _listLocalizationProject{}
        , _listGameDataFile{}
        , _gameDataPath{}
        , _selectedGameDataRawText{}
        , _savedGameDataRawText{}
        , _gameDataDocument{}
        , _localizationJob{}
        , _gameDataJob{}
        , _selectedProjectIndex{ 0 }
        , _requestedProjectIndex{ 0 }
        , _gameDataLoadCount{ 0 }
        , _bLocalizationLoaded{ SW_FALSE }
        , _bGameDataLoaded{ SW_FALSE }
        , _bLocalizationDirty{ SW_FALSE }
        , _bGameDataDirty{ SW_FALSE }
    {
    }

    DataTableViewModel::~DataTableViewModel() = default;

    CommandStack& DataTableViewModel::getLocalizationStack()
    {
        return _pLocalizationHistory->getStack();
    }

    void DataTableViewModel::beginRecordEdit( const LocalizationRecord& record )
    {
        _pendingRecord = record;
        _pendingRecordLabel.clear();
    }

    void DataTableViewModel::flushRecordEdit( bool bInteracting )
    {
        if ( bInteracting || _pendingRecord.has_value() == false )
            return;
        const optional<size_t> recordIndex = findRecordIndex( _pendingRecord->_key );
        if ( _pendingRecordLabel.empty() == false && recordIndex.has_value() )
            pushRecordStep( _pendingRecordLabel, _pendingRecord, _localizationSheet._listRecord[*recordIndex], *recordIndex );
        _pendingRecord = nullopt;
        _pendingRecordLabel.clear();
    }

    optional<size_t> DataTableViewModel::findRecordIndex( string_view key ) const
    {
        for ( size_t index = 0; index < _localizationSheet._listRecord.size(); ++index )
        {
            if ( string_view{ _localizationSheet._listRecord[index]._key } == key )
                return index;
        }
        return nullopt;
    }

    void DataTableViewModel::pushRecordStep( string_view label, const optional<LocalizationRecord>& before, const optional<LocalizationRecord>& after, size_t recordIndex )
    {
        const auto share = []( const optional<LocalizationRecord>& record )
        { return record.has_value() ? sw::make_shared<const LocalizationRecord>( *record ) : shared_ptr<const LocalizationRecord>{}; };
        const shared_ptr<const LocalizationRecord> pBefore = share( before );
        const shared_ptr<const LocalizationRecord> pAfter  = share( after );
        const string                               key     = before.has_value() ? before->_key : after->_key;
        uint64                                     bytes   = 0;
        for ( const LocalizationRecord* pRecord : { pBefore.get(), pAfter.get() } )
        {
            if ( pRecord == nullptr )
                continue;
            bytes += pRecord->_source.size();
            for ( const string& translation : pRecord->_listTranslation )
            {
                bytes += translation.size();
            }
        }
        _pLocalizationHistory->pushStep( label, SW_DELEGATE_LAMBDA( Delegate<void()>, [this, key, pBefore, recordIndex]()
        { applyRecord( key, pBefore, recordIndex ); } ),
                                         SW_DELEGATE_LAMBDA( Delegate<void()>, [this, key, pAfter, recordIndex]()
        { applyRecord( key, pAfter, recordIndex ); } ),
                                         bytes );
    }

    void DataTableViewModel::applyRecord( const string& key, const shared_ptr<const LocalizationRecord>& pRecord, size_t recordIndex )
    {
        vector<LocalizationRecord>& listRecord = _localizationSheet._listRecord;
        const optional<size_t>      existing   = findRecordIndex( key );
        _bLocalizationDirty                    = SW_TRUE;
        if ( pRecord == nullptr )
        {
            if ( existing.has_value() == false )
                return;
            _localizationSheet._listRemovedKey.push_back( key );
            listRecord.erase( listRecord.begin() + static_cast<ptrdiff_t>( *existing ) );
            return;
        }
        (void)VectorUtil::removeIf( _localizationSheet._listRemovedKey, [&key]( const string& removed )
        { return removed == key; } );
        LocalizationRecord restored = *pRecord;
        restored._bModified         = true; // 되돌린 값도 저장이 써야 한다 — 편집한 값이 이미 저장됐을 수 있다
        if ( existing.has_value() )
            listRecord[*existing] = std::move( restored );
        else
            listRecord.insert( listRecord.begin() + static_cast<ptrdiff_t>( MathUtil::min( recordIndex, listRecord.size() ) ), std::move( restored ) );
    }

    void DataTableViewModel::clearLocalizationHistory()
    {
        _pendingRecord = nullopt;
        _pendingRecordLabel.clear();
        _pLocalizationHistory->clear();
    }

    LocalizationCultureProgress DataTableViewModel::countCultureProgress( size_t cultureIndex ) const
    {
        LocalizationCultureProgress progress;
        for ( const LocalizationRecord& record : _localizationSheet._listRecord )
        {
            switch ( record._listState[cultureIndex] )
            {
                case TranslationState::Current:
                    ++progress._currentCount;
                    break;
                case TranslationState::Stale:
                    ++progress._staleCount;
                    break;
                case TranslationState::Review:
                    ++progress._reviewCount;
                    break;
                case TranslationState::Missing:
                    ++progress._missingCount;
                    break;
                case TranslationState::Orphan: // 원문이 없는 번역은 이 표의 줄이 아니다
                    break;
            }
        }
        return progress;
    }

    bool DataTableViewModel::isLocalizationToolRunning() const
    {
        return _pLocalizationToolJob->isPending();
    }

    bool DataTableViewModel::canStartLocalizationTool() const
    {
        return isLocalizationToolRunning() == false && _bLocalizationDirty == SW_FALSE && _bLocalizationLoaded == SW_TRUE && _localizationSheet._projectPath.empty() == false;
    }

    bool DataTableViewModel::startLocalizationTool( LocalizationTool tool, size_t cultureIndex )
    {
        if ( canStartLocalizationTool() == false )
            return false;
        vector<string> listArgument;
        switch ( tool )
        {
            case LocalizationTool::GatherText:
                listArgument.push_back( "--gather-text" );
                break;
            case LocalizationTool::CheckText:
                listArgument.push_back( "--check-text" );
                break;
            case LocalizationTool::ExportPo:
                listArgument.push_back( "--export-po" );
                break;
            case LocalizationTool::ImportPo:
            {
                if ( cultureIndex >= _localizationSheet._listCulture.size() )
                    return false;
                // 내보내기가 쓰는 자리다(`LocalizationTools::exportProjectPo` — 프로젝트 곁의 `po/<문화권>.po`)
                StringBuilder<constant::buffer::kMax1024> argument;
                argument.appendFormat( "--import-po=%#/po/%#.po", _localizationSheet._projectPath.getParent().str(), _localizationSheet._listCulture[cultureIndex] );
                listArgument.push_back( string( argument.view() ) );
                break;
            }
        }
        StringBuilder<constant::buffer::kMax1024> project;
        project.appendFormat( "-loc-project=%#", _localizationSheet._projectPath.str() );
        listArgument.push_back( string( project.view() ) );
        ProcessCommand command = EngineChildProcess::makeCommand( editor::getSubsystem<CommandLineManager>(), FileUtil::getExecutablePath(), std::move( listArgument ) );
        if ( _pLocalizationToolJob->request( std::move( command ), ProcessRoots::get()._project.str() ) == false )
            return false;
        _runningTool            = tool;
        _localizationToolStatus = "Running...";
        return true;
    }

    void DataTableViewModel::collectLocalizationToolResult()
    {
        EditorExternalToolResult result{};
        if ( _pLocalizationToolJob->take( result ) != EditorJobPoll::Ready )
            return;
        const bool bSucceeded = result._exitCode.has_value() && *result._exitCode == 0;
        string     lastLine;
        for ( const string& line : result._listLine )
        {
            if ( StringUtil::trim( line ).empty() == false )
                lastLine = line;
        }
        StringBuilder<constant::buffer::kMax1024> status;
        status.appendFormat( "%# - %#", bSucceeded ? "Done" : "Failed", lastLine );
        _localizationToolStatus = string( status.view() );
        if ( bSucceeded )
            SW_LOG_INFO_ID( "DataTablePanel.LocalizationToolDone", "Localization tool finished: %#", _localizationToolStatus );
        else
            SW_LOG_WARNING_ID( "DataTablePanel.LocalizationToolFailed", "Localization tool failed: %#", _localizationToolStatus );
        const bool bWritesTables = _runningTool == LocalizationTool::GatherText || _runningTool == LocalizationTool::ImportPo;
        if ( bWritesTables && _bLocalizationDirty == SW_FALSE )
            reloadLocalization(); // 도구가 표 파일을 고쳤다
    }

    void DataTableViewModel::pollBackgroundJobs()
    {
        collectLocalizationToolResult();
        LocalizationSheet   loadedSheet;
        const EditorJobPoll localizationPoll = _localizationJob.take( loadedSheet );
        if ( localizationPoll == EditorJobPoll::Ready )
        {
            (void)acceptLoadedSheet( _requestedProjectIndex, std::move( loadedSheet ) ); // 버렸으면 아직 읽지 않은 상태라 다음 그리기가 고른 프로젝트를 다시 읽는다
        }
        else if ( _bLocalizationLoaded == false && localizationPoll == EditorJobPoll::Idle )
        {
            vector<string> listProject;
            LocalizationTools::collectProjectPaths( listProject ); // 현지화 도구와 같은 목록(엔진 · 에디터 UI · 활성 게임)
            setLocalizationProjects( std::move( listProject ) );
            if ( _listLocalizationProject.empty() )
                _bLocalizationLoaded = SW_TRUE; // 프로젝트가 없다 — 빈 안내를 보인다
            else
            {
                _requestedProjectIndex = _selectedProjectIndex;
                _localizationJob.request( _listLocalizationProject[static_cast<size_t>( _selectedProjectIndex )] );
            }
        }

        vector<GameDataFileEntry> listGameData;
        const EditorJobPoll       gameDataPoll = _gameDataJob.take( listGameData );
        if ( gameDataPoll == EditorJobPoll::Ready )
        {
            setGameDataFiles( std::move( listGameData ) );
        }
        else if ( _bGameDataLoaded == false && gameDataPoll == EditorJobPoll::Idle )
            _gameDataJob.request();
    }

    bool DataTableViewModel::saveDocument()
    {
        saveLocalization(); // 고친 것이 있을 때만 쓴다
        const bool bGameDataSaved = _gameDataPath.empty() || saveGameDataFile();
        return bGameDataSaved && isAnyDirty() == false;
    }

    void DataTableViewModel::collectFilesToSave( vector<AbsolutePath>& outListPath ) const
    {
        if ( _bLocalizationDirty )
            EditorDataTableCommands::collectLocalizationFiles( _localizationSheet, outListPath );
        if ( _gameDataPath.empty() == false )
            outListPath.push_back( _gameDataPath );
    }

    void DataTableViewModel::revertLocalization()
    {
        if ( _bLocalizationDirty == false )
            return;
        clearLocalizationHistory();
        // 읽지 못한 테이블은 비어 보이고, 저장은 그 파일을 덮지 않는다(`saveLocalization`).
        if ( EditorDataTableCommands::loadLocalizationProject( _localizationSheet._projectPath.str(), _localizationSheet ) == false )
            SW_LOG_WARNING_ID( "DataTablePanel.SomeLocalizationFilesCould", "Some localization files could not be read - see the warnings above" );
        _bLocalizationLoaded = SW_TRUE;
        _bLocalizationDirty  = SW_FALSE;
    }

    void DataTableViewModel::setLocalizationProjects( vector<string> listProject )
    {
        _listLocalizationProject = std::move( listProject );
        if ( _selectedProjectIndex < 0 || static_cast<size_t>( _selectedProjectIndex ) >= _listLocalizationProject.size() )
            _selectedProjectIndex = 0;
    }

    void DataTableViewModel::selectProject( size_t projectIndex )
    {
        if ( projectIndex >= _listLocalizationProject.size() || _bLocalizationDirty )
            return;
        _selectedProjectIndex = static_cast<int32>( projectIndex );
        reloadLocalization();
    }

    bool DataTableViewModel::acceptLoadedSheet( int32 requestedProjectIndex, LocalizationSheet&& sheet )
    {
        if ( _bLocalizationDirty || requestedProjectIndex != _selectedProjectIndex )
            return false;
        _localizationSheet   = std::move( sheet );
        _bLocalizationLoaded = SW_TRUE;
        return true;
    }

    void DataTableViewModel::markSourceEdited( LocalizationRecord& record )
    {
        // 검토 표시는 원문과 상관없이 남는다(`TranslationTable::computeState` 가 검토를 먼저 본다).
        for ( TranslationState& state : record._listState )
        {
            if ( state == TranslationState::Current )
                state = TranslationState::Stale;
        }
        record._bModified   = true;
        _bLocalizationDirty = SW_TRUE;
        _pendingRecordLabel = "Edit Source Text";
    }

    void DataTableViewModel::markTranslationEdited( LocalizationRecord& record, size_t cultureIndex )
    {
        record._listState[cultureIndex] = record._listTranslation[cultureIndex].empty() ? TranslationState::Missing : TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
        _pendingRecordLabel             = "Edit Translation";
    }

    void DataTableViewModel::confirmTranslation( LocalizationRecord& record, size_t cultureIndex )
    {
        const TranslationState state = record._listState[cultureIndex];
        if ( state != TranslationState::Stale && state != TranslationState::Review )
            return;
        const LocalizationRecord before = record;
        record._listState[cultureIndex] = TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
        pushRecordStep( "Confirm Translation", before, record, findRecordIndex( record._key ).value_or( 0 ) );
    }

    uint32 DataTableViewModel::countCurrentTranslations( size_t cultureIndex ) const
    {
        uint32 count = 0;
        for ( const LocalizationRecord& record : _localizationSheet._listRecord )
        {
            if ( record._listState[cultureIndex] == TranslationState::Current )
                ++count;
        }
        return count;
    }

    bool DataTableViewModel::addKey( string_view key )
    {
        if ( key.empty() || _localizationSheet._listTablePath.empty() )
            return false;
        for ( const LocalizationRecord& record : _localizationSheet._listRecord )
        {
            if ( string_view{ record._key } == key )
                return false;
        }
        LocalizationRecord newRecord{};
        newRecord._key       = key;
        newRecord._bModified = true;
        newRecord._listTranslation.resize( _localizationSheet._listCulture.size() );
        newRecord._listState.resize( _localizationSheet._listCulture.size(), TranslationState::Missing );
        _localizationSheet._listRecord.push_back( std::move( newRecord ) );
        _bLocalizationDirty = SW_TRUE;
        pushRecordStep( "Add Key", nullopt, _localizationSheet._listRecord.back(), _localizationSheet._listRecord.size() - 1 );
        return true;
    }

    void DataTableViewModel::deleteRecord( size_t recordIndex )
    {
        if ( recordIndex >= _localizationSheet._listRecord.size() )
            return;
        pushRecordStep( "Delete Key", _localizationSheet._listRecord[recordIndex], nullopt, recordIndex );
        _localizationSheet._listRemovedKey.push_back( _localizationSheet._listRecord[recordIndex]._key );
        _localizationSheet._listRecord.erase( _localizationSheet._listRecord.begin() + static_cast<ptrdiff_t>( recordIndex ) );
        _bLocalizationDirty = SW_TRUE;
    }

    void DataTableViewModel::collectVisibleRecords( const EditorListFilter& filter, vector<size_t>& outListVisibleIndex, LocalizationRowFilter rowFilter ) const
    {
        // 호출한 쪽이 **멤버 버퍼를 다시 쓴다** — 지역 `vector` 면 프레임마다 할당하고 해제한다.
        outListVisibleIndex.clear();
        outListVisibleIndex.reserve( _localizationSheet._listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < _localizationSheet._listRecord.size(); ++recordIndex )
        {
            const LocalizationRecord& record = _localizationSheet._listRecord[recordIndex];
            if ( rowFilter == LocalizationRowFilter::NeedsWork && record.needsWork() == false )
                continue;
            bool bMatches = filter.matchesAny( { record._key, record._source } );
            for ( const string& translation : record._listTranslation )
            {
                bMatches = bMatches || filter.matchesAny( { translation } );
            }
            if ( bMatches )
                outListVisibleIndex.push_back( recordIndex );
        }
    }

    void DataTableViewModel::reloadLocalization()
    {
        clearLocalizationHistory();                 // 단계가 가리키던 표가 사라진다
        _localizationSheet   = LocalizationSheet{}; // 읽는 동안 앞 표에 키를 더하면 dirty 가 되어 읽은 결과를 영영 받지 못한다
        _bLocalizationDirty  = SW_FALSE;
        _bLocalizationLoaded = SW_FALSE;
    }

    void DataTableViewModel::saveLocalization()
    {
        // 고친 것이 없으면 쓰지 않는다 — 읽는 중의 빈 표나 그대로인 표로 파일을 다시 쓰고 실행 중인 게임을 다시 읽게 할 까닭이 없다.
        if ( _bLocalizationDirty == false )
            return;
        // 하나라도 쓰지 못했으면 dirty 를 지우지 않는다 — 저장 확인이 다시 묻는다.
        if ( EditorDataTableCommands::saveLocalizationProject( _localizationSheet ) == false )
            return;
        _bLocalizationDirty = SW_FALSE;
    }

    void DataTableViewModel::setGameDataFiles( vector<GameDataFileEntry> listFile )
    {
        _listGameDataFile = std::move( listFile );
        _bGameDataLoaded  = SW_TRUE;
    }

    string DataTableViewModel::makeGameDataAssetID( const AbsolutePath& absolutePath )
    {
        return editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
    }

    bool DataTableViewModel::openGameDataFile( const AbsolutePath& absolutePath )
    {
        // 읽지 못하면 아무 파일도 열지 않는다 — 앞 파일의 글을 그대로 들고 열면, 저장할 때 **앞 파일의 내용을 이 파일에** 쓴다.
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.CouldNotReadGame", "Could not read game data table '%#' - it is not opened", string( absolutePath.str() ).c_str() );
            _gameDataPath = {};
            openGameDataText( string{} ); // 읽은 것이 없다 - 테이블을 비운다
            return false;
        }
        _gameDataPath = absolutePath;
        openGameDataText( std::move( text ) );
        return true;
    }

    void DataTableViewModel::openGameDataText( string&& text )
    {
        _selectedGameDataRawText = std::move( text );
        _savedGameDataRawText    = _selectedGameDataRawText;
        (void)_gameDataDocument.load( _selectedGameDataRawText ); // 깨진 원문도 글 상자로 고치게 열린다
        ++_gameDataLoadCount;
        _bGameDataDirty = SW_FALSE;
    }

    bool DataTableViewModel::saveGameDataFile()
    {
        if ( _gameDataPath.empty() )
            return false;
        const string fileName = FileUtil::getFileNamePart( _gameDataPath.str() );
        if ( _gameDataDocument.isParsed() == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.GameDataNotValidXML", "Game data table '%#' is not saved - the text is not valid XML: %#", fileName.c_str(), _gameDataDocument.getError().c_str() );
            return false;
        }
        if ( EditorContentWrite::warnIfReadOnly( _gameDataPath, EditorContentWriteKind::Save ) ) // Save File 단추는 패널 저장을 지나지 않는다 — 같은 확인을 여기서도
            return false;
        if ( FileUtil::writeTextFile( _gameDataPath, _selectedGameDataRawText ) == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.CouldNotWriteGame", "Could not write game data table '%#'", _gameDataPath.c_str() );
            return false;
        }
        _savedGameDataRawText = _selectedGameDataRawText;
        _gameDataDocument.markSaved();
        _bGameDataDirty = SW_FALSE;
        SW_LOG_INFO_ID( "DataTablePanel.SavedGameDataTable", "Saved game data table %#", fileName.c_str() );
        return true;
    }

    void DataTableViewModel::reloadGameDataFiles()
    {
        _bGameDataLoaded = SW_FALSE;
        _gameDataJob.request();
    }

    void DataTableViewModel::replaceGameDataRawText( string_view text )
    {
        _selectedGameDataRawText = text;
        commitGameDataRawEdit();
    }

    void DataTableViewModel::commitGameDataRawEdit()
    {
        _gameDataDocument.replaceText( _selectedGameDataRawText );
        _bGameDataDirty = SW_TRUE;
        _pRecorder->notifyEdited( "Edit Game Data Text" ); // 글 상자를 치는 동안은 상호작용 구간 하나다
    }

    void DataTableViewModel::commitGameDataTableEdit()
    {
        _selectedGameDataRawText = _gameDataDocument.getText();
        _bGameDataDirty          = SW_TRUE;
        _pRecorder->notifyEdited( "Edit Game Data Table" );
    }

    void DataTableViewModel::applyGameDataText( string_view text )
    {
        _selectedGameDataRawText = text;
        _gameDataDocument.replaceText( _selectedGameDataRawText ); // 깨진 원문도 글 상자로 고치게 둔다
        _bGameDataDirty = _selectedGameDataRawText != _savedGameDataRawText ? SW_TRUE : SW_FALSE;
        ++_gameDataLoadCount;
    }
} // namespace sw::editor
