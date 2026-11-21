#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다
#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModAllowList.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModPackBuilder.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

// 모드 세션 — 엔진 팩 VFS 에 올리고 · 덮어쓰기가 낱개 파일 위에 서고 · 세션이 끝나면 내려간다. 바깥 폴더의 잘못된 모드는 하나도 올리지 않고 거절한다.
// 기본 리소스는 케이스 도메인(`testproject/…`, 붙인 디스크 도메인 — 낱개 파일을 먼저 읽는 자리)이다.

using namespace sw;

namespace
{
    struct TestModSessionInternal
    {
        static constexpr const utf8* kBasePath = "testproject/data/welcome.xml";

        static ModSupportSettings makeSettings()
        {
            ModSupportSettings settings;
            settings._contentVersion        = 1;
            settings._maxModCount           = 4;
            settings._maxManifestBytes      = 4096;
            settings._maxPackBytes          = 1u << 20;
            settings._maxEntryCount         = 8;
            settings._maxEntryBytes         = 4096;
            settings._packBlockSize         = kPackMinBlockSize;
            settings._listOverridablePrefix = { "testproject/data/" };
            settings._listStoredExtension   = { ".dds" };
            return settings;
        }

        static bool writeText( const string& path, string_view text )
        {
            return FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( FileUtil::getDirectoryPart( path ) ) ) && FileUtil::writeTextFile( FileUtil::toDiskPath( path ), text );
        }

        /** @brief 설치 폴더에 매니페스트를 씁니다(덮어쓰기는 기본 리소스 하나). */
        static bool writeManifest( const string& installFolder, string_view id )
        {
            StringBuilder<constant::buffer::kMax512> text;
            text.appendFormat( R"(<Mod id="%#" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Override path="%#"/></Mod>)", id, kBasePath );
            return writeText( FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModManifest::kFileName ), text.view() );
        }

        /** @brief 소스 모드 하나(새 리소스 하나 + 기본 리소스 덮어쓰기)를 씁니다. */
        static bool writeSourceMod( const string& installFolder, string_view id )
        {
            const string                             content = FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModPackBuilder::kSourceFolderName );
            StringBuilder<constant::buffer::kMax256> dataFilePath;
            dataFilePath.appendFormat( "mods/%#/data/lamp.txt", id );
            return writeManifest( installFolder, id ) && writeText( FileUtil::joinPath( content, dataFilePath.view() ), "lamp" ) &&
                   writeText( FileUtil::joinPath( content, kBasePath ), "modded" );
        }

        static string read( string_view assetID )
        {
            string text;
            return AssetIO::readTextAsset( AssetID{ string{ assetID } }, text ) ? text : string{ "<missing>" };
        }

        static ModSessionRequest makeRequest( const string& root, vector<string> listID )
        {
            ModSessionRequest request;
            request._installFolder = FileUtil::joinPath( root, "Mods" );
            request._buildFolder   = FileUtil::joinPath( root, "Built" );
            request._listEnabledID = std::move( listID );
            return request;
        }
    };
} // namespace

SW_TEST_CASE( ModSessionTest, SourceModOverridesAndReleases )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    const AbsolutePath      root    = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest request = Internal::makeRequest( root.str(), { "lantern" } );
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
        SW_EXPECT_EQUAL( string( "modded" ), Internal::read( Internal::kBasePath ) ); // 패치 팩이 붙인 디스크 도메인의 낱개 파일 위에 선다
        SW_EXPECT_EQUAL( string( "lamp" ), Internal::read( "mods/lantern/data/lamp.txt" ) );
        SW_REQUIRE_EQUAL( size_t{ 1 }, session.getDigest()._listEntry.size() );
        SW_EXPECT_TRUE( session.getDigest()._configHash != 0 );

        SW_EXPECT_LOG( "ModSession.AppliedTwice" ); // 구성은 세션 시작 때 한 번 고정한다
        SW_EXPECT_FALSE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
    }
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
    SW_EXPECT_EQUAL( string( "base" ), Internal::read( Internal::kBasePath ) );
    SW_EXPECT_EQUAL( string( "<missing>" ), Internal::read( "mods/lantern/data/lamp.txt" ) );
}

