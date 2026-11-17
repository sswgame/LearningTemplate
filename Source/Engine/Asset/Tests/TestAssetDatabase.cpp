#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
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

SW_TEST_CASE( AssetDatabaseTest, WriterGivesCopiesNewIdentityAndReplaceKeepsIt )
{
    const AssetIdentityRules rules      = AssetDatabaseTestUtil::makeRules();
    const string             prefabAbs  = sw::test::makeScratchPath( "copy.prefab.xml" );
    const string             soundAbs   = sw::test::makeScratchPath( "copy.ogg" );
    const string             newTextAbs = sw::test::makeScratchPath( "incoming.xml" );
    const string             newBytes   = sw::test::makeScratchPath( "incoming.ogg" );
    const AssetGUID          original   = AssetGUID::create();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( prefabAbs, AssetDatabaseTestUtil::makeHeaderText( original ) ) ); // 베낀 글 — 원본의 머리를 든다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( soundAbs, "ogg" ) );

    // 복사본은 새 에셋이다 — 머리는 새 GUID, 원본에는 새 .meta
    SW_REQUIRE_TRUE( rules.assignNewIdentity( prefabAbs, "a/copy.prefab.xml" ) );
    SW_REQUIRE_TRUE( rules.assignNewIdentity( soundAbs, "a/copy.ogg" ) );
    string prefabText;
    string metaText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( prefabAbs, prefabText ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( AssetMeta::makePath( soundAbs ), metaText ) );
    const AssetGUIDHeader copied = AssetGUIDHeader::read( prefabText );
    SW_REQUIRE_TRUE( copied._state == AssetGUIDHeaderState::Found );
    SW_EXPECT_TRUE( *copied._guid != original );
    AssetMeta                 meta;
    const optional<AssetGUID> soundGUID = meta.parseXML( metaText, "copy.ogg.meta" );
    SW_REQUIRE_TRUE( soundGUID.has_value() );

    // 내용 바꾸기는 GUID 를 지킨다 — 새 글에 머리가 없어도, 원본은 .meta 를 건드리지 않는다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( newTextAbs, "<Prefab formatVersion=\"0\" name=\"New\"/>\n" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( newBytes, "new ogg" ) );
    SW_REQUIRE_TRUE( rules.replaceContentKeepingIdentity( prefabAbs, "a/copy.prefab.xml", newTextAbs ) );
    SW_REQUIRE_TRUE( rules.replaceContentKeepingIdentity( soundAbs, "a/copy.ogg", newBytes ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( prefabAbs, prefabText ) );
    SW_EXPECT_TRUE( *AssetGUIDHeader::read( prefabText )._guid == *copied._guid );
    SW_EXPECT_TRUE( StringUtil::contains( prefabText, "name=\"New\"" ) );
    string soundText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( soundAbs, soundText ) );
    SW_EXPECT_EQUAL( string( "new ogg" ), soundText );
    string metaAfter;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( AssetMeta::makePath( soundAbs ), metaAfter ) );
    SW_EXPECT_EQUAL( metaText, metaAfter );
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

SW_TEST_CASE( AssetMetaTest, ImportOverrideRoundTrips )
{
    // 예외 없는 서술은 칸을 쓰지 않는다 — 이미 있는 2580 장의 `.meta` 글이 바뀌지 않는다.
    const AssetGUID guid = AssetGUID::create();
    SW_EXPECT_TRUE( AssetMeta::makeText( "a/hit.ogg", guid ).find( "_listImportOverride" ) == string::npos );

    vector<AssetImportOverride> listOverride( 2 );
    listOverride[0]._key   = "animationCodec";
    listOverride[0]._value = "raw";
    listOverride[1]._key   = "generateMips";
    listOverride[1]._value = "false";
    AssetMeta                 meta;
    const optional<AssetGUID> read = meta.parseXML( AssetMeta::makeText( "a/horse.glb", guid, listOverride ), "horse.glb.meta" );
    SW_REQUIRE_TRUE( read.has_value() );
    SW_EXPECT_TRUE( *read == guid );
    SW_REQUIRE_EQUAL( size_t{ 2 }, meta._listImportOverride.size() );
    SW_EXPECT_EQUAL( string( "animationCodec" ), meta._listImportOverride[0]._key );
    SW_EXPECT_EQUAL( string( "raw" ), meta._listImportOverride[0]._value );
    SW_EXPECT_EQUAL( string( "generateMips" ), meta._listImportOverride[1]._key );
    SW_EXPECT_EQUAL( string( "false" ), meta._listImportOverride[1]._value );
}

SW_TEST_CASE( AssetMetaTest, UnknownAttributeIsStillAnError )
{
    // 예외 칸이 생겨도 엄격한 읽기는 그대로다 — 겉 · 예외 원소의 모르는 속성이 모두 오류다.
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    AssetMeta outer;
    SW_EXPECT_FALSE( outer.parseXML( "<AssetMeta _guid=\"0123456789abcdef0011223344556677\" _source=\"horse.glb\" _codec=\"raw\"/>", "outer.meta" ).has_value() );
    AssetMeta inner;
    SW_EXPECT_FALSE( inner.parseXML( "<AssetMeta _guid=\"0123456789abcdef0011223344556677\" _source=\"horse.glb\"><_listImportOverride>"
                                     "<AssetImportOverride _key=\"animationCodec\" _value=\"raw\" _note=\"x\"/></_listImportOverride></AssetMeta>",
                                     "inner.meta" )
                         .has_value() );
}
