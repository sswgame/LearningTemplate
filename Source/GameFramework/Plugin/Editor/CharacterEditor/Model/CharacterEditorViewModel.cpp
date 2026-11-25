#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/SaveReloadGuard.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/AnimationSet.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorPackData.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearance.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorViewModelInternal
        {
            static constexpr const utf8* kDefaultPresetName = "MyLook";
            static constexpr const utf8* kDefaultExportID   = "MyCharacter";
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CharacterEditorViewModel::CharacterEditorViewModel()
        : _sockets{}
        , _pose{}
        , _appearance{}
        , _bodyShapes{}
        , _bodyProfiles{}
        , _visuals{}
        , _pReloadGuard{ make_unique<SaveReloadGuard<SocketSet>>() }
        , _fit{}
        , _pItems{}
        , _database{}
        , _presetStore{}
        , _stagedPresetStore{}
        , _stagedSelection{}
        , _thumbnailPresetName{}
        , _thumbnailSlot{}
        , _diskBaseline{}
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
        , _previewWipeEvent{}
        , _packDataState{ PackDataState::Unread }
        , _gizmoTarget{ CharacterGizmoTarget::None }
        , _bClipNamesLoaded{ SW_FALSE }
        , _bKindTableReady{ SW_FALSE }
        , _bDatabaseLoaded{ SW_FALSE }
        , _bHasFit{ SW_FALSE }
        , _bExternalChange{ SW_FALSE }
        , _bPreviewValid{ SW_FALSE }
        , _bStagedSelection{ SW_FALSE }
        , _bPresetsRead{ SW_FALSE }
        , _bSaveAfterRead{ SW_FALSE }
    {
        _paths._presetName = CharacterEditorViewModelInternal::kDefaultPresetName;
        _paths._exportID   = CharacterEditorViewModelInternal::kDefaultExportID;
    }

    CharacterEditorViewModel::~CharacterEditorViewModel() = default;

    void CharacterEditorViewModel::ensurePackData()
    {
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( _packDataState != PackDataState::Unread && packRoot == _packRoot )
            return;
        _packRoot = packRoot;
        _packDataMessage.clear();
        if ( const expected<void> path = GamePackDataUtil::tryMakeKindPath( packRoot, CharacterEditorPackData::kDataKind, _packDataPath ); path.has_value() == false )
        {
            _packDataState = PackDataState::Invalid;
            StringBuilder<constant::buffer::kMax512> message;
            message.appendFormat( "Character editor data has no path: %# (%#)", path.error().message(), path.error().detail() );
            _packDataMessage = string( message.view() );
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
            _packDataState = PackDataState::Invalid;
            StringBuilder<constant::buffer::kMax512> message;
            message.appendFormat( "%# could not be read (see log)", _packDataPath );
            _packDataMessage = string( message.view() );
            return;
        }
        _paths._socketPath       = data._socketPath;
        _paths._skeletonPath     = data._skeletonPath;
        _paths._refPosePath      = data._refPosePath;
        _paths._itemsPath        = data._itemsPath.path();
        _paths._appearanceFolder = data._appearanceFolder;
        _paths._animationSet     = data._animationSet;
        _paths._rigPath          = data._rigPath;
        _bClipNamesLoaded        = SW_FALSE; // 팩 데이터가 바뀌면 클립 목록을 다시 모은다
        _selectedClip            = hashed_string{};
        _previewPresetID         = hashed_string( string_view( data._previewPresetID ) );
        _previewWipeEvent        = data._wipeEvent.empty() ? hashed_string{} : hashed_string( string_view( data._wipeEvent ) );
        // 프리셋 내보내기 폴더는 외형 폴더에서 파생한다(기준은 팩 데이터의 외형 폴더 하나).
        const string exportFolder = FileUtil::joinPath( data._appearanceFolder, AppearanceDatabase::kPresetFolder );
        _paths._exportFolder      = string_view( exportFolder );
        _packDataState            = PackDataState::Ready;
    }

    void CharacterEditorViewModel::ensureKindTable()
    {
        if ( _bKindTableReady )
            return;
        _bKindTableReady = SW_TRUE;
        _pReloadGuard->reset();
        SocketKindTable kinds;
        if ( kinds.loadDefault() == false )
            SW_LOG_WARNING_ID( "CharacterEditorViewModel.DefaultSocketKindTable", "Default socket kind table could not be loaded - kinds are not validated" );
        else
            _sockets.setKindTable( kinds );

        MirrorNameRules rules;
        if ( rules.loadFromAsset( AssetID{ MirrorNameRules::kDefaultPath } ) == false )
            SW_LOG_WARNING_ID( "CharacterEditorViewModel.DefaultMirrorNameTable", "Default mirror name table could not be loaded - left/right pairs are not found" );
        else
            _sockets.setMirrorRules( rules );
    }

    bool CharacterEditorViewModel::loadSocketFile()
    {
        ensureKindTable(); // 문서는 첫 그리기 전에 열릴 수 있다(시나리오 · 콘텐츠 브라우저) — 종류 표 없이 읽으면 모든 kind 가 모르는 종류다
        const auto readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._socketPath.c_str() ) );
        if ( readPath.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Socket file '%#' has no file on disk to load", _paths._socketPath.c_str() );
            _status = string( status.view() );
            return false;
        }
        if ( _sockets.loadFromFile( *readPath ) == false )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Socket file '%#' could not be loaded (see log)", readPath->str() );
            _status = string( status.view() );
            return false;
        }
        _status.clear();
        _diskBaseline.capture( span<const AbsolutePath>( &*readPath, 1 ) );
        _selectedSocket  = hashed_string{};
        _bExternalChange = SW_FALSE;
        _pReloadGuard->reset();
        return true;
    }

    void CharacterEditorViewModel::pollExternalChange( float64 nowSeconds )
    {
        if ( _pReloadGuard->pollExternalChange( nowSeconds ) )
            _bExternalChange = SW_TRUE;
        if ( _bExternalChange && _paths._socketPath.empty() )
            _bExternalChange = SW_FALSE; // 열어 둔 소켓 파일이 없으면 반영할 모델이 없다
        // 고치지 않은 모델은 바뀐 파일을 바로 다시 읽는다. 읽기가 실패하면 표시가 남고 화면이 다시 읽기 단추를 낸다.
        const bool bCanAutoReload = _bExternalChange && _sockets.isDirty() == false;
        if ( bCanAutoReload && loadSocketFile() )
            _bExternalChange = SW_FALSE;
    }

    void CharacterEditorViewModel::openBodyDataFile( BodyDataEditModel& model, string_view assetPath )
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
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Body data file '%#' has no disk path", assetPath );
            _status = string( status.view() );
            return;
        }
        if ( model.isDirty() || ( model.isOpen() && model.getDiskPath() == *diskPath ) )
            return; // 고친 파일은 다시 읽지 않는다 — 같은 파일은 이미 열렸다
        if ( model.openFile( *diskPath ) == false )
            _status = model.getError();
        else
            _diskBaseline.capture( span<const AbsolutePath>( &*diskPath, 1 ) );
    }

    void CharacterEditorViewModel::openBodyData( const hashed_string& bodyShapesAsset )
    {
        const string folder( _paths._appearanceFolder.c_str() );
        openBodyDataFile( _bodyShapes, bodyShapesAsset.view() );
        openBodyDataFile( _bodyProfiles, folder.empty() ? string_view{} : string_view( FileUtil::joinPath( folder, "bodyprofiles.xml" ) ) );
        openBodyDataFile( _visuals, folder.empty() ? string_view{} : string_view( FileUtil::joinPath( folder, "itemvisuals.xml" ) ) );
    }

    bool CharacterEditorViewModel::saveBodyData()
    {
        bool       bSaved          = true;
        const bool bProfileChanged = _bodyProfiles.isDirty() || _visuals.isDirty();
        for ( BodyDataEditModel* pModel : { &_bodyShapes, &_bodyProfiles, &_visuals } )
        {
            if ( pModel->isDirty() == false )
                continue;
            if ( _diskBaseline.validateOverwrite( span<const AbsolutePath>( &pModel->getDiskPath(), 1 ) ) == false )
            {
                _status = "Body data file changed on disk - save again to overwrite it";
                bSaved  = false;
                continue;
            }
            if ( pModel->save() == false )
            {
                _status = pModel->getError();
                bSaved  = false;
                continue;
            }
            _diskBaseline.capture( span<const AbsolutePath>( &pModel->getDiskPath(), 1 ) );
            SW_LOG_INFO_ID( "CharacterEditorViewModel.BodyDataSaved", "Body data file saved to %#", pModel->getDiskPath().c_str() );
        }
        // 몸 프로필 · 구간은 이 창의 외형 데이터가 읽은 것이다 — 다시 읽어야 미리보기 몸이 새 값을 입는다(채널 매핑은 공유 캐시의 파일 감시가 다시 읽는다).
        if ( bSaved && bProfileChanged )
            loadAppearanceData();
        return bSaved;
    }

    bool CharacterEditorViewModel::saveFiles( float64 nowSeconds )
    {
        bool bSaved = true;
        if ( _sockets.isDirty() )
        {
            const auto writePath = editor::getSubsystem<AssetPaths>().toDiskPath( string_view( _paths._socketPath.c_str() ) );
            if ( writePath.has_value() == false || _diskBaseline.validateOverwrite( span<const AbsolutePath>( &*writePath, 1 ) ) == false || _sockets.saveDocument( *writePath ) == false )
            {
                _status = "Socket file could not be saved";
                bSaved  = false;
            }
            else
            {
                _diskBaseline.capture( span<const AbsolutePath>( &*writePath, 1 ) );
                _pReloadGuard->onSaved( nowSeconds ); // 파일 감시가 곧 같은 파일을 다시 읽는다 — 그건 외부 변경이 아니다
                SW_LOG_INFO_ID( "CharacterEditorViewModel.SocketFileSaved", "Socket file saved to %#", writePath->c_str() );
            }
        }
        if ( _pose.isDirty() )
        {
            const auto writePath = editor::getSubsystem<AssetPaths>().toDiskPath( string_view( _paths._refPosePath.c_str() ) );
            if ( writePath.has_value() == false || _diskBaseline.validateOverwrite( span<const AbsolutePath>( &*writePath, 1 ) ) == false || _pose.saveDocument( *writePath ) == false )
            {
                _status = "Reference pose file could not be saved";
                bSaved  = false;
            }
            else
            {
                _diskBaseline.capture( span<const AbsolutePath>( &*writePath, 1 ) );
                SW_LOG_INFO_ID( "CharacterEditorViewModel.ReferencePoseFileSaved", "Reference pose file saved to %#", writePath->c_str() );
            }
        }
        if ( isBodyDataDirty() && saveBodyData() == false )
            bSaved = false; // 까닭은 saveBodyData 가 상태 줄에 남겼다
        if ( bSaved )
            _status.clear();
        return bSaved;
    }

    void CharacterEditorViewModel::collectFilesToSave( vector<string>& outListAssetID ) const
    {
        if ( _sockets.isDirty() )
            outListAssetID.push_back( string( _paths._socketPath.c_str() ) );
        if ( _pose.isDirty() )
            outListAssetID.push_back( string( _paths._refPosePath.c_str() ) );
        for ( const BodyDataEditModel* pModel : { &_bodyShapes, &_bodyProfiles, &_visuals } )
        {
            if ( pModel->isDirty() && pModel->getDiskPath().empty() == false )
                outListAssetID.push_back( pModel->getDiskPath().str() );
        }
    }

    void CharacterEditorViewModel::revert()
    {
        if ( _sockets.isDirty() && _sockets.revertToSaved() == false )
            SW_LOG_ERROR_ID( "CharacterEditorViewModel.CharacterEditorSocketEdits", "Character Editor: the socket edits could not be discarded - the saved state did not read back" );
        if ( _pose.isDirty() && _pose.revertToSaved() == false )
            SW_LOG_ERROR_ID( "CharacterEditorViewModel.CharacterEditorReferencePose", "Character Editor: the reference pose edits could not be discarded - the saved state did not read back" );
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorViewModel::clearSockets()
    {
        _sockets.clear();
        _selectedSocket = hashed_string{};
        _status.clear();
    }

    void CharacterEditorViewModel::loadReferencePose()
    {
        const auto readPath = editor::getSubsystem<AssetPaths>().findLooseFile( string_view( _paths._refPosePath.c_str() ) );
        if ( readPath.has_value() == false )
            _status = "Reference pose file was not found - edit bones and Save to create it";
        else if ( _pose.loadFromFile( *readPath ) == false )
            _status = "Reference pose file could not be loaded (see log)";
        else
        {
            _diskBaseline.capture( span<const AbsolutePath>( &*readPath, 1 ) );
            _status.clear();
        }
    }

    void CharacterEditorViewModel::loadSkeleton()
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

    void CharacterEditorViewModel::addSocket( const hashed_string& name )
    {
        const expected<void> added = _sockets.addSocket( name, hashed_string{}, hashed_string{} );
        reportEdit( added );
        if ( added.has_value() )
            _selectedSocket = name;
    }

    void CharacterEditorViewModel::renameSocket( const hashed_string& name, const hashed_string& newName )
    {
        const expected<void> renamed = _sockets.renameSocket( name, newName );
        reportEdit( renamed );
        if ( renamed.has_value() )
            _selectedSocket = newName;
    }

    void CharacterEditorViewModel::mirrorSocket( const hashed_string& name )
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

    void CharacterEditorViewModel::removeSocket( const hashed_string& name )
    {
        reportEdit( _sockets.removeSocket( name ) );
        _selectedSocket = hashed_string{};
    }

    void CharacterEditorViewModel::mirrorSelectedBone()
    {
        const expected<hashed_string> mirrored = _pose.mirrorBone( _selectedBone, _sockets.getMirrorRules() );
        if ( mirrored.has_value() == false )
        {
            _status = EditorWidgets::makeStatusText( mirrored.error() );
            return;
        }
        _selectedBone = *mirrored;
    }

    void CharacterEditorViewModel::loadAppearanceData()
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
            StringBuilder<constant::buffer::kMax1024> status;
            status.appendFormat( "Appearance data has errors - %#", _database.getReport().joined() );
            _status = string( status.view() );
            return;
        }
        _status.clear();
        _bDatabaseLoaded = SW_TRUE;
    }

    CharacterEditorLocalStore* CharacterEditorViewModel::findLocalStore()
    {
        CharacterEditorLocalStore* const pStore = editor::findSubsystem<CharacterEditorLocalStore>(); // 열지 못했으면 없다(오류 로그가 남았다)
        if ( pStore == nullptr )
            _status = "The character editor local store is not open (see log) - presets are unavailable";
        return pStore;
    }

    void CharacterEditorViewModel::loadPresets()
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
                                                          SW_DELEGATE_METHOD( LocalStoreChannel::CompletionFunction, &CharacterEditorViewModel::onPresetsRead, this ) );
        _status        = "Loading presets...";
    }

    void CharacterEditorViewModel::onPresetsRead( const LocalStoreCompletion& completion )
    {
        const bool bSave = _bSaveAfterRead == SW_TRUE;
        const bool bNone = completion._result == LocalStoreResult::NotFound;
        const bool bRead = completion._result == LocalStoreResult::Ok &&
                           _presetStore.loadFromXMLText( string_view( reinterpret_cast<const utf8*>( completion._bytes.data() ), completion._bytes.size() ) );
        _bSaveAfterRead = SW_FALSE;
        _bPresetsRead   = bNone || bRead ? SW_TRUE : SW_FALSE; // 읽지 못한 슬롯은 덮어쓰지 않는다
        if ( bNone )
            _presetStore.clear();
        if ( _bPresetsRead == SW_FALSE )
            _status = "Preset slot could not be read (see log)";
        else if ( bSave )
            writeSelectionAsPreset();
        else if ( bNone )
            _status = "No presets saved yet";
        else
            _status.clear();
    }

    void CharacterEditorViewModel::savePreset()
    {
        const string_view name{ _paths._presetName.c_str() };
        if ( name.empty() || isPresetRequestPending() )
        {
            _status = name.empty() ? "The preset needs a name" : "A preset request is still running";
            return;
        }
        if ( _bPresetsRead == SW_TRUE )
        {
            writeSelectionAsPreset();
            return;
        }
        // 슬롯은 목록 전체라, 읽지 않은 채 쓰면 저장돼 있던 프리셋이 모두 사라진다 — 먼저 읽고 잇는다.
        loadPresets();
        _bSaveAfterRead = _presetRequest.isActive() ? SW_TRUE : SW_FALSE;
    }

    void CharacterEditorViewModel::writeSelectionAsPreset()
    {
        UserAppearancePresetStore staged    = _presetStore;
        const AppearanceSelection selection = _appearance.getSelection();
        if ( staged.savePreset( string_view{ _paths._presetName.c_str() }, selection, _database ) == false )
        {
            _status = "The preset needs a name";
            return;
        }
        submitPresetWrite( std::move( staged ), &selection );
    }

    void CharacterEditorViewModel::submitPresetWrite( UserAppearancePresetStore staged, const AppearanceSelection* pSavedSelection )
    {
        CharacterEditorLocalStore* const pStore = findLocalStore();
        if ( pStore == nullptr )
            return;
        vector<uint8> bytes;
        if ( staged.writeBytes( bytes ) == false )
        {
            _status = "Presets could not be written as text (see log) - the saved slot is kept";
            return;
        }
        _stagedPresetStore = std::move( staged );
        _bStagedSelection  = pSavedSelection != nullptr;
        if ( pSavedSelection != nullptr )
            _stagedSelection = *pSavedSelection;
        _presetRequest = pStore->getChannel().submitWrite( CharacterEditorLocalStore::kPresetSlot, std::move( bytes ), LocalStoreWriteOptions{},
                                                           SW_DELEGATE_METHOD( LocalStoreChannel::CompletionFunction, &CharacterEditorViewModel::onPresetsWritten, this ) );
        _status        = "Saving presets...";
    }

    void CharacterEditorViewModel::onPresetsWritten( const LocalStoreCompletion& completion )
    {
        if ( completion._result != LocalStoreResult::Ok )
        {
            // 목록과 "저장된 상태" 를 그대로 둔다 — 저장된 줄 알고 넘어가지 않게.
            _status = "Preset slot could not be written (see log)";
            SW_LOG_ERROR_ID( "CharacterEditorViewModel.CharacterEditorPresetSlot", "Character Editor: preset slot %# could not be written (result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
            return;
        }
        _presetStore = std::move( _stagedPresetStore );
        if ( _bStagedSelection )
            _appearance.markSaved( _stagedSelection );
        _status.clear();
        SW_LOG_INFO_ID( "CharacterEditor.PresetsSaved", "Character Editor: presets saved to slot %# (%# presets)", completion._slot.c_str(), static_cast<uint32>( _presetStore.getPresets().size() ) );
    }

    void CharacterEditorViewModel::applyPreset( string_view name )
    {
        if ( const expected<void> loaded = _appearance.loadFromPreset( _presetStore, name, _database ); loaded.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Preset '%#' could not be opened - %#", name, loaded.error().message() );
            _status = string( status.view() );
            return;
        }
        _paths._presetName = name;
    }

    bool CharacterEditorViewModel::canAttachThumbnail()
    {
        if ( _presetStore.findPresetByName( string_view( _paths._presetName.c_str() ) ) == nullptr )
        {
            _status = "Save the preset first - a thumbnail belongs to a saved preset";
            return false;
        }
        if ( isPresetRequestPending() || _bPresetsRead == SW_FALSE )
        {
            _status = _bPresetsRead == SW_TRUE ? "A preset request is still running" : "The saved presets could not be read - load them first";
            return false;
        }
        return findLocalStore() != nullptr;
    }

    void CharacterEditorViewModel::attachThumbnail( string_view presetName, vector<uint8> pngBytes )
    {
        CharacterEditorLocalStore* const pStore = findLocalStore();
        if ( pStore == nullptr || isPresetRequestPending() )
            return;
        _thumbnailPresetName = string{ presetName };
        _thumbnailSlot       = CharacterEditorLocalStore::makeThumbnailSlot( _presetStore, presetName );
        _thumbnailRequest    = pStore->getChannel().submitWrite( _thumbnailSlot, std::move( pngBytes ), LocalStoreWriteOptions{},
                                                                 SW_DELEGATE_METHOD( LocalStoreChannel::CompletionFunction, &CharacterEditorViewModel::onThumbnailWritten, this ) );
        _status              = "Saving the thumbnail...";
    }

    void CharacterEditorViewModel::onThumbnailWritten( const LocalStoreCompletion& completion )
    {
        UserAppearancePresetStore staged      = _presetStore;
        const bool                bAttachable = completion._result == LocalStoreResult::Ok && staged.setThumbnailSlot( _thumbnailPresetName, _thumbnailSlot );
        if ( bAttachable == false )
        {
            _status = "The thumbnail could not be stored (the slot write failed or the preset was removed)";
            SW_LOG_ERROR_ID( "CharacterEditorViewModel.CharacterEditorThumbnailSlot", "Character Editor: thumbnail slot %# of preset '%#' could not be stored (result %#)", _thumbnailSlot.c_str(), _thumbnailPresetName.c_str(),
                             static_cast<int32>( completion._result ) );
            return;
        }
        SW_LOG_INFO_ID( "CharacterEditor.ThumbnailStored", "Character Editor: thumbnail of preset '%#' stored in slot %#", _thumbnailPresetName.c_str(), _thumbnailSlot.c_str() );
        submitPresetWrite( std::move( staged ), nullptr );
    }

    bool CharacterEditorViewModel::exportDataPreset()
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
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Data preset could not be made - %#", made.error().message() );
            if ( made.error().detail().empty() == false )
                status.appendFormat( " (%#)", made.error().detail() );
            _status = string( status.view() );
            return false;
        }
        StringBuilder<constant::buffer::kMax256> presetFile;
        presetFile.appendFormat( "%#%#", presetID.c_str(), AppearanceDatabase::kPresetExtension );
        const string assetID   = FileUtil::joinPath( string_view( _paths._exportFolder.c_str() ), presetFile.view() );
        const auto   writePath = editor::getSubsystem<AssetPaths>().toDiskPath( assetID );
        const bool   bWritten  = writePath.has_value() && doc.saveCompleteToFile( *writePath );
        if ( bWritten == false )
        {
            StringBuilder<constant::buffer::kMax512> status;
            status.appendFormat( "Data preset could not be written to %#", assetID );
            _status = string( status.view() );
            return false;
        }
        // 이 창의 데이터와 실행 중인 게임의 데이터에 올린다 — 씬의 컴포넌트는 게임 서비스(`AppearanceDatabase`)에서 프리셋을 푼다.
        // 쓰기는 디스크 경로, 읽기는 리소스 id 다(리소스 입구는 OS 경로를 받지 않는다).
        if ( _database.loadPresetFile( AssetID{ assetID } ) == false )
        {
            StringBuilder<constant::buffer::kMax1024> status;
            status.appendFormat( "Exported preset has errors - %#", _database.getReport().joined() );
            _status = string( status.view() );
            return false;
        }
        AppearanceDatabase* pGameData = game::findSubsystem<AppearanceDatabase>();
        if ( pGameData == nullptr )
            SW_LOG_WARNING_ID( "CharacterEditorViewModel.ExportedAppearancePresetGame", "Exported appearance preset '%#' - this game has no AppearanceDatabase service, so scenes use it after the game loads it", presetID.c_str() );
        else if ( pGameData->loadPresetFile( AssetID{ assetID } ) == false )
        {
            StringBuilder<constant::buffer::kMax1024> status;
            status.appendFormat( "The game's appearance data rejected the preset - %#", pGameData->getReport().joined() );
            _status = string( status.view() );
            return false;
        }
        _status.clear();
        SW_LOG_INFO_ID( "CharacterEditor.PresetExported", "Exported appearance preset '%#' to '%#'", presetID.c_str(), assetID.c_str() );
        return true;
    }

    AppearanceSelection CharacterEditorViewModel::makePreviewSelection() const
    {
        AppearanceSelection selection = _appearance.getSelection();
        if ( selection._basePresetID.empty() )
            selection._basePresetID = _previewPresetID;
        return selection;
    }

    const CustomizationSchemaDef* CharacterEditorViewModel::findCustomizationSchema( const AppearanceSelection& selection ) const
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

    bool CharacterEditorViewModel::hasBasePresetInData() const
    {
        return _database.getPresets().findPreset( _appearance.getSelection()._basePresetID ) != nullptr;
    }

    void CharacterEditorViewModel::refreshResolvedPreview()
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

    uint32 CharacterEditorViewModel::countEquippedSlots() const
    {
        uint32 equipped = 0;
        for ( const AppearanceSlotRequest& request : _appearance.getSelection()._listSlot )
        {
            if ( request._itemID.empty() == false )
                ++equipped;
        }
        return equipped;
    }

    void CharacterEditorViewModel::setFitResult( const FitResult& result )
    {
        _fit     = FitCheckSummary::fromResult( result );
        _bHasFit = SW_TRUE;
    }

    const vector<hashed_string>& CharacterEditorViewModel::getClipNames()
    {
        if ( _bClipNamesLoaded == false )
            loadClipNames();
        return _listClipName;
    }

    void CharacterEditorViewModel::loadClipNames()
    {
        _listClipName.clear();
        _bClipNamesLoaded = SW_TRUE;
        if ( _paths._animationSet.empty() )
            return;
        // 런타임과 같은 공유 캐시의 한 벌을 읽는다(사본을 읽지 않는다).
        const shared_ptr<const AnimationSet> pAnimationSet = SharedAssetCache<AnimationSet>::acquire( string_view( _paths._animationSet.c_str() ) ).snapshot();
        if ( pAnimationSet == nullptr )
            return;                                                          // 셋이 까닭을 남겼다
        for ( const AnimationSetEntry& entry : pAnimationSet->getEntries() ) // 표는 이름순이다
        {
            _listClipName.push_back( hashed_string( string_view{ entry._name } ) );
        }
    }

    void CharacterEditorViewModel::selectSocket( const hashed_string& name )
    {
        _selectedSocket = name;
        if ( name.empty() == false )
            _gizmoTarget = CharacterGizmoTarget::Socket;
    }

    void CharacterEditorViewModel::selectBone( const hashed_string& name )
    {
        _selectedBone = name;
        if ( name.empty() == false )
            _gizmoTarget = CharacterGizmoTarget::Bone;
    }

    CharacterGizmoTarget CharacterEditorViewModel::getGizmoTarget() const
    {
        // 주인을 지웠으면(소켓 삭제 · 본 해제) 다른 쪽이 남아 있어도 기즈모를 두지 않는다 — 고르지 않은 것을 끌게 하지 않는다.
        if ( _gizmoTarget == CharacterGizmoTarget::Socket && _selectedSocket.empty() )
            return CharacterGizmoTarget::None;
        if ( _gizmoTarget == CharacterGizmoTarget::Bone && _selectedBone.empty() )
            return CharacterGizmoTarget::None;
        return _gizmoTarget;
    }

    void CharacterEditorViewModel::reportEdit( const expected<void>& result )
    {
        _status = result.has_value() ? string{} : EditorWidgets::makeStatusText( result.error() );
    }

    void CharacterEditorViewModel::reportEdit( const expected<bool>& result )
    {
        _status = result.has_value() ? string{} : EditorWidgets::makeStatusText( result.error() );
    }
} // namespace sw::editor