// 같은 내용이면 소스 모드와 묶은 모드가 같은 구성 해시다 — 서버(묶은 팩)와 제작자(소스 폴더)가 대조된다.
SW_TEST_CASE( ModSessionTest, PackedModMatchesItsSourceHash )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    const AbsolutePath      root    = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest request = Internal::makeRequest( root.str(), { "lantern" } );
    const string            modDir  = FileUtil::joinPath( request._installFolder, "lantern" );
    const string            source  = FileUtil::joinPath( modDir, ModPackBuilder::kSourceFolderName );
    AssetManager&           assets  = engine::getSubsystem<AssetManager>();
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );

    uint64 sourceHash = 0;
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
        sourceHash = session.getDigest()._configHash;
    }
    SW_REQUIRE_TRUE( ModPackBuilder::buildFromFolder( source, FileUtil::joinPath( modDir, ModPackBuilder::kPackFileName ), Internal::makeSettings() ) );
    SW_REQUIRE_TRUE( FileUtil::removeDirectory( FileUtil::toDiskPath( source ) ) );

    ModAllowList allowList;
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
        SW_EXPECT_EQUAL( sourceHash, session.getDigest()._configHash );
        SW_EXPECT_EQUAL( string( "modded" ), Internal::read( Internal::kBasePath ) );
        allowList._listEntry = session.getDigest()._listEntry; // 운영자가 이 내용을 허락한다
    }

    // 서버: 허용 목록과 다른 내용은 올리지 않는다.
    allowList._listEntry[0]._contentHash ^= 1u;
    ModSessionRequest serverRequest = request;
    serverRequest._pAllowList       = &allowList;
    ModSession server( assets );
    SW_EXPECT_LOG( "ModAllowList.Mismatch" );
    SW_EXPECT_FALSE( server.apply( getContentPreparationTokenForTest(), serverRequest, Internal::makeSettings() ) );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
    SW_EXPECT_EQUAL( string( "base" ), Internal::read( Internal::kBasePath ) );
}

// 설치 폴더는 바깥 입력이다 — 경로 탈출 id · 없는 모드 · 모양이 둘인 모드 · 밝히지 않은 항목 · 경로 탈출 항목 · 한계를 넘는 팩은 하나도 올리지 않는다.
SW_TEST_CASE( ModSessionTest, RejectsUntrustedFolders )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    const AbsolutePath root     = test::makeScratchPath( "ModRoot" );
    AssetManager&      assets   = engine::getSubsystem<AssetManager>();
    const auto         tryApply = [&assets, &root]( vector<string> listID )
    {
        ModSession session( assets );
        const bool bApplied = session.apply( getContentPreparationTokenForTest(), TestModSessionInternal::makeRequest( root.str(), std::move( listID ) ), TestModSessionInternal::makeSettings() );
        return bApplied || assets.getPackManager().hasPatchPacks(); // 실패한 적용은 아무것도 남기지 않는다
    };
    const string installFolder = Internal::makeRequest( root.str(), {} )._installFolder;

    SW_EXPECT_LOG( "ModSession.BadID" );
    SW_EXPECT_FALSE( tryApply( { "../outside" } ) );
    SW_EXPECT_LOG( "ModSession.MissingMod" );
    SW_EXPECT_FALSE( tryApply( { "absent" } ) );

    // 묶은 팩과 소스 폴더가 함께 있으면 어느 쪽이 켜졌는지 모른다.
    SW_REQUIRE_TRUE( Internal::writeSourceMod( installFolder, "both" ) );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( FileUtil::joinPath( installFolder, "both" ), ModPackBuilder::kPackFileName ), "not a pack" ) );
    SW_EXPECT_LOG( "ModSession.PackShape" );
    SW_EXPECT_FALSE( tryApply( { "both" } ) );

    // 밝히지 않은 기본 리소스를 덮어쓰는 항목.
    const auto writePack = [&installFolder]( string_view id, const vector<std::pair<string, string>>& listEntry )
    {
        return TestModSessionInternal::writeManifest( installFolder, id ) &&
               PackTestUtil::writeTextPack( FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModPackBuilder::kPackFileName ), "", listEntry );
    };
    SW_REQUIRE_TRUE( writePack( "sneaky", {
                                              {         Internal::kBasePath, "modded"},
                                              {"testproject/data/other.xml", "hijack"}
    } ) );
    SW_EXPECT_LOG( "ModSession.UndeclaredEntry" );
    SW_EXPECT_FALSE( tryApply( { "sneaky" } ) );

    // 밝힌 덮어쓰기가 팩에 없다.
    SW_REQUIRE_TRUE( writePack( "hollow", {
                                              { "mods/hollow/data/a.txt", "a" }
    } ) );
    SW_EXPECT_LOG( "ModSession.OverrideNotInPack" );
    SW_EXPECT_FALSE( tryApply( { "hollow" } ) );

    // 항목 수 한계(8)를 넘는 팩.
    vector<std::pair<string, string>> listMany{
        { Internal::kBasePath, "modded" }
    };
    for ( int32 index = 0; index < 9; ++index )
    {
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "mods/crowd/data/%#.txt", index );
        listMany.emplace_back( string( textBuilder.view() ), "x" );
    }
    SW_REQUIRE_TRUE( writePack( "crowd", listMany ) );
    SW_EXPECT_LOG( "ModSession.TooManyEntries" );
    SW_EXPECT_FALSE( tryApply( { "crowd" } ) );

    // 매니페스트 id 와 폴더 이름이 다르다(다른 모드를 흉내 낸다).
    SW_REQUIRE_TRUE( Internal::writeManifest( installFolder, "real" ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( FileUtil::joinPath( installFolder, "fake" ) ) ) );
    SW_REQUIRE_TRUE( FileUtil::copyFile( FileUtil::toDiskPath( FileUtil::joinPath( FileUtil::joinPath( installFolder, "real" ), ModManifest::kFileName ) ),
                                         FileUtil::toDiskPath( FileUtil::joinPath( FileUtil::joinPath( installFolder, "fake" ), ModManifest::kFileName ) ) ) );
    SW_EXPECT_LOG( "ModSession.IDMismatch" );
    SW_EXPECT_FALSE( tryApply( { "fake" } ) );

    SW_EXPECT_EQUAL( string( "base" ), Internal::read( Internal::kBasePath ) );
}

