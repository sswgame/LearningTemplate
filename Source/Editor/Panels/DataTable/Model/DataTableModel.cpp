#include "pch.h"

#include "Editor/Panels/DataTable/Model/DataTableModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
#include "Editor/Common/Localization/LocalizationTools.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"

namespace sw::editor
{
    DataTableModel::DataTableModel()
        : _localizationSheet{}
        , _listLocalizationProject{}
        , _listGameDataFile{}
        , _selectedGameDataRawText{}
        , _savedGameDataRawText{}
        , _gameDataDocument{}
        , _localizationJob{}
        , _gameDataJob{}
        , _selectedGameDataIndex{ -1 }
        , _selectedProjectIndex{ 0 }
        , _requestedProjectIndex{ 0 }
        , _gameDataLoadCount{ 0 }
        , _bLocalizationLoaded{ SW_FALSE }
        , _bGameDataLoaded{ SW_FALSE }
        , _bLocalizationDirty{ SW_FALSE }
        , _bGameDataDirty{ SW_FALSE }
    {
    }

    void DataTableModel::pollBackgroundJobs()
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

    bool DataTableModel::saveDocument()
    {
        if ( _bLocalizationDirty )
            saveLocalization();
        if ( _bGameDataDirty )
            saveSelectedGameDataFile();
        return isAnyDirty() == false;
    }

    void DataTableModel::revert()
    {
        if ( _bLocalizationDirty )
        {
            // 읽지 못한 테이블은 비어 보이고, 저장은 그 파일을 덮지 않는다(`saveLocalization`).
            if ( EditorDataTableCommands::loadLocalizationProject( _localizationSheet._projectPath, _localizationSheet ) == false )
                SW_LOG_WARNING_ID( "DataTablePanel.SomeLocalizationFilesCould", "Some localization files could not be read - see the warnings above" );
            _bLocalizationLoaded = SW_TRUE;
            _bLocalizationDirty  = SW_FALSE;
        }
        if ( _bGameDataDirty )
        {
            _selectedGameDataRawText = _savedGameDataRawText;
            (void)_gameDataDocument.load( _selectedGameDataRawText ); // 깨진 원문도 글 상자로 고치게 열린다
            ++_gameDataLoadCount;
            _bGameDataDirty = SW_FALSE;
        }
    }

    void DataTableModel::setLocalizationProjects( vector<string> listProject )
    {
        _listLocalizationProject = std::move( listProject );
        if ( _selectedProjectIndex < 0 || static_cast<size_t>( _selectedProjectIndex ) >= _listLocalizationProject.size() )
            _selectedProjectIndex = 0;
    }

    void DataTableModel::selectProject( size_t projectIndex )
    {
        if ( projectIndex >= _listLocalizationProject.size() || _bLocalizationDirty )
            return;
        _selectedProjectIndex = static_cast<int32>( projectIndex );
        reloadLocalization();
    }

    bool DataTableModel::acceptLoadedSheet( int32 requestedProjectIndex, LocalizationSheet&& sheet )
    {
        if ( _bLocalizationDirty || requestedProjectIndex != _selectedProjectIndex )
            return false;
        _localizationSheet   = std::move( sheet );
        _bLocalizationLoaded = SW_TRUE;
        return true;
    }

    void DataTableModel::markSourceEdited( LocalizationRecord& record )
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

    void DataTableModel::markTranslationEdited( LocalizationRecord& record, size_t cultureIndex )
    {
        record._listState[cultureIndex] = record._listTranslation[cultureIndex].empty() ? TranslationState::Missing : TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
    }

    void DataTableModel::confirmTranslation( LocalizationRecord& record, size_t cultureIndex )
    {
        const TranslationState state = record._listState[cultureIndex];
        if ( state != TranslationState::Stale && state != TranslationState::Review )
            return;
        record._listState[cultureIndex] = TranslationState::Current;
        record._bModified               = true;
        _bLocalizationDirty             = SW_TRUE;
    }

    uint32 DataTableModel::countCurrentTranslations( size_t cultureIndex ) const
    {
        uint32 count = 0;
        for ( const LocalizationRecord& record : _localizationSheet._listRecord )
        {
            if ( record._listState[cultureIndex] == TranslationState::Current )
                ++count;
        }
        return count;
    }

    bool DataTableModel::addKey( string_view key )
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

    void DataTableModel::deleteRecord( size_t recordIndex )
    {
        if ( recordIndex >= _localizationSheet._listRecord.size() )
            return;
        _localizationSheet._listRemovedKey.push_back( _localizationSheet._listRecord[recordIndex]._key );
        _localizationSheet._listRecord.erase( _localizationSheet._listRecord.begin() + static_cast<ptrdiff_t>( recordIndex ) );
        _bLocalizationDirty = SW_TRUE;
    }

    void DataTableModel::collectVisibleRecords( const EditorListFilter& filter, vector<size_t>& outListVisibleIndex, LocalizationRowFilter rowFilter ) const
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

    void DataTableModel::reloadLocalization()
    {
        _localizationSheet   = LocalizationSheet{}; // 읽는 동안 앞 표에 키를 더하면 dirty 가 되어 읽은 결과를 영영 받지 못한다
        _bLocalizationDirty  = SW_FALSE;
        _bLocalizationLoaded = SW_FALSE;
    }

