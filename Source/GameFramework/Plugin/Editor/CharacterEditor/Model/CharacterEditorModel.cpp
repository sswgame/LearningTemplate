#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/AnimationSet.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorPackData.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorModelInternal
        {
            static constexpr const utf8* kDefaultPresetName = "MyLook";
            static constexpr const utf8* kDefaultExportID   = "MyCharacter";
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorModel::CharacterEditorModel()
        : _sockets{}
        , _pose{}
        , _appearance{}
        , _bodyShapes{}
        , _bodyProfiles{}
        , _visuals{}
        , _reloadGuard{}
        , _fit{}
        , _pItems{}
        , _database{}
        , _presetStore{}
        , _stagedPresetStore{}
        , _stagedSelection{}
        , _thumbnailPresetName{}
        , _thumbnailSlot{}
        , _presetRequest{}
        , _thumbnailRequest{}
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
        , _gizmoTarget{ CharacterGizmoTarget::None }
        , _bClipNamesLoaded{ SW_FALSE }
        , _bKindTableReady{ SW_FALSE }
        , _bDatabaseLoaded{ SW_FALSE }
        , _bHasFit{ SW_FALSE }
        , _bExternalChange{ SW_FALSE }
        , _bPreviewValid{ SW_FALSE }
        , _bStagedSelection{ SW_FALSE }
    {
        _paths._presetName = CharacterEditorModelInternal::kDefaultPresetName;
        _paths._exportID   = CharacterEditorModelInternal::kDefaultExportID;
        _presetStore.setDatabase( &_database );
    }

    void CharacterEditorModel::ensurePackData()
    {
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( _packDataState != PackDataState::Unread && packRoot == _packRoot )
            return;
        _packRoot = packRoot;
        _packDataMessage.clear();
        if ( const expected<void> path = GamePackDataUtil::tryMakeKindPath( packRoot, CharacterEditorPackData::kDataKind, _packDataPath ); path.has_value() == false )
        {
            _packDataState   = PackDataState::Invalid;
            _packDataMessage = "Character editor data has no path: " + string( path.error().message() ) + " (" + string( path.error().detail() ) + ")";
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
        _paths._animationSet     = data._animationSet;
        _paths._rigPath          = data._rigPath;
        _bClipNamesLoaded        = SW_FALSE; // 팩 데이터가 바뀌면 클립 목록을 다시 모은다
        _selectedClip            = hashed_string{};
        _previewPresetID         = hashed_string( string_view( data._previewPresetID ) );
        // 프리셋 내보내기 폴더는 외형 폴더에서 파생한다(기준은 팩 데이터의 외형 폴더 하나).
        const string exportFolder = FileUtil::joinPath( data._appearanceFolder, AppearanceDatabase::kPresetFolder );
        _paths._exportFolder      = string_view( exportFolder );
        _packDataState            = PackDataState::Ready;
    }

    void CharacterEditorModel::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = SW_TRUE;
        _reloadGuard.reset();
        SocketKindTable kinds;
        if ( kinds.loadDefault() == false )
            SW_LOG_WARNING_ID( "CharacterEditorModel.DefaultSocketKindTable", "Default socket kind table could not be loaded - kinds are not validated" );
        else
            _sockets.setKindTable( kinds );

        MirrorNameRules rules;
        if ( rules.loadFromAsset( AssetID{ MirrorNameRules::kDefaultPath } ) == false )
            SW_LOG_WARNING_ID( "CharacterEditorModel.DefaultMirrorNameTable", "Default mirror name table could not be loaded - left/right pairs are not found" );
        else
            _sockets.setMirrorRules( rules );
    }

    bool CharacterEditorModel::loadSocketFile()
    {
        ensureKindTable(); // 문서는 첫 그리기 전에 열릴 수 있다(시나리오 · 콘텐츠 브라우저) — 종류 표 없이 읽으면 모든 kind 가 모르는 종류다
        const auto readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._socketPath.c_str() ) );
        if ( readPath.has_value() == false )
        {
            _status = "Socket file '" + string{ _paths._socketPath.c_str() } + "' has no file on disk to load";
            return false;
        }
        if ( _sockets.loadFromFile( readPath->str() ) == false )
        {
            _status = "Socket file '" + readPath->str() + "' could not be loaded (see log)";
            return false;
        }
        _status.clear();
        _selectedSocket  = hashed_string{};
        _bExternalChange = SW_FALSE;
        _reloadGuard.reset();
        return true;
    }

    void CharacterEditorModel::pollExternalChange( float64 nowSeconds )
    {
        if ( _reloadGuard.pollExternalChange( nowSeconds ) )
            _bExternalChange = SW_TRUE;
        if ( _bExternalChange && _paths._socketPath.empty() )
            _bExternalChange = SW_FALSE; // 열어 둔 소켓 파일이 없으면 반영할 모델이 없다
        // 고치지 않은 모델은 바뀐 파일을 바로 다시 읽는다. 읽기가 실패하면 표시가 남고 화면이 다시 읽기 단추를 낸다.
        const bool bCanAutoReload = _bExternalChange && _sockets.isDirty() == false;
        if ( bCanAutoReload && loadSocketFile() )
            _bExternalChange = SW_FALSE;
    }

    void CharacterEditorModel::openBodyDataFile( BodyDataEditModel& model, string_view assetPath )
    {
        if ( assetPath.empty() )
        {
            if ( model.isDirty() == false )
                model.close();
            return;
        }
        const auto diskPath = editor::getSubsystem<AssetPaths>().toDiskPath( assetPath );
        if ( diskPath.has_value() == false )
        {
            _status = string( "Body data file '" ) + string( assetPath ) + "' has no disk path";
            return;
        }
        if ( model.isDirty() || ( model.isOpen() && StringUtil::equals( model.getDiskPath(), diskPath->c_str() ) ) )
            return; // 고친 파일은 다시 읽지 않는다 — 같은 파일은 이미 열렸다
        if ( model.openFile( diskPath->c_str() ) == false )
            _status = model.getError();
    }

    void CharacterEditorModel::openBodyData( const hashed_string& bodyShapesAsset )
    {
        const string folder( _paths._appearanceFolder.c_str() );
        openBodyDataFile( _bodyShapes, bodyShapesAsset.empty() ? string_view{} : bodyShapesAsset.view() );
        openBodyDataFile( _bodyProfiles, folder.empty() ? string_view{} : string_view( FileUtil::joinPath( folder, "bodyprofiles.xml" ) ) );
        openBodyDataFile( _visuals, folder.empty() ? string_view{} : string_view( FileUtil::joinPath( folder, "itemvisuals.xml" ) ) );
    }

    bool CharacterEditorModel::saveBodyData()
    {
        bool       bSaved          = true;
        const bool bProfileChanged = _bodyProfiles.isDirty() || _visuals.isDirty();
        for ( BodyDataEditModel* pModel : { &_bodyShapes, &_bodyProfiles, &_visuals } )
        {
            if ( pModel->isDirty() == false )
                continue;
            if ( pModel->save() == false )
            {
                _status = pModel->getError();
                bSaved  = false;
                continue;
            }
            SW_LOG_INFO_ID( "CharacterEditorModel.BodyDataSaved", "Body data file saved to %#", pModel->getDiskPath().c_str() );
        }
        // 몸 프로필 · 구간은 이 창의 외형 데이터가 읽은 것이다 — 다시 읽어야 미리보기 몸이 새 값을 입는다(채널 매핑은 공유 캐시의 파일 감시가 다시 읽는다).
        if ( bSaved && bProfileChanged )
            loadAppearanceData();
        return bSaved;
    }

    bool CharacterEditorModel::saveFiles( float64 nowSeconds )
    {
        bool bSaved = true;
        if ( _sockets.isDirty() )
        {
            const auto writePath = editor::getSubsystem<AssetPaths>().toDiskPath( string_view( _paths._socketPath.c_str() ) );
            if ( writePath.has_value() == false || _sockets.saveDocument( *writePath ) == false )
            {
                _status = "Socket file could not be saved";
                bSaved  = false;
            }
            else
            {
                _reloadGuard.onSaved( nowSeconds ); // 파일 감시가 곧 같은 파일을 다시 읽는다 — 그건 외부 변경이 아니다
                SW_LOG_INFO_ID( "CharacterEditorModel.SocketFileSaved", "Socket file saved to %#", writePath->c_str() );
            }
        }
        if ( _pose.isDirty() )
        {
            const auto writePath = editor::getSubsystem<AssetPaths>().toDiskPath( string_view( _paths._refPosePath.c_str() ) );
            if ( writePath.has_value() == false || _pose.saveDocument( *writePath ) == false )
            {
                _status = "Reference pose file could not be saved";
                bSaved  = false;
            }
            else
                SW_LOG_INFO_ID( "CharacterEditorModel.ReferencePoseFileSaved", "Reference pose file saved to %#", writePath->c_str() );
        }
        if ( bSaved )
            _status.clear();
        return bSaved;
    }

    void CharacterEditorModel::revert()
    {
        if ( _sockets.isDirty() && _sockets.revertToSaved() == false )
            SW_LOG_ERROR_ID( "CharacterEditorModel.CharacterEditorSocketEdits", "Character Editor: the socket edits could not be discarded - the saved state did not read back" );
        if ( _pose.isDirty() && _pose.revertToSaved() == false )
            SW_LOG_ERROR_ID( "CharacterEditorModel.CharacterEditorReferencePose", "Character Editor: the reference pose edits could not be discarded - the saved state did not read back" );
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorModel::clearSockets()
    {
        _sockets.clear();
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorModel::loadReferencePose()
    {
        const auto readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._refPosePath.c_str() ) );
        if ( readPath.has_value() == false )
            _status = "Reference pose file was not found - edit bones and Save to create it";
        else if ( _pose.loadFromFile( readPath->str() ) == false )
            _status = "Reference pose file could not be loaded (see log)";
        else
            _status.clear();
    }

    void CharacterEditorModel::loadSkeleton()
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

    void CharacterEditorModel::addSocket( const hashed_string& name )
    {
        const expected<void> added = _sockets.addSocket( name, hashed_string{}, hashed_string{} );
        reportEdit( added );
        if ( added.has_value() )
            _selectedSocket = name;
    }

    void CharacterEditorModel::renameSocket( const hashed_string& name, const hashed_string& newName )
    {
        const expected<void> renamed = _sockets.renameSocket( name, newName );
        reportEdit( renamed );
        if ( renamed.has_value() )
            _selectedSocket = newName;
    }

    void CharacterEditorModel::mirrorSocket( const hashed_string& name )
    {
        const expected<hashed_string> mirrored = _sockets.mirrorSocket( name );
        if ( mirrored.has_value() == false )
        {
            reportEdit( expected<void>{ unexpected( mirrored.error() ) } );
            return;
        }
        reportEdit( expected<void>{} );
        _selectedSocket = *mirrored;
    }

    void CharacterEditorModel::removeSocket( const hashed_string& name )
    {
        reportEdit( _sockets.removeSocket( name ) );
        _selectedSocket = hashed_string{};
    }

    void CharacterEditorModel::mirrorSelectedBone()
    {
        const expected<hashed_string> mirrored = _pose.mirrorBone( _selectedBone, _sockets.getMirrorRules() );
        if ( mirrored.has_value() == false )
        {
            _status = EditorWidgets::makeStatusText( mirrored.error() );
            return;
        }
        _selectedBone = *mirrored;
    }

    void CharacterEditorModel::loadAppearanceData()
    {
        _bDatabaseLoaded = SW_FALSE;
        _bPreviewValid   = SW_FALSE;
        _database.clear();
        _pItems = SharedAssetCache<ItemCatalog>::acquire( string_view( _paths._itemsPath.c_str() ) );
        if ( _pItems == nullptr )
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
        _bDatabaseLoaded = SW_TRUE;
    }

    CharacterEditorLocalStore* CharacterEditorModel::findLocalStore()
    {
        CharacterEditorLocalStore* const pStore = editor::findSubsystem<CharacterEditorLocalStore>(); // 열지 못했으면 없다(오류 로그가 남았다)
        if ( pStore == nullptr )
            _status = "The character editor local store is not open (see log) - presets are unavailable";
        return pStore;
    }

    void CharacterEditorModel::loadPresets()
    {
        if ( isPresetRequestPending() )
        {
            _status = "A preset request is still running";
            return;
        }
        CharacterEditorLocalStore* const pStore = findLocalStore();
        if ( pStore == nullptr )
            return;
        _presetRequest = pStore->getChannel().submitRead( CharacterEditorLocalStore::kPresetSlot,
                                                          LocalStoreChannel::CompletionFunction::create<&CharacterEditorModel::onPresetsRead>( this ) );
        _status        = "Loading presets...";
    }

    void CharacterEditorModel::onPresetsRead( const LocalStoreCompletion& completion )
    {
        if ( completion._result == LocalStoreResult::NotFound )
        {
            _presetStore.clear();
            _status = "No presets saved yet";
            return;
        }
        const bool bRead = completion._result == LocalStoreResult::Ok &&
                           _presetStore.loadFromXMLText( string_view( reinterpret_cast<const utf8*>( completion._bytes.data() ), completion._bytes.size() ) );
        if ( bRead == false )
        {
            _status = "Preset slot could not be read (needs appearance data; see log)";
            return;
        }
        _status.clear();
    }

    void CharacterEditorModel::savePreset()
    {
        const string_view name{ _paths._presetName.c_str() };
        if ( name.empty() || isPresetRequestPending() )
        {
            _status = name.empty() ? "The preset needs a name" : "A preset request is still running";
            return;
        }
        UserAppearancePresetStore staged = _presetStore;
        (void)staged.savePreset( name, _appearance.getSelection() ); // 새 이름이면 붙이고 있으면 바꾼다(번호는 쓰지 않는다)
        const AppearanceSelection selection = _appearance.getSelection();
        submitPresetWrite( std::move( staged ), &selection );
    }

    void CharacterEditorModel::submitPresetWrite( UserAppearancePresetStore staged, const AppearanceSelection* pSavedSelection )
    {
        CharacterEditorLocalStore* const pStore = findLocalStore();
        if ( pStore == nullptr )
            return;
        vector<uint8> bytes;
        (void)staged.writeBytes( bytes ); // 글로 쓰기는 늘 된다
        _stagedPresetStore = std::move( staged );
        _bStagedSelection  = pSavedSelection != nullptr;
        if ( pSavedSelection != nullptr )
            _stagedSelection = *pSavedSelection;
        _presetRequest = pStore->getChannel().submitWrite( CharacterEditorLocalStore::kPresetSlot, std::move( bytes ), LocalStoreWriteOptions{},
                                                           LocalStoreChannel::CompletionFunction::create<&CharacterEditorModel::onPresetsWritten>( this ) );
        _status        = "Saving presets...";
    }

    void CharacterEditorModel::onPresetsWritten( const LocalStoreCompletion& completion )
    {
        if ( completion._result != LocalStoreResult::Ok )
        {
            // 목록과 "저장된 상태" 를 그대로 둔다 — 저장된 줄 알고 넘어가지 않게.
            _status = "Preset slot could not be written (see log)";
            SW_LOG_ERROR_ID( "CharacterEditorModel.CharacterEditorPresetSlot", "Character Editor: preset slot %# could not be written (result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
            return;
        }
        _presetStore = std::move( _stagedPresetStore );
        _presetStore.setDatabase( &_database );
        if ( _bStagedSelection )
            _appearance.markSaved( _stagedSelection );
        _status.clear();
        SW_LOG_INFO_ID( "CharacterEditor.PresetsSaved", "Character Editor: presets saved to slot %# (%# presets)", completion._slot.c_str(), static_cast<uint32>( _presetStore.getPresets().size() ) );
    }

    void CharacterEditorModel::applyPreset( string_view name )
    {
        (void)_appearance.loadFromPreset( _presetStore, name ); // 목록의 이름이라 항상 있다
        _paths._presetName = name;
    }

    bool CharacterEditorModel::canAttachThumbnail()
    {
        if ( _presetStore.findPresetByName( string_view( _paths._presetName.c_str() ) ) == nullptr )
        {
            _status = "Save the preset first - a thumbnail belongs to a saved preset";
            return false;
        }
        if ( isPresetRequestPending() )
        {
            _status = "A preset request is still running";
            return false;
        }
        return findLocalStore() != nullptr;
    }

    void CharacterEditorModel::attachThumbnail( string_view presetName, vector<uint8> pngBytes )
    {
        CharacterEditorLocalStore* const pStore = findLocalStore();
        if ( pStore == nullptr || isPresetRequestPending() )
            return;
        _thumbnailPresetName = string{ presetName };
        _thumbnailSlot       = CharacterEditorLocalStore::makeThumbnailSlot( presetName );
        _thumbnailRequest    = pStore->getChannel().submitWrite( _thumbnailSlot, std::move( pngBytes ), LocalStoreWriteOptions{},
                                                                 LocalStoreChannel::CompletionFunction::create<&CharacterEditorModel::onThumbnailWritten>( this ) );
        _status              = "Saving the thumbnail...";
    }

    void CharacterEditorModel::onThumbnailWritten( const LocalStoreCompletion& completion )
    {
        UserAppearancePresetStore staged      = _presetStore;
        const bool                bAttachable = completion._result == LocalStoreResult::Ok && staged.setThumbnailSlot( _thumbnailPresetName, _thumbnailSlot );
        if ( bAttachable == false )
        {
            _status = "The thumbnail could not be stored (the slot write failed or the preset was removed)";
            SW_LOG_ERROR_ID( "CharacterEditorModel.CharacterEditorThumbnailSlot", "Character Editor: thumbnail slot %# of preset '%#' could not be stored (result %#)", _thumbnailSlot.c_str(), _thumbnailPresetName.c_str(),
                             static_cast<int32>( completion._result ) );
            return;
        }
        SW_LOG_INFO_ID( "CharacterEditor.ThumbnailStored", "Character Editor: thumbnail of preset '%#' stored in slot %#", _thumbnailPresetName.c_str(), _thumbnailSlot.c_str() );
        submitPresetWrite( std::move( staged ), nullptr );
    }

    bool CharacterEditorModel::exportDataPreset()
    {
        const hashed_string    presetID( string_view( _paths._exportID.c_str() ) );
        CharacterAppearanceDef def;
        XMLDocument            doc;
        const expected<void>   made = AppearanceSelection::makePresetDef( _database, _appearance.getSelection(), presetID, def ).and_then( [&]
        {
            return CharacterAppearanceCatalog::writePresetToNode( def, doc.appendRoot( "CharacterAppearance" ) );
        } );
        if ( made.has_value() == false )
        {
            _status = "Data preset could not be made - " + string( made.error().message() ) + ( made.error().detail().empty() ? string() : " (" + string( made.error().detail() ) + ")" );
            return false;
        }
        const string assetID   = FileUtil::joinPath( string_view( _paths._exportFolder.c_str() ), string( presetID.c_str() ) + AppearanceDatabase::kPresetExtension );
        const auto   writePath = editor::getSubsystem<AssetPaths>().toDiskPath( assetID );
        const bool   bWritten  = writePath.has_value() && FileUtil::ensureParentDirectoryExists( writePath->str() ) && doc.saveToFile( writePath->str() );
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
            SW_LOG_WARNING_ID( "CharacterEditorModel.ExportedAppearancePresetGame", "Exported appearance preset '%#' - this game has no AppearanceDatabase service, so scenes use it after the game loads it", presetID.c_str() );
        else if ( pGameData->loadPresetFile( AssetID{ assetID } ) == false )
        {
            _status = "The game's appearance data rejected the preset - " + pGameData->getReport().joined();
            return false;
        }
        _status.clear();
        SW_LOG_INFO_ID( "CharacterEditor.PresetExported", "Exported appearance preset '%#' to '%#'", presetID.c_str(), assetID.c_str() );
        return true;
    }

    AppearanceSelection CharacterEditorModel::makePreviewSelection() const
    {
        AppearanceSelection selection = _appearance.getSelection();
        if ( selection._basePresetID.empty() )
            selection._basePresetID = _previewPresetID;
        return selection;
    }

    const CustomizationSchemaDef* CharacterEditorModel::findCustomizationSchema( const AppearanceSelection& selection ) const
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

    bool CharacterEditorModel::hasBasePresetInData() const
    {
        return _database.getPresets().findPreset( _appearance.getSelection()._basePresetID ) != nullptr;
    }

    void CharacterEditorModel::refreshResolvedPreview()
    {
        const bool bStale = _bPreviewValid == false || _previewRevision != _appearance.getRevision();
        if ( bStale == false )
            return;
        _preview       = ResolvedAppearance{};
        _previewReport = AppearanceSelectionReport{};
        _appearance.previewResolved( _database, _preview, _previewReport );
        _previewRevision = _appearance.getRevision();
        _bPreviewValid   = SW_TRUE;
    }

    uint32 CharacterEditorModel::countEquippedSlots() const
    {
        uint32 equipped = 0;
        for ( const AppearanceSlotRequest& request : _appearance.getSelection()._listSlot )
        {
            if ( request._itemID.empty() == false )
                ++equipped;
        }
        return equipped;
    }

    void CharacterEditorModel::setFitResult( const FitResult& result )
    {
        _fit     = FitCheckSummary::fromResult( result );
        _bHasFit = SW_TRUE;
    }

    const vector<hashed_string>& CharacterEditorModel::getClipNames()
    {
        if ( _bClipNamesLoaded == false )
            loadClipNames();
        return _listClipName;
    }

    void CharacterEditorModel::loadClipNames()
    {
        _listClipName.clear();
        _bClipNamesLoaded = SW_TRUE;
        if ( _paths._animationSet.empty() )
            return;
        // 런타임과 같은 공유 캐시의 한 벌을 읽는다(사본을 읽지 않는다).
        const shared_ptr<const AnimationSet> pAnimationSet = SharedAssetCache<AnimationSet>::acquire( string_view( _paths._animationSet.c_str() ) );
        if ( pAnimationSet == nullptr )
            return;                                                          // 셋이 까닭을 남겼다
        for ( const AnimationSetEntry& entry : pAnimationSet->getEntries() ) // 표는 이름순이다
        {
            _listClipName.push_back( hashed_string( string_view{ entry._name } ) );
        }
    }

    void CharacterEditorModel::selectSocket( const hashed_string& name )
    {
        _selectedSocket = name;
        if ( name.empty() == false )
            _gizmoTarget = CharacterGizmoTarget::Socket;
    }

    void CharacterEditorModel::selectBone( const hashed_string& name )
    {
        _selectedBone = name;
        if ( name.empty() == false )
            _gizmoTarget = CharacterGizmoTarget::Bone;
    }

    CharacterGizmoTarget CharacterEditorModel::getGizmoTarget() const
    {
        // 주인을 지웠으면(소켓 삭제 · 본 해제) 다른 쪽이 남아 있어도 기즈모를 두지 않는다 — 고르지 않은 것을 끌게 하지 않는다.
        if ( _gizmoTarget == CharacterGizmoTarget::Socket && _selectedSocket.empty() )
            return CharacterGizmoTarget::None;
        if ( _gizmoTarget == CharacterGizmoTarget::Bone && _selectedBone.empty() )
            return CharacterGizmoTarget::None;
        return _gizmoTarget;
    }

    void CharacterEditorModel::reportEdit( const expected<void>& result )
    {
        _status = result.has_value() ? string{} : EditorWidgets::makeStatusText( result.error() );
    }

    void CharacterEditorModel::reportEdit( const expected<bool>& result )
    {
        _status = result.has_value() ? string{} : EditorWidgets::makeStatusText( result.error() );
    }
} // namespace sw::editor
