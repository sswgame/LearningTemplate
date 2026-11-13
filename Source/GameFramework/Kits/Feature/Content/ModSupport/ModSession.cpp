#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModAllowList.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModResolver.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

namespace sw
{
    /** @brief `-gv_modList=a,b`: 이 세션에 켤 모드 id(쉼표로 가른다). 비면 모드 없이 돈다. 배포 구성에도 있다 — 모드를 고르는 것은 플레이어다. */
    SW_GLOBAL_VARIABLE( sw::string, gv_modList, "", "이 세션에 켤 모드 id 목록 (쉼표로 가른다, 비우면 모드 없음)" );

    namespace
    {
        struct ModSessionInternal
        {
            static constexpr const utf8* kInstallFolderName       = "Mods";      ///< 패키지(실행 파일 폴더) 아래 모드 설치 폴더
            static constexpr const utf8* kSourceTreeInstallFolder = "Test/Mods"; ///< 저장소에서의 설치 폴더 — 시연 모드는 시나리오 · 테스트 자료다
            static constexpr const utf8* kExportFolderName        = "ModPacks";  ///< Saved 아래 배포용 묶은 모드 폴더(`mod.pack`)
            static constexpr size_t      kReplyLineLimit          = 200;         ///< `mod.read` 답의 글자 수 상한