// 층을 밝힌 모드는 엔진 덮어쓰기 층 끝에 자기 층 뿌리를 더하고, 기본 표는 원본을 부모로 항목이 더해져 읽힌다. 세션이 끝나면 층 목록이 앞 값으로 돌아간다.
// 층 폴더에 밝히지 않은 파일이 있으면(정책을 지나지 않고 기본 리소스를 바꾼다) 하나도 올리지 않는다.
SW_TEST_CASE( ModSessionTest, LayerModAddsEntriesAndReleases )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    const string              basePath = "testproject/data/quests.xml";
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/quests.xml" ).str(),
                                          R"(<QuestCatalog><Quest id="base_quest"><Stage id="done" complete="true"/></Quest></QuestCatalog>)" ) );
    const AbsolutePath                       root    = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest                  request = Internal::makeRequest( root.str(), { "tide" } );
    const string                             modDir  = FileUtil::joinPath( request._installFolder, "tide" );
    const string                             content = FileUtil::joinPath( modDir, ModPackBuilder::kSourceFolderName );
    StringBuilder<constant::buffer::kMax256> layerRelative;
    layerRelative.appendFormat( "%#/%#", ModManifest::makeLayerRoot( "tide" ), basePath );
    const string                             layerFile     = FileUtil::joinPath( content, layerRelative.view() );
    const vector<string>                     listLayerBase = engine::getSubsystem<ResourceComposition>().getLayers();
    StringBuilder<constant::buffer::kMax512> manifestText;
    manifestText.appendFormat( R"(<Mod id="tide" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Layer path="%#"/></Mod>)", basePath );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( modDir, ModManifest::kFileName ), manifestText.view() ) );
    SW_REQUIRE_TRUE( Internal::writeText( layerFile, R"(<QuestCatalog><Quest id="tide_quest"><Stage id="done" complete="true"/></Quest></QuestCatalog>)" ) );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
        vector<string> listExpected = listLayerBase;
        listExpected.push_back( "mods/tide/layer" );
        SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().getLayers() == listExpected );
        const shared_ptr<const QuestCatalog> pQuests = SharedAssetCache<QuestCatalog>::acquire( basePath ).snapshot();
        SW_REQUIRE_TRUE( pQuests != nullptr );
        SW_EXPECT_TRUE( pQuests->find( hashed_string( "base_quest" ) ) != nullptr ); // 원본 항목은 그대로
        SW_EXPECT_TRUE( pQuests->find( hashed_string( "tide_quest" ) ) != nullptr ); // 층이 더한 항목
    }
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().getLayers() == listLayerBase );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );

    // 밝히지 않은 층 파일.
    StringBuilder<constant::buffer::kMax256> sneakyRelative;
    sneakyRelative.appendFormat( "%#/testproject/data/welcome.xml", ModManifest::makeLayerRoot( "tide" ) );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( content, sneakyRelative.view() ), "sneaky" ) );
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    ModSession sneaky( assets );
    SW_EXPECT_LOG( "ModSession.UndeclaredLayer" );
    SW_EXPECT_FALSE( sneaky.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
    SW_EXPECT_TRUE( engine::getSubsystem<ResourceComposition>().getLayers() == listLayerBase );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
}

