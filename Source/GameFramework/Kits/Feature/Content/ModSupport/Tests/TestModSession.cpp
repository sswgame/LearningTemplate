#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다
#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackWriter.h"
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
            settings._listOverridablePrefix = { "testproject/data/" };
            return settings;
        }

        static bool writeText( const string& path, string_view text )
        {
            return FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) ) && FileUtil::writeTextFile( path, text );
        }

        /** @brief 설치 폴더에 매니페스트를 씁니다(덮어쓰기는 기본 리소스 하나). */
        static bool writeManifest( const string& installFolder, string_view id )
        {
            const string text = R"(<Mod id=")" + string{ id } + R"(" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Override path=")" + string{ kBasePath } +
                                R"("/></Mod>)";
            return writeText( FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModManifest::kFileName ), text );
        }

        /** @brief 소스 모드 하나(새 리소스 하나 + 기본 리소스 덮어쓰기)를 씁니다. */
        static bool writeSourceMod( const string& installFolder, string_view id )
        {
            const string content = FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModPackBuilder::kSourceFolderName );
            return writeManifest( installFolder, id ) && writeText( FileUtil::joinPath( content, "mods/" + string{ id } + "/data/lamp.txt" ), "lamp" ) &&
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
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ), "base" ) );
    const string            root    = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest request = Internal::makeRequest( root, { "lantern" } );
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( request, Internal::makeSettings() ) );
        SW_EXPECT_EQUAL( string( "modded" ), Internal::read( Internal::kBasePath ) ); // 패치 팩이 붙인 디스크 도메인의 낱개 파일 위에 선다
        SW_EXPECT_EQUAL( string( "lamp" ), Internal::read( "mods/lantern/data/lamp.txt" ) );
        SW_REQUIRE_EQUAL( size_t{ 1 }, session.getDigest()._listEntry.size() );
        SW_EXPECT_TRUE( session.getDigest()._configHash != 0 );

        SW_EXPECT_LOG( "ModSession.AppliedTwice" ); // 구성은 세션 시작 때 한 번 고정한다
        SW_EXPECT_FALSE( session.apply( request, Internal::makeSettings() ) );
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
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ), "base" ) );
    const string            root    = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest request = Internal::makeRequest( root, { "lantern" } );
    const string            modDir  = FileUtil::joinPath( request._installFolder, "lantern" );
    const string            source  = FileUtil::joinPath( modDir, ModPackBuilder::kSourceFolderName );
    AssetManager&           assets  = engine::getSubsystem<AssetManager>();
    SW_REQUIRE_TRUE( Internal::writeSourceMod( request._installFolder, "lantern" ) );

    uint64 sourceHash = 0;
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( request, Internal::makeSettings() ) );
        sourceHash = session.getDigest()._configHash;
    }
    SW_REQUIRE_TRUE( ModPackBuilder::buildFromFolder( source, FileUtil::joinPath( modDir, ModPackBuilder::kPackFileName ), Internal::makeSettings() ) );
    SW_REQUIRE_TRUE( FileUtil::removeDirectory( source ) );

    ModAllowList allowList;
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( request, Internal::makeSettings() ) );
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
    SW_EXPECT_FALSE( server.apply( serverRequest, Internal::makeSettings() ) );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
    SW_EXPECT_EQUAL( string( "base" ), Internal::read( Internal::kBasePath ) );
}

