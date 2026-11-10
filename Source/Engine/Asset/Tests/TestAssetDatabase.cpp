#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetDatabase.h"

using namespace sw;

namespace
{
    struct AssetDatabaseTestUtil
    {
        static AssetIdentityRules makeRules()
        {
            AssetIdentityRules rules;
            rules._listHeaderSuffix  = { ".xml", ".material" };
            rules._listSkipPattern   = { "*/golden/*", "*.md" };
            rules._listDerivedFolder = { "models", "textures" };
            return rules;
        }

        static string makeHeaderText( const AssetGUID& guid ) { return AssetGUIDHeader::write( "<Prefab formatVersion=\"0\"/>\n", guid ); }

        static bool hasIssue( const vector<AssetIdentityIssue>& listIssue, AssetIdentityIssueKind kind, string_view assetID )
        {
            for ( const AssetIdentityIssue& issue : listIssue )
            {
                if ( issue._kind == kind && issue._assetID == assetID )
                    return true;
            }
            return false;
        }
    };
} // namespace

SW_TEST_CASE( AssetDatabaseTest, ClassifiesByRuleTable )
{
    const AssetIdentityRules rules = AssetDatabaseTestUtil::makeRules();
    SW_EXPECT_TRUE( rules.classify( "common/prefabs/bow.prefab.xml" ) == AssetIdentityKind::Header );
    SW_EXPECT_TRUE( rules.classify( "engine/materials/grass.material" ) == AssetIdentityKind::Header );
    SW_EXPECT_TRUE( rules.classify( "common/audio/hit.ogg" ) == AssetIdentityKind::Source );
    SW_EXPECT_TRUE( rules.classify( "common/audio/hit.ogg.meta" ) == AssetIdentityKind::Sidecar );
    SW_EXPECT_TRUE( rules.classify( "game/empty/automation/golden/a.dx11.ppm" ) == AssetIdentityKind::Skipped );
    SW_EXPECT_TRUE( rules.classify( "common/credits.md" ) == AssetIdentityKind::Skipped );
    SW_EXPECT_TRUE( rules.isUnderDerivedFolder( "common/models/barrel.mesh" ) );
    SW_EXPECT_FALSE( rules.isUnderDerivedFolder( "common/models_raw/barrel.glb" ) );
}

SW_TEST_CASE( AssetDatabaseTest, IndexesHeadersAndSidecarsBothWays )
{
    const AssetIdentityRules        rules  = AssetDatabaseTestUtil::makeRules();
    const AssetGUID                 prefab = AssetGUID::create();
    const AssetGUID                 sound  = AssetGUID::create();
    const vector<AssetIdentityFile> listFile{
        { "common/prefabs/bow.prefab.xml", AssetDatabaseTestUtil::makeHeaderText( prefab ) },
        { "common/audio/hit.ogg", {} },
        { "common/audio/hit.ogg.meta", AssetMeta::makeText( "common/audio/hit.ogg", sound ) },
        { "common/credits.md", {} },
    };
    vector<AssetIdentityIssue> listIssue;
    const AssetDatabase        database = AssetDatabase::build( listFile, rules, listIssue );
    SW_EXPECT_TRUE( listIssue.empty() );
    SW_EXPECT_EQUAL( size_t{ 2 }, database.getCount() );
    SW_EXPECT_EQUAL( string( "common/prefabs/bow.prefab.xml" ), database.findPath( prefab ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "common/audio/hit.ogg" ), database.findPath( sound ).value_or( string{} ) ); // 서술의 GUID 는 원본을 가리킨다
    SW_REQUIRE_TRUE( database.findGUID( "Common/Audio/Hit.ogg" ).has_value() );                           // 대소문자는 가리지 않는다
    SW_EXPECT_TRUE( *database.findGUID( "common/audio/hit.ogg" ) == sound );
    SW_EXPECT_FALSE( database.findGUID( "common/credits.md" ).has_value() );
}

SW_TEST_CASE( AssetDatabaseTest, ReportsBrokenPairsAndCopiesWithoutGuessing )
{
    const AssetIdentityRules        rules  = AssetDatabaseTestUtil::makeRules();
    const AssetGUID                 shared = AssetGUID::create();
    const vector<AssetIdentityFile> listFile{
        { "common/prefabs/a.prefab.xml", AssetDatabaseTestUtil::makeHeaderText( shared ) },
        { "common/prefabs/b.prefab.xml", AssetDatabaseTestUtil::makeHeaderText( shared ) }, // 복사본
        { "common/prefabs/c.prefab.xml", "<Prefab/>" }, // 머리 없음
        { "common/audio/lonely.ogg", {} }, // .meta 없음
        { "common/audio/moved.ogg.meta", AssetMeta::makeText( "common/audio/moved.ogg", AssetGUID::create() ) }, // 원본만 옮겼다
        { "common/audio/wrong.ogg", {} },
        { "common/audio/wrong.ogg.meta", AssetMeta::makeText( "common/audio/other.ogg", AssetGUID::create() ) }, // 다른 원본을 적었다
    };
    vector<AssetIdentityIssue> listIssue;
    const AssetDatabase        database = AssetDatabase::build( listFile, rules, listIssue );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::DuplicateGUID, "common/prefabs/b.prefab.xml" ) );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::MissingHeader, "common/prefabs/c.prefab.xml" ) );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::MissingMeta, "common/audio/lonely.ogg" ) );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::OrphanMeta, "common/audio/moved.ogg.meta" ) );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::InvalidMeta, "common/audio/wrong.ogg.meta" ) );
    SW_EXPECT_EQUAL( size_t{ 5 }, listIssue.size() );
    // 겹친 GUID 는 정렬 순서로 먼저 온 쪽만 남는다 — 새 GUID 로 메우지 않는다
    SW_EXPECT_EQUAL( string( "common/prefabs/a.prefab.xml" ), database.findPath( shared ).value_or( string{} ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, database.getCount() );
}

SW_TEST_CASE( AssetDatabaseTest, MetaTextRoundTripsAndRejectsOtherShapes )
{
    const AssetGUID           guid = AssetGUID::create();
    AssetMeta                 meta;
    const optional<AssetGUID> read = meta.parseXML( AssetMeta::makeText( "a/b/hit.ogg", guid ), "hit.ogg.meta" );
    SW_REQUIRE_TRUE( read.has_value() );
    SW_EXPECT_TRUE( *read == guid );
    SW_EXPECT_EQUAL( string( "hit.ogg" ), meta._source );
    SW_EXPECT_EQUAL( string( "a/b/hit.ogg.meta" ), AssetMeta::makePath( "a/b/hit.ogg" ) );

    SW_EXPECT_LOG( "AssetMeta.InvalidGUID" );
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    AssetMeta badGUID;
    SW_EXPECT_FALSE( badGUID.parseXML( "<AssetMeta _guid=\"ABC\" _source=\"hit.ogg\"/>", "bad.meta" ).has_value() );
    AssetMeta unknownField;
    SW_EXPECT_FALSE( unknownField.parseXML( "<AssetMeta _guid=\"0123456789abcdef0011223344556677\" _source=\"hit.ogg\" _extra=\"1\"/>", "extra.meta" ).has_value() );
}