// 켤 모드가 없으면 아무것도 하지 않는다 — 모드 없는 실행은 이 키트가 있어도 그대로다.
SW_TEST_CASE( ModSessionTest, NoModsChangesNothing )
{
    AssetManager& assets = engine::getSubsystem<AssetManager>();
    ModSession    session( assets );
    SW_EXPECT_TRUE( session.apply( getContentPreparationTokenForTest(), TestModSessionInternal::makeRequest( test::makeScratchPath( "ModRoot" ).str(), {} ), TestModSessionInternal::makeSettings() ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, session.getDigest()._configHash );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
}

/**
 * @brief [ModSessionTest] 모드가 새로 더한 에셋도 GUID 참조로 풀린다 — 모드 팩이 자기 색인(`assetindex.xml`)을 싣고, 세션이 런타임 색인에 합쳤다가 풀면 걷는다
 */
SW_TEST_CASE( ModSessionTest, ModAssetsResolveByGUIDWhileApplied )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    const AbsolutePath      root    = test::makeScratchPath( "ModIndexRoot" );
    const ModSessionRequest request = Internal::makeRequest( root.str(), { "lantern" } );
    const string            source  = FileUtil::joinPath( FileUtil::joinPath( request._installFolder, "lantern" ), ModPackBuilder::kSourceFolderName );
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( source, "mods/lantern/data/lamp.xml" ), R"(<?asset guid="0000000000000000000000000000c0de"?><Lamp/>)" ) );
    const AssetGUID guid = *AssetGUID::tryParse( "0000000000000000000000000000c0de" );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
        SW_EXPECT_EQUAL( string( "mods/lantern/data/lamp.xml" ), AssetRegistry::resolve( guid ).value_or( string{} ) );
    }
    SW_EXPECT_FALSE( AssetRegistry::resolve( guid ).has_value() );
}

/**
 * @brief [ModSessionTest] 모드 팩 색인이 이미 풀리는 GUID(기본 에셋)를 자기 파일로 돌리면 적용을 거절한다 — 덮어쓰기는 밝힌 `<Override>` 로만 한다
 */
SW_TEST_CASE( ModSessionTest, ModIndexCannotRedirectAKnownGUID )
{
    using Internal = TestModSessionInternal;
    SW_EXPECT_LOG( "ModSession.IndexRedirectsAsset" );
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ).str(), "base" ) );
    const AbsolutePath      root    = test::makeScratchPath( "ModIndexRedirectRoot" );
    const ModSessionRequest request = Internal::makeRequest( root.str(), { "lantern" } );
    const string            source  = FileUtil::joinPath( FileUtil::joinPath( request._installFolder, "lantern" ), ModPackBuilder::kSourceFolderName );
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( source, "mods/lantern/data/lamp.xml" ), R"(<?asset guid="0000000000000000000000000000beef"?><Lamp/>)" ) );
    const AssetGUID guid = *AssetGUID::tryParse( "0000000000000000000000000000beef" );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    // 같은 GUID 가 이미 기본 에셋으로 풀리게 둔다 — 프로세스 표에 한 줄을 더해 걸고, 끝에 원래 표로 되돌린다.
    const AssetRegistry previous = AssetRegistry::install( AssetRegistry{} );
    AssetRegistry       withBase = previous;
    withBase.put( guid, "data/welcome.xml" );
    (void)AssetRegistry::install( withBase );
    {
        ModSession session( assets );
        SW_EXPECT_FALSE( session.apply( getContentPreparationTokenForTest(), request, Internal::makeSettings() ) );
    }
    (void)AssetRegistry::install( previous );
}

// 모드 팩도 배포 팩과 같은 저장 방식 규칙(PackEntryStorageRule)을 쓴다 — 범위로 읽는 텍스처(.dds)는 Stored, 글은 블록 압축이다.
// 변이: 빌더가 저장 방식을 Blocks 로 고정하면(예전 길) 첫 단언이 진다.
SW_TEST_CASE( ModSessionTest, ModPackStoresTexturesUncompressed )
{
    using Internal                 = TestModSessionInternal;
    const AbsolutePath root        = test::makeScratchPath( "ModPackStorage" );
    const AbsolutePath source      = root.join( ModPackBuilder::kSourceFolderName );
    const AbsolutePath packPath    = root.join( ModPackBuilder::kPackFileName );
    const string       texturePath = "mods/stone/textures/stone.dds";
    const string       dataPath    = "mods/stone/data/stone.txt";
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( source.str(), texturePath ), "not really a dds - bytes only" ) );
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( source.str(), dataPath ), "stone" ) );
    SW_REQUIRE_TRUE( ModPackBuilder::buildFromFolder( source.str(), packPath.str(), Internal::makeSettings() ) );

    PackReader reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
    PackFileEntry texture;
    PackFileEntry data;
    SW_REQUIRE_TRUE( reader.getFileEntry( texturePath, texture ) );
    SW_REQUIRE_TRUE( reader.getFileEntry( dataPath, data ) );
    SW_EXPECT_TRUE( texture._storage == PackEntryStorage::Stored );
    SW_EXPECT_TRUE( data._storage == PackEntryStorage::Blocks );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
