#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorDocument.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorDocumentInternal
        {
            static constexpr const utf8* kDefaultPresetPath = "editor/characterpresets.txt";
            static constexpr const utf8* kDefaultPresetName = "MyLook";
            static constexpr const utf8* kDefaultExportID   = "MyCharacter";

            /** @brief 파일 이름에 쓸 수 없는 글자를 밑줄로 바꿉니다(프리셋 이름은 저장소가 정리한 그대로 둔다). */
            static string makeFileSafeName( string_view name )
            {
                string safeName{ name };
                for ( utf8& character : safeName )
                {
                    const bool bAllowed = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) || character == '_' ||
                                          character == '-';
                    if ( bAllowed == false )
                        character = '_';
                }
                return safeName;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "CharacterEditorDocument" );

    CharacterEditorDocument::CharacterEditorDocument()
        : _sockets{}
        , _pose{}
        , _appearance{}
        , _reloadGuard{}
        , _fit{}
        , _pItems{}
        , _database{}
        , _presetStore{}
        , _paths{}
        , _status{}
        , _selectedSocket{}
        , _selectedBone{}
        , _listClipName{}
        , _selectedClip{}
        , _preview{}
        , _previewReport{}
        , _previewRevision{ 0 }
        , _packRoot{}
        , _packDataPath{}
        , _packDataMessage{}
        , _previewPresetID{}
        , _packDataState{ PackDataState::Unread }
        , _bClipNamesLoaded{ false }
        , _bKindTableReady{ false }
        , _bDatabaseLoaded{ false }
        , _bHasFit{ false }
        , _bExternalChange{ false }
        , _bPreviewValid{ false }
    {
        _paths._presetPath = CharacterEditorDocumentInternal::kDefaultPresetPath;
        _paths._presetName = CharacterEditorDocumentInternal::kDefaultPresetName;
        _paths._exportID   = CharacterEditorDocumentInternal::kDefaultExportID;
        _presetStore.setDatabase( &_database );
    }

    void CharacterEditorDocument::ensurePackData()
    {
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( _packDataState != PackDataState::Unread && packRoot == _packRoot )
            return;
        _packRoot = packRoot;
        _packDataMessage.clear();
        string error;
        if ( GamePackDataUtil::tryMakeKindPath( packRoot, CharacterEditorPackData::kDataKind, _packDataPath, error ) == false )
        {
            _packDataState   = PackDataState::Invalid;
            _packDataMessage = "Character editor data has no path: " + error;
            return;
        }
        if ( AssetIO::hasAsset( AssetID{ _packDataPath } ) == false )
        {
            _packDataState = PackDataState::Missing;
            return;
        }
        CharacterEditorPackData data;
        if ( data.loadFromAsset( AssetID{ _packDataPath } ) == false )
        {
            _packDataState   = PackDataState::Invalid;
            _packDataMessage = _packDataPath + " could not be read (see log)";
            return;
        }
        _paths._socketPath       = data._socketPath;
        _paths._skeletonPath     = data._skeletonPath;
        _paths._refPosePath      = data._refPosePath;
        _paths._itemsPath        = data._itemsPath;
        _paths._appearanceFolder = data._appearanceFolder;
        _paths._clipFolder       = data._clipFolder;
        _paths._rigPath          = data._rigPath;
        _bClipNamesLoaded        = false; // 팩 데이터가 바뀌면 클립 목록을 다시 모은다
        _selectedClip            = hashed_string{};
        _previewPresetID         = hashed_string( string_view( data._previewPresetID ) );
        // 프리셋 내보내기 폴더는 외형 폴더에서 파생한다(기준은 팩 데이터의 외형 폴더 하나).
        const string exportFolder = FileUtil::joinPath( data._appearanceFolder, AppearanceDatabase::kPresetFolder );
        _paths._exportFolder      = string_view( exportFolder );
        _packDataState            = PackDataState::Ready;
    }

    void CharacterEditorDocument::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = true;
        _reloadGuard.reset( SharedAssetCache<SocketSet>::getReloadCount() );
        SocketKindTable kinds;
        if ( kinds.loadDefault() == false )
            SW_LOG_WARNING( "Default socket kind table could not be loaded - kinds are not validated" );
        else
            _sockets.setKindTable( kinds );

        MirrorNameRules rules;
        if ( rules.loadFromAsset( AssetID{ MirrorNameRules::kDefaultPath } ) == false )
            SW_LOG_WARNING( "Default mirror name table could not be loaded - left/right pairs are not found" );
        else
            _sockets.setMirrorRules( rules );
    }

    bool CharacterEditorDocument::loadSocketFile()
    {
        ensureKindTable(); // 문서는 첫 그리기 전에 열릴 수 있다(시나리오 · 콘텐츠 브라우저) — 종류 표 없이 읽으면 모든 kind 가 모르는 종류다
        const string readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._socketPath.c_str() ) ).getDiskPath();
        if ( readPath.empty() )
        {
            _status = "Socket file '" + string{ _paths._socketPath.c_str() } + "' has no file on disk to load";
            return false;
        }
        if ( _sockets.loadFromFile( readPath ) == false )
        {
            _status = "Socket file '" + readPath + "' could not be loaded (see log)";
            return false;
        }
        _status.clear();
        _selectedSocket  = hashed_string{};
        _bExternalChange = false;
        _reloadGuard.reset( SharedAssetCache<SocketSet>::getReloadCount() );
        return true;
    }

    void CharacterEditorDocument::pollExternalChange( float64 nowSeconds )
    {
        if ( _reloadGuard.pollExternalChange( SharedAssetCache<SocketSet>::getReloadCount(), nowSeconds ) )
            _bExternalChange = true;
        if ( _bExternalChange && _paths._socketPath.empty() )
            _bExternalChange = false; // 열어 둔 소켓 파일이 없으면 반영할 모델이 없다
        // 고치지 않은 모델은 바뀐 파일을 바로 다시 읽는다. 읽기가 실패하면 표시가 남고 화면이 다시 읽기 단추를 낸다.
        const bool bCanAutoReload = _bExternalChange && _sockets.isDirty() == false;
        if ( bCanAutoReload && loadSocketFile() )
            _bExternalChange = false;
    }

    bool CharacterEditorDocument::saveFiles( float64 nowSeconds )
    {
        bool bSaved = true;
        if ( _sockets.isDirty() )
        {
            const std::optional<AbsolutePath> writePath = editor::getSubsystem<AssetPaths>().resolveWritePath( string_view( _paths._socketPath.c_str() ) );
            if ( writePath.has_value() == false || _sockets.saveToFile( *writePath ) == false )
            {
                _status = "Socket file could not be saved";
                bSaved  = false;
            }
            else
            {
                _reloadGuard.onSaved( SharedAssetCache<SocketSet>::getReloadCount(), nowSeconds ); // 파일 감시가 곧 같은 파일을 다시 읽는다 — 그건 외부 변경이 아니다
                SW_LOG_INFO( "Socket file saved to %#", writePath->c_str() );
            }
        }
        if ( _pose.isDirty() )
        {
            const std::optional<AbsolutePath> writePath = editor::getSubsystem<AssetPaths>().resolveWritePath( string_view( _paths._refPosePath.c_str() ) );
            if ( writePath.has_value() == false || _pose.saveToFile( *writePath ) == false )
            {
                _status = "Reference pose file could not be saved";
                bSaved  = false;
            }
            else
                SW_LOG_INFO( "Reference pose file saved to %#", writePath->c_str() );
        }
        if ( bSaved )
            _status.clear();
        return bSaved;
    }

    void CharacterEditorDocument::revert()
    {
        if ( _sockets.isDirty() && _sockets.revertToSaved() == false )
            SW_LOG_ERROR( "Character Editor: the socket edits could not be discarded - the saved state did not read back" );
        if ( _pose.isDirty() && _pose.revertToSaved() == false )
            SW_LOG_ERROR( "Character Editor: the reference pose edits could not be discarded - the saved state did not read back" );
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorDocument::clearSockets()
    {
        _sockets.clear();
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorDocument::loadReferencePose()
    {
        const string readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._refPosePath.c_str() ) ).getDiskPath();
        if ( readPath.empty() )
            _status = "Reference pose file was not found - edit bones and Save to create it";
        else if ( _pose.loadFromFile( readPath ) == false )
            _status = "Reference pose file could not be loaded (see log)";
        else
            _status.clear();
    }

    void CharacterEditorDocument::loadSkeleton()
    {
        if ( _pose.loadSkeleton( AssetID{ string_view( _paths._skeletonPath.c_str() ) } ) == false )
        {
            _status = "Skeleton could not be loaded (see log)";
            return;
        }
        _status.clear();
        _selectedBone = hashed_string{};
        vector<hashed_string> listBoneName;
        _pose.collectBoneNames( listBoneName );
        _sockets.setBoneNames( listBoneName ); // 이제 소켓 부모 본 이름을 검증한다
    }

    void CharacterEditorDocument::addSocket( const hashed_string& name )
    {
        const bool bAdded = _sockets.addSocket( name, hashed_string{}, hashed_string{}, _status );
        reportEdit( bAdded );
        if ( bAdded )
            _selectedSocket = name;
    }

    void CharacterEditorDocument::renameSocket( const hashed_string& name, const hashed_string& newName )
    {
        const bool bDone = _sockets.renameSocket( name, newName, _status );
        reportEdit( bDone );
        if ( bDone )
            _selectedSocket = newName;
    }

    void CharacterEditorDocument::mirrorSocket( const hashed_string& name )
    {
        hashed_string mirrorName;
        const bool    bDone = _sockets.mirrorSocket( name, mirrorName, _status );
        reportEdit( bDone );
        if ( bDone )
            _selectedSocket = mirrorName;
    }

    void CharacterEditorDocument::removeSocket( const hashed_string& name )
    {
        reportEdit( _sockets.removeSocket( name, _status ) );
        _selectedSocket = hashed_string{};
    }

    void CharacterEditorDocument::mirrorSelectedBone()
    {
        string        error;
        hashed_string mirrored;
        if ( _pose.mirrorBone( _selectedBone, _sockets.getMirrorRules(), mirrored, error ) )
            _selectedBone = mirrored;
        if ( error.empty() == false )
            _status = error;
    }

    void CharacterEditorDocument::loadAppearanceData()
    {
        _bDatabaseLoaded = false;
        _bPreviewValid   = false;
        _database.clear();
        _pItems = make_unique<ItemCatalog>();
        if ( _pItems->loadFromAsset( AssetID{ string_view( _paths._itemsPath.c_str() ) } ) == false )
        {
            _status = "Items file could not be loaded (see log)";
            return;
        }
        if ( _database.loadFromFolder( string_view( _paths._appearanceFolder.c_str() ), _pItems.get() ) == false )
        {
            _status = "Appearance data has errors - " + _database.getReport().joined();
            return;
        }
        _status.clear();
        _bDatabaseLoaded = true;
    }

    void CharacterEditorDocument::loadPresets()
    {
        const string readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._presetPath.c_str() ) ).getDiskPath();
        if ( readPath.empty() || AppearanceEditModel::loadPresetFile( _presetStore, readPath ) == false )
            _status = "Preset file could not be loaded (needs appearance data; see log)";
        else
            _status.clear();
    }

    void CharacterEditorDocument::savePreset()
    {
        const std::optional<AbsolutePath> writePath = editor::getSubsystem<AssetPaths>().resolveWritePath( string_view( _paths._presetPath.c_str() ) );
        if ( writePath.has_value() == false || _appearance.saveToPresetFile( _presetStore, string_view( _paths._presetName.c_str() ), *writePath ) == false )
            _status = "Preset could not be saved (needs a name and appearance data)";
        else
            _status.clear();
    }

    void CharacterEditorDocument::applyPreset( string_view name )
    {
        (void)_appearance.loadFromPreset( _presetStore, name ); // 목록의 이름이라 항상 있다
        _paths._presetName = name;
    }

    bool CharacterEditorDocument::makeThumbnailID( string& outThumbnailID )
    {
        const string name{ _paths._presetName.c_str() };
        if ( _presetStore.findPresetByName( name ) == nullptr )
        {
            _status = "Save the preset first - a thumbnail belongs to a saved preset";
            return false;
        }
        outThumbnailID = FileUtil::removeExtension( string_view( _paths._presetPath.c_str() ) ) + "." + CharacterEditorDocumentInternal::makeFileSafeName( name ) + ".png";
        return true;
    }

    bool CharacterEditorDocument::attachThumbnail( const string& thumbnailID )
    {
        const string                      name{ _paths._presetName.c_str() };
        const std::optional<AbsolutePath> presetFile = editor::getSubsystem<AssetPaths>().resolveWritePath( string_view( _paths._presetPath.c_str() ) );
        const bool                        bAttached  = presetFile.has_value() && _presetStore.setThumbnailPath( name, thumbnailID );
        if ( bAttached == false || _appearance.saveToPresetFile( _presetStore, name, *presetFile ) == false )
        {
            _status = "The thumbnail was requested but the preset file could not be updated";
            return false;
        }
        _status.clear();
        return true;
    }

    bool CharacterEditorDocument::exportDataPreset()
    {
        const hashed_string    presetID( string_view( _paths._exportID.c_str() ) );
        CharacterAppearanceDef def;
        string                 reason;
        XMLDocument            doc;
        const bool             bMade = AppearanceSelectionUtil::makePresetDef( _database, _appearance.getSelection(), presetID, def, reason ) &&
                           CharacterAppearanceCatalog::writePresetToNode( def, doc.appendRoot( "CharacterAppearance" ), reason );
        if ( bMade == false )
        {
            _status = "Data preset could not be made - " + reason;
            return false;
        }
        const string                      assetID   = FileUtil::joinPath( string_view( _paths._exportFolder.c_str() ), string( presetID.c_str() ) + AppearanceDatabase::kPresetExtension );
        const std::optional<AbsolutePath> writePath = editor::getSubsystem<AssetPaths>().resolveWritePath( assetID );
        const bool                        bWritten  = writePath.has_value() && FileUtil::ensureParentDirectoryExists( writePath->str() ) && doc.saveFile( writePath->str() );
        if ( bWritten == false )
        {
            _status = "Data preset could not be written to " + assetID;
            return false;
        }
        // 이 창의 데이터와 실행 중인 게임의 데이터에 올린다 — 씬의 컴포넌트는 게임 서비스(`AppearanceDatabase`)에서 프리셋을 푼다.
        // 쓰기는 디스크 경로, 읽기는 리소스 id 다(리소스 입구는 OS 경로를 받지 않는다).
        if ( _database.loadPresetFile( AssetID{ assetID } ) == false )
        {
            _status = "Exported preset has errors - " + _database.getReport().joined();
            return false;
        }
        AppearanceDatabase* pGameData = game::findSubsystem<AppearanceDatabase>();
        if ( pGameData == nullptr )
            SW_LOG_WARNING( "Exported appearance preset '%#' - this game has no AppearanceDatabase service, so scenes use it after the game loads it", presetID.c_str() );
        else if ( pGameData->loadPresetFile( AssetID{ assetID } ) == false )
        {
            _status = "The game's appearance data rejected the preset - " + pGameData->getReport().joined();
            return false;
        }
        _status.clear();
        SW_LOG_INFO( "Exported appearance preset '%#' to '%#'", presetID.c_str(), assetID.c_str() );
        return true;
    }

    AppearanceSelection CharacterEditorDocument::makePreviewSelection() const
    {
        AppearanceSelection selection = _appearance.getSelection();
        if ( selection._basePresetID.empty() )
            selection._basePresetID = _previewPresetID;
        return selection;
    }

    const CustomizationSchemaDef* CharacterEditorDocument::findCustomizationSchema( const AppearanceSelection& selection ) const
    {
        if ( _bDatabaseLoaded == false )
            return nullptr;
        hashed_string schemaID = selection._schema;
        if ( schemaID.empty() )
        {
            const CharacterAppearanceDef* pPreset = _database.getPresets().findPreset( selection._basePresetID.empty() ? _previewPresetID : selection._basePresetID );
            if ( pPreset != nullptr )
                schemaID = pPreset->_schema;
        }
        return schemaID.empty() ? nullptr : _database.getSchemas().findSchema( schemaID );
    }

    bool CharacterEditorDocument::hasBasePresetInData() const
    {
        return _database.getPresets().findPreset( _appearance.getSelection()._basePresetID ) != nullptr;
    }

    void CharacterEditorDocument::refreshResolvedPreview()
    {
        const bool bStale = _bPreviewValid == false || _previewRevision != _appearance.getRevision();
        if ( bStale == false )
            return;
        _preview       = ResolvedAppearance{};
        _previewReport = AppearanceSelectionReport{};
        _appearance.previewResolved( _database, _preview, _previewReport );
        _previewRevision = _appearance.getRevision();
        _bPreviewValid   = true;
    }

    uint32 CharacterEditorDocument::countEquippedSlots() const
    {
        uint32 equipped = 0;
        for ( const AppearanceSlotRequest& request : _appearance.getSelection()._listSlot )
        {
            if ( request._itemID.empty() == false )
                ++equipped;
        }
        return equipped;
    }

    void CharacterEditorDocument::setFitResult( const FitResult& result )
    {
        _fit     = FitCheckSummary::fromResult( result );
        _bHasFit = true;
    }

    const vector<hashed_string>& CharacterEditorDocument::getClipNames()
    {
        if ( _bClipNamesLoaded == false )
            loadClipNames();
        return _listClipName;
    }

    void CharacterEditorDocument::loadClipNames()
    {
        _listClipName.clear();
        _bClipNamesLoaded = true;
        if ( _paths._clipFolder.empty() )
            return;
        // 클립 목록은 디스크의 폴더를 훑어 얻는다 — 에디터는 원본 트리에서 도는 도구다(배포 팩에는 폴더 목록이 없어 목록이 빈다).
        const string   diskFolder = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._clipFolder.c_str() ) ).getDiskPath();
        vector<string> listFile;
        if ( diskFolder.empty() || FileUtil::collectFiles( diskFolder, string( AnimationClip::kExtension ), listFile, false ) == false )
            return;
        const size_t extensionLength = AnimationClip::kExtension.size();
        for ( const string& filePath : listFile )
        {
            const string fileName = FileUtil::getFileNamePart( filePath );
            if ( fileName.size() > extensionLength )
                _listClipName.push_back( hashed_string( string_view( fileName ).substr( 0, fileName.size() - extensionLength ) ) );
        }
        VectorUtil::sort( _listClipName, []( const hashed_string& left, const hashed_string& right )
        { return string_view( left.c_str() ) < string_view( right.c_str() ); } );
    }

    void CharacterEditorDocument::reportEdit( bool bSucceeded )
    {
        if ( bSucceeded )
            _status.clear();
    }
} // namespace sw::editor