    void DataTableModel::saveLocalization()
    {
        // 고친 것이 없으면 쓰지 않는다 — 읽는 중의 빈 표나 그대로인 표로 파일을 다시 쓰고 실행 중인 게임을 다시 읽게 할 까닭이 없다.
        if ( _bLocalizationDirty == false )
            return;
        // 하나라도 쓰지 못했으면 dirty 를 지우지 않는다 — 저장 확인이 다시 묻는다.
        if ( EditorDataTableCommands::saveLocalizationProject( _localizationSheet ) == false )
            return;
        _bLocalizationDirty = SW_FALSE;
    }

    const GameDataFileEntry* DataTableModel::findSelectedGameDataFile() const
    {
        if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
            return nullptr;
        return &_listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
    }

    void DataTableModel::setGameDataFiles( vector<GameDataFileEntry> listFile )
    {
        // 고른 파일은 번호가 아니라 경로로 다시 찾는다. 파일이 늘거나 줄면 옛 번호는 다른 파일이고, 저장이 그 파일을 덮어쓴다.
        const GameDataFileEntry* pSelected    = findSelectedGameDataFile();
        const string             selectedPath = pSelected != nullptr ? pSelected->_absolutePath : string{};
        _listGameDataFile                     = std::move( listFile );
        _bGameDataLoaded                      = SW_TRUE;
        _selectedGameDataIndex                = -1;
        for ( size_t index = 0; index < _listGameDataFile.size() && selectedPath.empty() == false; ++index )
        {
            if ( _listGameDataFile[index]._absolutePath == selectedPath )
                _selectedGameDataIndex = static_cast<int32>( index );
        }
    }

    void DataTableModel::selectGameDataFile( size_t fileIndex )
    {
        // 고친 것이 있으면 바꾸지 않는다. 같은 파일을 다시 눌러도 고친 글을 디스크 내용으로 덮지 않는다(다시 읽기는 Revert 다).
        if ( fileIndex >= _listGameDataFile.size() || _bGameDataDirty )
            return;
        _selectedGameDataIndex = static_cast<int32>( fileIndex );
        loadSelectedGameDataFile();
    }

    void DataTableModel::loadSelectedGameDataFile()
    {
        const GameDataFileEntry* pEntry = findSelectedGameDataFile();
        if ( pEntry == nullptr )
            return;

        // 읽지 못하면 고르지 않는다 — 앞 파일의 글을 그대로 들고 고르면, 저장할 때 **앞 파일의 내용을 이 파일에** 쓴다.
        string text;
        if ( FileUtil::readTextFile( pEntry->_absolutePath, text ) == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.CouldNotReadGame", "Could not read game data table '%#' - it is not opened", pEntry->_absolutePath.c_str() );
            _selectedGameDataIndex = -1;
            openGameDataText( string{} ); // 읽은 것이 없다 - 테이블을 비운다
            return;
        }
        openGameDataText( std::move( text ) );
    }

    void DataTableModel::openGameDataText( string&& text )
    {
        _selectedGameDataRawText = std::move( text );
        _savedGameDataRawText    = _selectedGameDataRawText;
        (void)_gameDataDocument.load( _selectedGameDataRawText ); // 깨진 원문도 글 상자로 고치게 열린다
        ++_gameDataLoadCount;
        _bGameDataDirty = SW_FALSE;
    }

    void DataTableModel::saveSelectedGameDataFile()
    {
        const GameDataFileEntry* pEntry = findSelectedGameDataFile();
        if ( pEntry == nullptr )
            return;

        if ( _gameDataDocument.isParsed() == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.GameDataNotValidXML", "Game data table '%#' is not saved - the text is not valid XML: %#", pEntry->_fileName.c_str(), _gameDataDocument.getError().c_str() );
            return;
        }
        if ( FileUtil::writeTextFile( pEntry->_absolutePath, _selectedGameDataRawText ) == false )
        {
            SW_LOG_ERROR_ID( "DataTablePanel.CouldNotWriteGame", "Could not write game data table '%#'", pEntry->_absolutePath.c_str() );
            return;
        }
        _savedGameDataRawText = _selectedGameDataRawText;
        _gameDataDocument.markSaved();
        _bGameDataDirty = SW_FALSE;
        SW_LOG_INFO_ID( "DataTablePanel.SavedGameDataTable", "Saved game data table %#", pEntry->_fileName.c_str() );
    }

    void DataTableModel::reloadGameDataFiles()
    {
        _bGameDataLoaded = SW_FALSE;
        _gameDataJob.request();
    }

    void DataTableModel::replaceGameDataRawText( string_view text )
    {
        _selectedGameDataRawText = text;
        commitGameDataRawEdit();
    }

    void DataTableModel::commitGameDataRawEdit()
    {
        _gameDataDocument.replaceText( _selectedGameDataRawText );
        _bGameDataDirty = SW_TRUE;
    }

    void DataTableModel::commitGameDataTableEdit()
    {
        _selectedGameDataRawText = _gameDataDocument.getText();
        _bGameDataDirty          = SW_TRUE;
    }
} // namespace sw::editor