            /** @brief 쉼표 목록을 id 로 가릅니다(앞뒤 공백을 떼고 빈 칸은 버린다). 형식 검사는 `loadManifests` 가 한다. */
            static vector<string> splitList( string_view text )
            {
                vector<string> listID;
                for ( const string_view part : StringUtil::split( text, ",", StringSplitOptions{ true, true } ) )
                {
                    listID.emplace_back( part );
                }
                return listID;
            }

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const ModSession* const pSession = game::findSubsystem<ModSession>();
                if ( pSession == nullptr )
                {
                    outReply = "mods: no mod session";
                    return true;
                }
                const ModSetDigest&                      digest = pSession->getDigest();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mods: %# active [%#] config %#", digest._listEntry.size(), digest.toText(), digest._configHash );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runRead( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                string text;
                if ( AssetIO::readTextAsset( AssetID{ listArgument[0] }, text ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "mod.read %#: not found", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                string line{ StringUtil::trim( text ) };
                for ( utf8& ch : line )
                {
                    if ( ch == '\r' || ch == '\n' || ch == '\t' )
                        ch = ' ';
                }
                if ( line.size() > kReplyLineLimit )
                    line.resize( kReplyLineLimit );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mod.read %#: %#", listArgument[0], line );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `mod.pack <id>` — 소스 모드를 배포 형태로 묶어 내보내기 폴더에 둔다(`ModSession::exportPackedMod`). */
            static bool runPack( const vector<string>& listArgument, string& outReply )
            {
                const GameConfig* const pGameConfig = game::findSubsystem<const GameConfig>();
                if ( listArgument.size() != 1 || pGameConfig == nullptr )
                    return false;
                string folder;
                if ( ModSession::exportPackedMod( listArgument[0], pGameConfig->_packRoot.get(), folder ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "mod.pack %#: failed (see the error above)", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "mod.pack %#: %#", listArgument[0], folder );
                outReply = string( replyBuilder.view() );
                return true;
            }

            [[nodiscard]] static bool readActiveCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ModSession* const pSession = game::findSubsystem<ModSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getDigest()._listEntry.size() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ModSession::ModSession( AssetManager& assets )
        : _assets{ assets }
        , _listMountedPackPath{}
        , _listBuiltPackPath{}
        , _listOverridePath{}
        , _listLayerPath{}
        , _listPreviousLayerRoot{}
        , _digest{}
        , _bApplied{ 0 }
        , _bLayersSet{ 0 }
        , _reserved{ 0 }
    {
    }

    ModSession::~ModSession()
    {
        release();
    }

    string ModSession::getInstallFolder()
    {
        // 패키지는 실행 파일 옆 `Mods/`(사용자가 넣는 자리), 저장소에서는 시연 모드가 시나리오 · 테스트 자료라 `Test/Mods/` 다.
        // 패키지 판정은 ProcessRoots 의 규칙(프로젝트 루트가 바이너리 폴더 — `Content` 가 없다)을 그대로 쓴다.
        const ProcessRoots& roots    = ProcessRoots::get();
        const bool          bPackage = roots._project.str() == roots._binary.str();
        return bPackage ? FileUtil::joinPath( roots._binary.str(), ModSessionInternal::kInstallFolderName )
                        : FileUtil::joinPath( roots._project.str(), ModSessionInternal::kSourceTreeInstallFolder );
    }

    string ModSession::getExportFolder()
    {
        return FileUtil::joinPath( ProcessRoots::get()._saved.str(), ModSessionInternal::kExportFolderName );
    }

    bool ModSession::exportPackedMod( string_view id, string_view packRoot, string& outFolder )
    {
        outFolder.clear();
        // id 는 바깥 입력이다 — 형식이 맞아야 경로로 쓴다(`..` · 구분자 · 대문자가 없다).
        if ( ModManifest::isValidID( id ) == false )
        {
            SW_LOG_ERROR_ID( "ModSession.ExportBadID", "mod.pack: '%#' is not a mod id (lowercase letters, digits and _)", string( id ) );
            return false;
        }
        string settingsPath;
        if ( const expected<void> path = GamePackDataUtil::tryMakeKindPath( packRoot, ModSupportSettings::kDataKind, settingsPath ); path.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModSession.ExportNoSettings", "mod.pack: the game has no mod support settings path: %# (%#)", path.error().message(), path.error().detail() );
            return false;
        }
        ModSupportSettings settings;
        if ( ModSupportSettings::loadFromAsset( AssetID{ settingsPath }, settings ) == false )
            return false;
        ModSessionRequest request;
        request._installFolder = getInstallFolder();
        request._listEnabledID = { string( id ) };
        vector<ModManifest> listManifest;
        if ( loadManifests( request, settings, listManifest ) == false ) // id 와 폴더 이름 · 매니페스트 크기 · 형식을 마운트와 같은 길로 본다
            return false;
        const string modFolder    = FileUtil::joinPath( request._installFolder, id );
        const string sourceFolder = FileUtil::joinPath( modFolder, ModPackBuilder::kSourceFolderName );
        if ( FileUtil::isDirectory( sourceFolder ) == false )
        {
            SW_LOG_ERROR_ID( "ModSession.ExportNoSource", "mod.pack: '%#' has no %#/ source folder to pack", string( id ), ModPackBuilder::kSourceFolderName );
            return false;
        }
        const string exportFolder = FileUtil::joinPath( getExportFolder(), id );
        const string packPath     = FileUtil::joinPath( exportFolder, ModPackBuilder::kPackFileName );
        const string manifestPath = FileUtil::joinPath( exportFolder, ModManifest::kFileName );
        const bool   bExported    = FileUtil::ensureDirectoryExists( exportFolder ) && ModPackBuilder::buildFromFolder( sourceFolder, packPath, settings ) &&
                               FileUtil::copyFile( FileUtil::joinPath( modFolder, ModManifest::kFileName ), manifestPath );
        if ( bExported == false )
        {
            (void)FileUtil::removeFile( packPath ); // 반쯤 쓴 배포 형태를 남기지 않는다(패키지가 베끼지 않게)
            (void)FileUtil::removeFile( manifestPath );
            SW_LOG_ERROR_ID( "ModSession.ExportFailed", "mod.pack: '%#' could not be packed into %#", string( id ), exportFolder );
            return false;
        }
        outFolder = exportFolder;
        SW_LOG_INFO_ID( "ModSession.Exported", "Mod '%#' packed for shipping: %#", string( id ), exportFolder );
        return true;
    }

    bool ModSession::applyForGame( string_view packRoot )
    {
        ModSessionRequest request;
        request._listEnabledID = ModSessionInternal::splitList( gv_modList );
        if ( request._listEnabledID.empty() )
            return true;
        string settingsPath;
        if ( const expected<void> path = GamePackDataUtil::tryMakeKindPath( packRoot, ModSupportSettings::kDataKind, settingsPath ); path.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ModSession.NoSettingsPath", "Mods were requested (%#) but the game has no mod support settings path: %# (%#)", gv_modList, path.error().message(), path.error().detail() );
            return false;
        }
        ModSupportSettings settings;
        if ( ModSupportSettings::loadFromAsset( AssetID{ settingsPath }, settings ) == false )
            return false;
        request._installFolder = getInstallFolder();
        request._buildFolder   = FileUtil::joinPath( ProcessRoots::get()._saved.str(), ModSessionInternal::kInstallFolderName );
        return apply( request, settings );
    }

    bool ModSession::apply( const ModSessionRequest& request, const ModSupportSettings& settings )
    {
        if ( _bApplied != 0 )
        {
            SW_LOG_ERROR_ID( "ModSession.AppliedTwice", "The mod set is fixed when the session starts - apply is called once per session" );
            return false;
        }
        _bApplied = 1;
        if ( request._listEnabledID.empty() )
            return true;

        vector<ModManifest> listManifest;
        vector<size_t>      listOrder;
        if ( loadManifests( request, settings, listManifest ) == false || ModResolver::resolveOrder( listManifest, settings, listOrder ) == false )
            return false;

        // 팩을 모두 준비 · 검사한 뒤에야 올린다 — 반쯤 올린 구성을 만들지 않는다.
        vector<string>         listPackPath;
        vector<ModDigestEntry> listEntry;
        for ( const size_t manifestIndex : listOrder )
        {
            const ModManifest& manifest = listManifest[manifestIndex];
            string             packPath;
            ModDigestEntry     entry;
            if ( preparePack( request, manifest, settings, packPath ) == false || checkPack( packPath, manifest, settings, entry._contentHash ) == false )
            {
                release();
                return false;
            }
            entry._id      = manifest._id;
            entry._version = manifest._version;
            listEntry.push_back( std::move( entry ) );
            listPackPath.push_back( std::move( packPath ) );
        }
        const ModSetDigest digest = ModSetDigest::make( std::move( listEntry ) );
        if ( request._pAllowList != nullptr && request._pAllowList->checkDigest( digest ) == false )
        {
            release();
            return false;
        }

        PackManager& packs        = _assets.getPackManager();
        const int32  basePriority = packs.getTopPriority();
        for ( size_t orderIndex = 0; orderIndex < listPackPath.size(); ++orderIndex )
        {
            const int32 priority = basePriority + kPriorityStep * static_cast<int32>( orderIndex + 1 );
            if ( packs.mountPatchPack( listPackPath[orderIndex], priority, {} ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.MountFailed", "Mod pack '%#' could not be mounted", listPackPath[orderIndex] );
                release();
                return false;
            }
            _listMountedPackPath.push_back( listPackPath[orderIndex] );
        }
        vector<string> listLayerRoot = game::getSubsystem<ResourceComposition>().getLayers();
        // 경로마다 모은 목록을 훑지 않는다(켠 모드들의 덮어쓰기 · 층 수의 제곱). 목록은 처음 나온 순서다.
        unordered_set<string> uniqueOverridePath;
        unordered_set<string> uniqueLayerPath;
        for ( const size_t manifestIndex : listOrder )
        {
            const ModManifest& manifest = listManifest[manifestIndex];
            for ( const string& path : manifest._listOverride )
            {
                if ( uniqueOverridePath.insert( path ).second )
                    _listOverridePath.push_back( path );
            }
            for ( const string& path : manifest._listLayer )
            {
                if ( uniqueLayerPath.insert( path ).second )
                    _listLayerPath.push_back( path );
            }
            if ( manifest._listLayer.empty() == false )
                listLayerRoot.push_back( ModManifest::makeLayerRoot( manifest._id ) );
        }
        // 모드 층은 게임 팩의 층 위에 모드 순서대로 얹는다 — 뒤(우선순위가 높은) 모드의 차이가 나중에 얹혀 이긴다.
        if ( _listLayerPath.empty() == false )
        {
            _listPreviousLayerRoot = game::getSubsystem<ResourceComposition>().getLayers();
            if ( game::getSubsystem<ResourceComposition>().setLayers( listLayerRoot ) == false )
            {
                release();
                return false;
            }
            _bLayersSet = 1;
        }

        // 덮어쓰거나 층을 얹은 리소스를 이미 쥔 캐시는 다시 읽힌다. 제자리로 다시 읽지 못하는 캐시가 쥐었으면 옛 내용을 계속 내주므로 거절한다(모드를 더 일찍 적용해야 한다).
        vector<string> listStale;
        (void)_assets.reloadChangedPaths( makeChangedPaths(), listStale );
        if ( listStale.empty() == false )
        {
            for ( const string& path : listStale )
            {
                SW_LOG_ERROR_ID( "ModSession.OverrideAlreadyLoaded", "Mod override '%#' was already loaded by a cache that cannot reload it in place - apply mods before that resource loads",
                                 path );
            }
            release();
            return false;
        }

        _digest = digest;
        SW_LOG_INFO_ID( "ModSession.Applied", "Mods applied: %# [%#] config %#", _digest._listEntry.size(), _digest.toText(), _digest._configHash );
        return true;
    }

    vector<string> ModSession::makeChangedPaths() const
    {
        vector<string>        listPath = _listOverridePath;
        unordered_set<string> uniquePath; // 층마다 덮어쓰기 목록을 훑지 않는다(덮어쓰기 수 × 층 수)
        for ( const string& path : _listOverridePath )
        {
            uniquePath.insert( path );
        }
        for ( const string& path : _listLayerPath )
        {
            if ( uniquePath.insert( path ).second )
                listPath.push_back( path );
        }
        return listPath;
    }

    void ModSession::release()
    {
        // 층을 먼저 되돌린다 — 팩을 내린 뒤의 다시 읽기가 사라진 층 뿌리를 보지 않게.
        if ( _bLayersSet != 0 && game::getSubsystem<ResourceComposition>().setLayers( _listPreviousLayerRoot ) == false )
            SW_LOG_ERROR_ID( "ModSession.LayersNotRestored", "The resource layers before the mods could not be restored - the next session may still see mod layers" );
        _bLayersSet = 0;
        _listPreviousLayerRoot.clear();
        PackManager& packs = _assets.getPackManager();
        for ( auto it = _listMountedPackPath.rbegin(); it != _listMountedPackPath.rend(); ++it )
        {
            if ( packs.unmountPack( *it ) == false )
                SW_LOG_ERROR_ID( "ModSession.UnmountFailed", "Mod pack '%#' was not mounted when the session released it", *it );
        }
        const bool bHadMounts = _listMountedPackPath.empty() == false;
        _listMountedPackPath.clear();
        if ( bHadMounts )
        {
            // 덮어썼던 리소스를 쥔 캐시는 기본 내용으로 다시 읽힌다 — 다음 세션이 모드 내용을 보지 않게.
            vector<string> listStale;
            (void)_assets.reloadChangedPaths( makeChangedPaths(), listStale );
            for ( const string& path : listStale )
            {
                SW_LOG_ERROR_ID( "ModSession.OverrideStillHeld", "Mod override '%#' is still held by a cache that cannot reload it - the next session would see the mod's content", path );
            }
        }
        _listOverridePath.clear();
        _listLayerPath.clear();
        for ( const string& builtPath : _listBuiltPackPath )
        {
            if ( FileUtil::removeFile( builtPath ) == false )
                SW_LOG_WARNING_ID( "ModSession.BuiltPackKept", "The mod pack built for this session could not be removed: %#", builtPath );
        }
        _listBuiltPackPath.clear();
        _digest = ModSetDigest{};
    }

    bool ModSession::loadManifests( const ModSessionRequest& request, const ModSupportSettings& settings, vector<ModManifest>& outListManifest )
    {
        if ( request._listEnabledID.size() > settings._maxModCount )
        {
            SW_LOG_ERROR_ID( "ModSession.TooManyMods", "%# mods are requested, but the game allows at most %#", request._listEnabledID.size(), settings._maxModCount );
            return false;
        }
        for ( const string& id : request._listEnabledID )
        {
            // id 가 형식을 지켜야 경로를 만든다 — `..` · 구분자 · 드라이브가 든 id 는 설치 폴더 밖을 가리킬 수 있다.
            if ( ModManifest::isValidID( id ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.BadID", "Requested mod id '%#' must be 1 to %# characters of a-z, 0-9 and _", id, ModManifest::kMaxIDLength );
                return false;
            }
            const string manifestPath = FileUtil::joinPath( FileUtil::joinPath( request._installFolder, id ), ModManifest::kFileName );
            uint64       manifestSize = 0;
            if ( FileUtil::tryGetFileSize( manifestPath, manifestSize ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.MissingMod", "Requested mod '%#' is not installed (no %#)", id, manifestPath );
                return false;
            }
            if ( manifestSize > settings._maxManifestBytes )
            {
                SW_LOG_ERROR_ID( "ModSession.ManifestTooLarge", "Mod manifest '%#' is %# bytes, more than %#", manifestPath, manifestSize, settings._maxManifestBytes );
                return false;
            }
            string text;
            if ( FileUtil::readTextFile( manifestPath, text ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.CannotReadManifest", "Mod manifest '%#' could not be read", manifestPath );
                return false;
            }
            ModManifest manifest;
            if ( ModManifest::parseXMLText( text, manifestPath, manifest ) == false )
                return false;
            if ( manifest._id != id )
            {
                SW_LOG_ERROR_ID( "ModSession.IDMismatch", "Mod folder '%#' holds a manifest for '%#' - the folder name is the mod id", id, manifest._id );
                return false;
            }
            outListManifest.push_back( std::move( manifest ) );
        }
        return true;
    }

    bool ModSession::preparePack( const ModSessionRequest& request, const ModManifest& manifest, const ModSupportSettings& settings, string& outPackPath )
    {
        const string modFolder    = FileUtil::joinPath( request._installFolder, manifest._id );
        const string packPath     = FileUtil::joinPath( modFolder, ModPackBuilder::kPackFileName );
        const string sourceFolder = FileUtil::joinPath( modFolder, ModPackBuilder::kSourceFolderName );
        uint64       packSize     = 0;
        const bool   bPacked      = FileUtil::tryGetFileSize( packPath, packSize );
        const bool   bSource      = FileUtil::isDirectory( sourceFolder );
        if ( bPacked == bSource )
        {
            SW_LOG_ERROR_ID( "ModSession.PackShape", "Mod '%#' must have exactly one of %# (packed) and %#/ (source) - it has %#", manifest._id, ModPackBuilder::kPackFileName,
                             ModPackBuilder::kSourceFolderName, bPacked ? "both" : "neither" );
            return false;
        }
        if ( bPacked )
        {
            outPackPath = packPath;
            return true;
        }
        // 소스 모드는 개발 구성의 제작 반복용이다 — 배포 구성은 묶은 팩만 받는다(낱개 파일을 읽지 않는 규칙과 같다).
        if constexpr ( build::kShipping )
        {
            SW_LOG_ERROR_ID( "ModSession.SourceModInShipping", "Mod '%#' is a source folder - a packaged game loads only packed mods (%#)", manifest._id, ModPackBuilder::kPackFileName );
            return false;
        }
        if ( FileUtil::ensureDirectoryExists( request._buildFolder ) == false )
        {
            SW_LOG_ERROR_ID( "ModSession.NoBuildFolder", "The mod build folder '%#' could not be created", request._buildFolder );
            return false;
        }
        // 프로세스마다 다른 이름이다 — 시나리오 체인은 백엔드마다 자식 프로세스가 같은 폴더를 동시에 쓰고, 마운트한 팩은 열려 있어 바꿔 쓸 수 없다.
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "%#.%#.pack", manifest._id, Process::getCurrentProcessID() );
        const string builtPath = FileUtil::joinPath( request._buildFolder, textBuilder.view() );
        if ( ModPackBuilder::buildFromFolder( sourceFolder, builtPath, settings ) == false )
            return false;
        _listBuiltPackPath.push_back( builtPath );
        outPackPath = builtPath;
        return true;
    }

    bool ModSession::checkPack( string_view packPath, const ModManifest& manifest, const ModSupportSettings& settings, uint64& outContentHash )
    {
        uint64 packSize = 0;
        if ( FileUtil::tryGetFileSize( packPath, packSize ) == false || packSize > settings._maxPackBytes )
        {
            SW_LOG_ERROR_ID( "ModSession.PackTooLarge", "Mod pack '%#' is unreadable or larger than %# bytes", packPath, settings._maxPackBytes );
            return false;
        }
        vector<uint8> packBytes;
        if ( FileUtil::readFile( packPath, packBytes ) == false )
        {
            SW_LOG_ERROR_ID( "ModSession.CannotReadPack", "Mod pack '%#' could not be read", packPath );
            return false;
        }
        outContentHash = HashUtil::appendBytes64( HashUtil::kFNVOffset64, packBytes.data(), packBytes.size() );

        PackReader reader;
        if ( reader.open( packPath ) == false )
        {
            SW_LOG_ERROR_ID( "ModSession.NotAPack", "Mod pack '%#' is not a valid pack", packPath );
            return false;
        }
        if ( reader.getMountDomain().empty() == false )
        {
            SW_LOG_ERROR_ID( "ModSession.PackHasDomain", "Mod pack '%#' names mount domain '%#' - a mod pack holds full resource paths (no domain)", packPath, reader.getMountDomain() );
            return false;
        }
        if ( reader.getFileCount() > settings._maxEntryCount )
        {
            SW_LOG_ERROR_ID( "ModSession.TooManyEntries", "Mod pack '%#' holds %# files, more than %#", packPath, reader.getFileCount(), settings._maxEntryCount );
            return false;
        }
        vector<string> listEntryPath;
        reader.collectAllEntryPaths( listEntryPath );
        const string contentPrefix = ModManifest::makeContentPrefix( manifest._id );
        const string layerPrefix   = ModManifest::makeLayerRoot( manifest._id ) + "/";
        // 팩 파일마다 밝힌 덮어쓰기 목록을, 밝힌 덮어쓰기 · 층마다 팩 파일 목록을 훑지 않는다(팩 파일 수 × 밝힌 수).
        unordered_set<string> uniqueEntryPath;
        for ( const string& entryPath : listEntryPath )
        {
            uniqueEntryPath.insert( entryPath );
        }
        unordered_set<string> uniqueOverride;
        for ( const string& overridePath : manifest._listOverride )
        {
            uniqueOverride.insert( overridePath );
        }
        for ( const string& entryPath : listEntryPath )
        {
            // 층 폴더의 파일은 엔진 층이 기본 리소스에 얹으므로 밝힌 층만 둔다(정책을 지나지 않은 차이를 막는다).
            if ( StringUtil::startsWith( entryPath, layerPrefix ) && VectorUtil::contains( manifest._listLayer, entryPath.substr( layerPrefix.size() ) ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.UndeclaredLayer", "Mod pack '%#': '%#' is in the layer folder but '%#' is not a declared <Layer>", packPath, entryPath,
                                 entryPath.substr( layerPrefix.size() ) );
                return false;
            }
            PackFileEntry entry;
            if ( reader.getFileEntry( entryPath, entry ) == false || entry._uncompressedSize > settings._maxEntryBytes )
            {
                SW_LOG_ERROR_ID( "ModSession.EntryTooLarge", "Mod pack '%#': '%#' is unreadable or larger than %# bytes", packPath, entryPath, settings._maxEntryBytes );
                return false;
            }
            if ( AssetPaths::normalizeAssetID( entryPath ) != entryPath )
            {
                SW_LOG_ERROR_ID( "ModSession.BadEntryPath", "Mod pack '%#': '%#' is not a resource id in normal form (lowercase, '/', no '..')", packPath, entryPath );
                return false;
            }
            const bool bOwnContent = StringUtil::startsWith( entryPath, contentPrefix );
            if ( bOwnContent == false && uniqueOverride.contains( entryPath ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.UndeclaredEntry", "Mod pack '%#': '%#' is neither under '%#' nor a declared <Override>", packPath, entryPath, contentPrefix );
                return false;
            }
        }
        for ( const string& overridePath : manifest._listOverride )
        {
            if ( uniqueEntryPath.contains( overridePath ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.OverrideNotInPack", "Mod '%#' declares override '%#' but its pack does not hold it", manifest._id, overridePath );
                return false;
            }
            // 덮어쓸 기본 리소스가 있어야 한다 — 오타난 덮어쓰기는 아무것도 덮지 않고 새 파일만 남긴다.
            if ( AssetIO::hasAsset( AssetID{ overridePath } ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.OverrideTargetMissing", "Mod '%#' overrides '%#', which is not a resource of the game", manifest._id, overridePath );
                return false;
            }
        }
        for ( const string& layerPath : manifest._listLayer )
        {
            if ( uniqueEntryPath.contains( string( layerPrefix + layerPath ) ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.LayerNotInPack", "Mod '%#' declares layer '%#' but its pack does not hold '%#'", manifest._id, layerPath, layerPrefix + layerPath );
                return false;
            }
            // 층은 원본을 부모로 삼는다 — 원본이 없으면 얹을 곳이 없다(오타난 층이 조용히 아무것도 바꾸지 않게 두지 않는다).
            if ( AssetIO::hasAsset( AssetID{ layerPath } ) == false )
            {
                SW_LOG_ERROR_ID( "ModSession.LayerTargetMissing", "Mod '%#' layers '%#', which is not a resource of the game", manifest._id, layerPath );
                return false;
            }
        }
        return true;
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( ModStatus, "mod.status", "mod.status", "Show the mods of this session: count, id@version#content hash list and the config hash", &ModSessionInternal::runStatus );
    SW_DEV_COMMAND( ModRead, "mod.read", "mod.read <resource id>", "Read a text resource through the resource entrance (mod packs included) and reply its text on one line",
                    &ModSessionInternal::runRead );

    SW_DEV_COMMAND( ModPack, "mod.pack", "mod.pack <mod id>", "Pack the source mod <id> from the Mods folder into Saved/ModPacks/<id> (mod.xml + content.pack) for a shipping package",
                    &ModSessionInternal::runPack );

    SW_AUTOMATION_WATCH( modActiveCount, "Mods.ActiveCount", "Mods mounted for this game session", &ModSessionInternal::readActiveCount );

    bool ModSession::checkSaveCompatible( string_view savedText ) const
    {
        // 세이브 머리는 신뢰하지 않는 파일의 글이다 — 읽지 못하면 거절한다(구성을 짐작하지 않는다).
        ModSetDigest saved;
        if ( ModSetDigest::parseText( savedText, saved ) == false )
            return false;
        return _digest.checkSaveCompatible( saved );
    }
} // namespace sw
