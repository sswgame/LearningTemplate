#include "pch.h"

#include "Editor/Panels/DataTable/Model/DataTableViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
#include "Editor/Common/Localization/LocalizationTools.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    DataTableViewModel::DataTableViewModel( const DocumentEditRecorder& recorder )
        : _pRecorder{ &recorder }
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

    void DataTableViewModel::pollBackgroundJobs()
    {
        LocalizationSheet    loadedSheet;
        const EditorTaskPoll localizationPoll = _localizationJob.take( loadedSheet );
        if ( localizationPoll == EditorTaskPoll::Ready )
        {
            (void)acceptLoadedSheet( _requestedProjectIndex, std::move( loadedSheet ) ); // 버렸으면 아직 읽지 않은 상태라 다음 그리기가 고른 프로젝트를 다시 읽는다
        }
        else if ( _bLocalizationLoaded == false && localizationPoll == EditorTaskPoll::Idle )
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
        const EditorTaskPoll      gameDataPoll = _gameDataJob.take( listGameData );
        if ( gameDataPoll == EditorTaskPoll::Ready )
        {
            setGameDataFiles( std::move( listGameData ) );
        }
        else if ( _bGameDataLoaded == false && gameDataPoll == EditorTaskPoll::Idle )
            _gameDataJob.request();
    }

    bool DataTableViewModel::saveDocument()
    {
        saveLocalization(); // 고친 것이 있을 때만 쓴다
        const bool bGameDataSaved = _gameDataPath.empty() || saveGameDataFile();
        return bGameDataSaved && isAnyDirty() == false;
    }

    void DataTableViewModel::collectFilesToSave( vector<string>& outListPath ) const
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
        // 읽지 못한 테이블은 비어 보이고, 저장은 그 파일을 덮지 않는다(`saveLocalization`).
        if ( EditorDataTableCommands::loadLocalizationProject( _localizationSheet._projectPath, _localizationSheet ) == false )
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
    }

    void DataTableViewModel::markTranslationEdited( LocalizationRecord& record, size_t cultureIndex )
    {
        record._listState[cultureIndex] = record._listTranslation[cultureIndex].empty() ? TranslationState::Missing : TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
    }

    void DataTableViewModel::confirmTranslation( LocalizationRecord& record, size_t cultureIndex )
    {
        const TranslationState state = record._listState[cultureIndex];
        if ( state != TranslationState::Stale && state != TranslationState::Review )
            return;
        record._listState[cultureIndex] = TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
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
        return true;
    }

    void DataTableViewModel::deleteRecord( size_t recordIndex )
    {
        if ( recordIndex >= _localizationSheet._listRecord.size() )
            return;
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

    string DataTableViewModel::makeGameDataAssetID( string_view absolutePath )
    {
        return editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
    }

    bool DataTableViewModel::openGameDataFile( string_view absolutePath )
    {
        // 읽지 못하면 아무 파일도 열지 않는다 — 앞 파일의 글을 그대로 들고 열면, 저장할 때 **앞 파일의 내용을 이 파일에** 쓴다.
        string text;
        if ( FileUtil::readTextFile( FileUtil::toDiskPath( absolutePath ), text ) == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.CouldNotReadGame", "Could not read game data table '%#' - it is not opened", string( absolutePath ).c_str() );
            _gameDataPath.clear();
            openGameDataText( string{} ); // 읽은 것이 없다 - 테이블을 비운다
            return false;
        }
        _gameDataPath = string( absolutePath );
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
        const string fileName = FileUtil::getFileNamePart( _gameDataPath );
        if ( _gameDataDocument.isParsed() == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.GameDataNotValidXML", "Game data table '%#' is not saved - the text is not valid XML: %#", fileName.c_str(), _gameDataDocument.getError().c_str() );
            return false;
        }
        if ( EditorContentWrite::warnIfReadOnly( _gameDataPath, EditorContentWriteKind::Save ) ) // Save File 단추는 패널 저장을 지나지 않는다 — 같은 확인을 여기서도
            return false;
        if ( FileUtil::writeTextFile( FileUtil::toDiskPath( _gameDataPath ), _selectedGameDataRawText ) == false )
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