// 설치 폴더는 바깥 입력이다 — 경로 탈출 id · 없는 모드 · 모양이 둘인 모드 · 밝히지 않은 항목 · 경로 탈출 항목 · 한계를 넘는 팩은 하나도 올리지 않는다.
SW_TEST_CASE( ModSessionTest, RejectsUntrustedFolders )
{
    using Internal = TestModSessionInternal;
    test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ), "base" ) );
    const string  root     = test::makeScratchPath( "ModRoot" );
    AssetManager& assets   = engine::getSubsystem<AssetManager>();
    const auto    tryApply = [&assets, &root]( vector<string> listID )
    {
        ModSession session( assets );
        const bool bApplied = session.apply( TestModSessionInternal::makeRequest( root, std::move( listID ) ), TestModSessionInternal::makeSettings() );
        return bApplied || assets.getPackManager().hasPatchPacks(); // 실패한 적용은 아무것도 남기지 않는다
    };
    const string installFolder = Internal::makeRequest( root, {} )._installFolder;

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
        PackWriterDesc desc;
        PackWriter     writer( desc );
        for ( const std::pair<string, string>& entry : listEntry )
        {
            writer.addBytes( entry.first, vector<uint8>( entry.second.begin(), entry.second.end() ) );
        }
        return TestModSessionInternal::writeManifest( installFolder, id ) &&
               writer.writePackFile( FileUtil::joinPath( FileUtil::joinPath( installFolder, id ), ModPackBuilder::kPackFileName ) );
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
        listMany.emplace_back( formatToString( "mods/crowd/data/%#.txt", index ), "x" );
    }
    SW_REQUIRE_TRUE( writePack( "crowd", listMany ) );
    SW_EXPECT_LOG( "ModSession.TooManyEntries" );
    SW_EXPECT_FALSE( tryApply( { "crowd" } ) );

    // 매니페스트 id 와 폴더 이름이 다르다(다른 모드를 흉내 낸다).
    SW_REQUIRE_TRUE( Internal::writeManifest( installFolder, "real" ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( installFolder, "fake" ) ) );
    SW_REQUIRE_TRUE( FileUtil::copyFile( FileUtil::joinPath( FileUtil::joinPath( installFolder, "real" ), ModManifest::kFileName ),
                                         FileUtil::joinPath( FileUtil::joinPath( installFolder, "fake" ), ModManifest::kFileName ) ) );
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
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/quests.xml" ),
                                          R"(<QuestCatalog><Quest id="base_quest"><Stage id="done" complete="true"/></Quest></QuestCatalog>)" ) );
    const string            root          = test::makeScratchPath( "ModRoot" );
    const ModSessionRequest request       = Internal::makeRequest( root, { "tide" } );
    const string            modDir        = FileUtil::joinPath( request._installFolder, "tide" );
    const string            content       = FileUtil::joinPath( modDir, ModPackBuilder::kSourceFolderName );
    const string            layerFile     = FileUtil::joinPath( content, ModManifest::makeLayerRoot( "tide" ) + "/" + basePath );
    const vector<string>    listLayerBase = ResourceLayers::getLayers();
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( modDir, ModManifest::kFileName ),
                                          R"(<Mod id="tide" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Layer path=")" + basePath + R"("/></Mod>)" ) );
    SW_REQUIRE_TRUE( Internal::writeText( layerFile, R"(<QuestCatalog><Quest id="tide_quest"><Stage id="done" complete="true"/></Quest></QuestCatalog>)" ) );

    AssetManager& assets = engine::getSubsystem<AssetManager>();
    {
        ModSession session( assets );
        SW_REQUIRE_TRUE( session.apply( request, Internal::makeSettings() ) );
        vector<string> listExpected = listLayerBase;
        listExpected.push_back( "mods/tide/layer" );
        SW_EXPECT_TRUE( ResourceLayers::getLayers() == listExpected );
        const shared_ptr<const QuestCatalog> pQuests = SharedAssetCache<QuestCatalog>::acquire( basePath );
        SW_REQUIRE_TRUE( pQuests != nullptr );
        SW_EXPECT_TRUE( pQuests->find( hashed_string( "base_quest" ) ) != nullptr ); // 원본 항목은 그대로
        SW_EXPECT_TRUE( pQuests->find( hashed_string( "tide_quest" ) ) != nullptr ); // 층이 더한 항목
    }
    SW_EXPECT_TRUE( ResourceLayers::getLayers() == listLayerBase );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );

    // 밝히지 않은 층 파일.
    SW_REQUIRE_TRUE( Internal::writeText( FileUtil::joinPath( content, ModManifest::makeLayerRoot( "tide" ) + "/testproject/data/welcome.xml" ), "sneaky" ) );
    SW_REQUIRE_TRUE( Internal::writeText( domain.makeDiskPath( "data/welcome.xml" ), "base" ) );
    ModSession sneaky( assets );
    SW_EXPECT_LOG( "ModSession.UndeclaredLayer" );
    SW_EXPECT_FALSE( sneaky.apply( request, Internal::makeSettings() ) );
    SW_EXPECT_TRUE( ResourceLayers::getLayers() == listLayerBase );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
}

// 켤 모드가 없으면 아무것도 하지 않는다 — 모드 없는 실행은 이 키트가 있어도 그대로다.
SW_TEST_CASE( ModSessionTest, NoModsChangesNothing )
{
    AssetManager& assets = engine::getSubsystem<AssetManager>();
    ModSession    session( assets );
    SW_EXPECT_TRUE( session.apply( TestModSessionInternal::makeRequest( test::makeScratchPath( "ModRoot" ), {} ), TestModSessionInternal::makeSettings() ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, session.getDigest()._configHash );
    SW_EXPECT_FALSE( assets.getPackManager().hasPatchPacks() );
}
