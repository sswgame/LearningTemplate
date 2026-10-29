#include "pch.h"

#include "Editor/Panels/DataTable/Model/DataTableModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

SW_LOG_CALLER( "DataTablePanel" );
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
        , _gameDataLoadCount{ 0 }
        , _bLocalizationLoaded{ false }
        , _bGameDataLoaded{ false }
        , _bLocalizationDirty{ false }
        , _bGameDataDirty{ false }
    {
    }

    void DataTableModel::pollBackgroundJobs()
    {
        LocalizationSheet    loadedSheet;
        const EditorTaskPoll localizationPoll = _localizationJob.take( loadedSheet );
        if ( localizationPoll == EditorTaskPoll::Ready )
        {
            if ( _bLocalizationDirty == false )
            {
                _localizationSheet   = std::move( loadedSheet );
                _bLocalizationLoaded = true;
            }
        }
        else if ( _bLocalizationLoaded == false && localizationPoll == EditorTaskPoll::Idle )
        {
            EditorDataTableCommands::collectLocalizationProjects( _listLocalizationProject );
            if ( _listLocalizationProject.empty() )
                _bLocalizationLoaded = true; // 프로젝트가 없다 — 빈 안내를 보인다
            else
            {
                if ( _selectedProjectIndex < 0 || static_cast<size_t>( _selectedProjectIndex ) >= _listLocalizationProject.size() )
                    _selectedProjectIndex = 0;
                _localizationJob.request( _listLocalizationProject[static_cast<size_t>( _selectedProjectIndex )] );
            }
        }

        vector<GameDataFileEntry> listGameData;
        const EditorTaskPoll      gameDataPoll = _gameDataJob.take( listGameData );
        if ( gameDataPoll == EditorTaskPoll::Ready )
        {
            _listGameDataFile = std::move( listGameData );
            _bGameDataLoaded  = true;
        }
        else if ( _bGameDataLoaded == false && gameDataPoll == EditorTaskPoll::Idle )
            _gameDataJob.request();
    }

    bool DataTableModel::save()
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
                SW_LOG_WARNING( "Some localization files could not be read - see the warnings above" );
            _bLocalizationLoaded = true;
            _bLocalizationDirty  = false;
        }
        if ( _bGameDataDirty )
        {
            _selectedGameDataRawText = _savedGameDataRawText;
            (void)_gameDataDocument.load( _selectedGameDataRawText ); // 깨진 원문도 글 상자로 고치게 열린다
            ++_gameDataLoadCount;
            _bGameDataDirty = false;
        }
    }

    void DataTableModel::selectProject( size_t projectIndex )
    {
        if ( projectIndex >= _listLocalizationProject.size() || _bLocalizationDirty )
            return;
        _selectedProjectIndex = static_cast<int32>( projectIndex );
        reloadLocalization();
    }

    void DataTableModel::markRecordModified( LocalizationRecord& record )
    {
        record._bModified   = true;
        _bLocalizationDirty = true;
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
        _bLocalizationDirty = true;
        return true;
    }

    void DataTableModel::deleteRecord( size_t recordIndex )
    {
        if ( recordIndex >= _localizationSheet._listRecord.size() )
            return;
        _localizationSheet._listRemovedKey.push_back( _localizationSheet._listRecord[recordIndex]._key );
        _localizationSheet._listRecord.erase( _localizationSheet._listRecord.begin() + static_cast<ptrdiff_t>( recordIndex ) );
        _bLocalizationDirty = true;
    }

    void DataTableModel::collectVisibleRecords( const EditorListFilter& filter, vector<size_t>& outListVisibleIndex ) const
    {
        // 호출한 쪽이 **멤버 버퍼를 다시 쓴다** — 지역 `vector` 면 프레임마다 할당하고 해제한다.
        outListVisibleIndex.clear();
        outListVisibleIndex.reserve( _localizationSheet._listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < _localizationSheet._listRecord.size(); ++recordIndex )
        {
            const LocalizationRecord& record   = _localizationSheet._listRecord[recordIndex];
            bool                      bMatches = filter.matchesAny( { record._key, record._source } );
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
        _bLocalizationDirty  = false;
        _bLocalizationLoaded = false;
    }

    void DataTableModel::saveLocalization()
    {
        // 하나라도 쓰지 못했으면 dirty 를 지우지 않는다 — 저장 확인이 다시 묻는다.
        if ( EditorDataTableCommands::saveLocalizationProject( _localizationSheet ) == false )
            return;
        _bLocalizationDirty = false;
    }

    const GameDataFileEntry* DataTableModel::findSelectedGameDataFile() const
    {
        if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
            return nullptr;
        return &_listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
    }

    void DataTableModel::selectGameDataFile( size_t fileIndex )
    {
        const bool bSelected = _selectedGameDataIndex == static_cast<int32>( fileIndex );
        if ( fileIndex >= _listGameDataFile.size() || ( _bGameDataDirty && bSelected == false ) )
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
            SW_LOG_ERROR( "Could not read game data table '%#' - it is not opened", pEntry->_absolutePath.c_str() );
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
        _bGameDataDirty = false;
    }

    void DataTableModel::saveSelectedGameDataFile()
    {
        const GameDataFileEntry* pEntry = findSelectedGameDataFile();
        if ( pEntry == nullptr )
            return;

        if ( FileUtil::writeTextFile( pEntry->_absolutePath, _selectedGameDataRawText ) == false )
        {
            SW_LOG_ERROR( "Could not write game data table '%#'", pEntry->_absolutePath.c_str() );
            return;
        }
        _savedGameDataRawText = _selectedGameDataRawText;
        _gameDataDocument.markSaved();
        _bGameDataDirty = false;
        SW_LOG_INFO( "Saved game data table %#", pEntry->_fileName.c_str() );
    }

    void DataTableModel::reloadGameDataFiles()
    {
        _bGameDataLoaded = false;
        _gameDataJob.request();
    }

    void DataTableModel::replaceGameDataRawText( string_view text )
    {
        _selectedGameDataRawText = text;
        _gameDataDocument.replaceText( _selectedGameDataRawText, false );
        _bGameDataDirty = true;
    }

    void DataTableModel::commitGameDataTableEdit()
    {
        _selectedGameDataRawText = _gameDataDocument.getText();
        _bGameDataDirty          = true;
    }
} // namespace sw::editor
