#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetScanCache.h"

using namespace sw;

namespace
{
    struct AssetDatabaseTestUtil
    {
        static AssetIdentityRules makeRules()
        {
            AssetIdentityRules rules;
            rules._listHeaderSuffix   = { ".xml", ".material" };
            rules._listSkipPattern    = { "*/golden/*", "*.md" };
            rules._listKeptDerived    = { "*/models/*.mirrortable.xml" };
            rules._listImportedFolder = {
                AssetImportedFolder{  "models",   "models_raw", { ".glb", ".vrm" }, { ".mesh", ".material", ".skeleton.xml" },  true},
                AssetImportedFolder{"textures", "textures_raw",         { ".png" },                                { ".dds" }, false},
            };
            return rules;
        }

        static string makeHeaderText( const AssetGUID& guid ) { return AssetGUIDHeader::write( "<Prefab formatVersion=\"0\"/>\n", guid ); }

        /** @brief 내용 주소 글(`store:<@p hashDigit 64 자><tail>`)입니다. */
        static string makeStoredText( utf8 hashDigit, string_view tail )
        {
            StringBuilder<constant::buffer::kMax256> builder;
            builder.appendFormat( "store:%#%#", string( AssetSourceAddress::kContentHashLength, hashDigit ), tail );
            return string( builder.view() );
        }

        /** @brief 원본 자리 @p source 를 적은 서술 글입니다. */
        static string makeMetaText( string_view source, string_view children )
        {
            StringBuilder<constant::buffer::kMax512> builder;
            builder.appendFormat( "<AssetMeta _guid=\"0123456789abcdef0011223344556677\" _source=\"%#\">%#</AssetMeta>", source, children );
            return string( builder.view() );
        }

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
    SW_EXPECT_TRUE( rules.classify( "game/playground/automation/golden/a.dx11.ppm" ) == AssetIdentityKind::Skipped );
    SW_EXPECT_TRUE( rules.classify( "common/credits.md" ) == AssetIdentityKind::Skipped );
    SW_EXPECT_TRUE( rules.isUnderDerivedFolder( "common/models/barrel.mesh" ) );
    SW_EXPECT_FALSE( rules.isUnderDerivedFolder( "common/models_raw/barrel.glb" ) );
}

/** @brief [AssetDatabaseTest] 손으로 적는 글은 임포트 산출물 폴더에 두지 못한다 — 허락한 곁 파일 · 산출물 바이트 · 폴더 밖은 괜찮다 */
SW_TEST_CASE( AssetDatabaseTest, HandAuthoredTextIsMisplacedInDerivedFolders )
{
    const AssetIdentityRules rules = AssetDatabaseTestUtil::makeRules();
    SW_EXPECT_TRUE( rules.isMisplacedInDerivedFolder( "game/x/models/head/head.facial.xml" ) );
    SW_EXPECT_TRUE( rules.isMisplacedInDerivedFolder( "game/x/textures/wall.material" ) );
    SW_EXPECT_FALSE( rules.isMisplacedInDerivedFolder( "game/x/models/head/head.mirrortable.xml" ) );
    SW_EXPECT_FALSE( rules.isMisplacedInDerivedFolder( "game/x/models/head.mesh" ) );
    SW_EXPECT_FALSE( rules.isMisplacedInDerivedFolder( "game/x/data/head.facial.xml" ) );
    SW_EXPECT_FALSE( rules.isMisplacedInDerivedFolder( "game/x/models_raw/head.facial.xml" ) );
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

/**
 * @brief [AssetDatabaseTest] 임포트 산출물은 원본 GUID 와 산출물 이름에서 나온 줄을 갖는다 — 원본을 옮겨도 산출물의 GUID 는 그대로고, 손으로 커밋한 파일(머리 · .meta)은 자기 정체성, 원본 없는 공유 결과는 줄이 없다
 */
SW_TEST_CASE( AssetDatabaseTest, ImportedOutputsGetLinesFromTheirSource )
{
    const AssetIdentityRules rules  = AssetDatabaseTestUtil::makeRules();
    const AssetGUID          source = AssetGUID::create();
    const AssetGUID          mirror = AssetGUID::create();
    const auto               scan   = [&]( const utf8* pStem, vector<AssetIdentityIssue>& outListIssue )
    {
        StringBuilder<constant::buffer::kMax256> builder;
        const auto                               makeID = [&builder, pStem]( const utf8* pPrefix, const utf8* pTail )
        {
            builder.clear();
            builder.appendFormat( "common/%#/%#%#", pPrefix, pStem, pTail );
            return string( builder.view() );
        };
        const vector<AssetIdentityFile> listFile{
            { makeID( "models_raw", ".glb" ), {} },
            { makeID( "models_raw", ".glb.meta" ), AssetMeta::makeText( makeID( "models_raw", ".glb" ), source ) },
            { makeID( "models", ".mesh" ), {} },
            { makeID( "models", "/materials/mi_eyes.material" ), "<MaterialDef/>" },
            { makeID( "models", "/mirrors.xml" ), AssetDatabaseTestUtil::makeHeaderText( mirror ) },
            { "common/models/_shared/materials/00ff.material", "<MaterialDef/>" },
        };
        return AssetDatabase::build( listFile, rules, outListIssue );
    };
    vector<AssetIdentityIssue> listIssue;
    const AssetDatabase        database = scan( "kaykit/knight", listIssue );
    const AssetDatabase        moved    = scan( "people/hero", listIssue );
    SW_EXPECT_TRUE( listIssue.empty() );
    SW_EXPECT_EQUAL( size_t{ 4 }, database.getCount() ); // 원본 · 산출물 둘 · 손으로 커밋한 표
    SW_EXPECT_EQUAL( string( "common/models/kaykit/knight.mesh" ), database.findPath( AssetGUID::fromName( source, ".mesh" ) ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "common/models/kaykit/knight/materials/mi_eyes.material" ), database.findPath( AssetGUID::fromName( source, "/materials/mi_eyes.material" ) ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "common/models/people/hero.mesh" ), moved.findPath( AssetGUID::fromName( source, ".mesh" ) ).value_or( string{} ) );
    SW_EXPECT_EQUAL( string( "common/models/kaykit/knight/mirrors.xml" ), database.findPath( mirror ).value_or( string{} ) );
    SW_EXPECT_FALSE( database.findGUID( "common/models/_shared/materials/00ff.material" ).has_value() );
}

/** @brief [AssetDatabaseTest] 도메인 리다이렉트 표의 정체성 대체 줄은 색인 층에 실리고, 틀린 줄은 결함이다 */
SW_TEST_CASE( AssetDatabaseTest, IdentityRedirectRowsJoinTheRegistryLayer )
{
    const AssetIdentityRules                 rules = AssetDatabaseTestUtil::makeRules();
    const AssetGUID                          from  = AssetGUID::create();
    const AssetGUID                          to    = AssetGUID::create();
    StringBuilder<constant::buffer::kMax256> table;
    table.appendFormat( "<Redirects><IdentityRedirect from=\"%#\" to=\"%#\"/></Redirects>", from.toString(), to.toString() );
    vector<AssetIdentityIssue> listIssue;
    const AssetRegistryLayer   layer =
        AssetDatabase::build( vector<AssetIdentityFile>{
                                  { "common/redirects.xml", AssetGUIDHeader::write( table.view(), AssetGUID::create() ) }
    },
                              rules, listIssue )
            .makeRegistryLayer( "test" );
    SW_EXPECT_TRUE( listIssue.empty() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, layer._listRedirect.size() );
    SW_EXPECT_TRUE( layer._listRedirect[0]._from == from && layer._listRedirect[0]._to == to );

    SW_EXPECT_LOG( "AssetRedirect.IdentityRowInvalid" );
    (void)AssetDatabase::build( vector<AssetIdentityFile>{
                                    { "game/x/redirects.xml", AssetGUIDHeader::write( "<Redirects><IdentityRedirect from=\"x\" to=\"y\"/></Redirects>", AssetGUID::create() ) }
    },
                                rules, listIssue );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::InvalidRedirect, "game/x/redirects.xml" ) );
}

/** @brief [AssetDatabaseTest] 디스크의 산출물 하나의 정체성(파일 감시 · 쿠킹)도 색인의 줄과 같다 */
SW_TEST_CASE( AssetDatabaseTest, OutputOnDiskReadsTheIdentityOfItsSource )
{
    const AssetIdentityRules rules      = AssetDatabaseTestUtil::makeRules();
    const AssetGUID          source     = AssetGUID::create();
    const AbsolutePath       sourceMeta = sw::test::makeScratchPath( "common/models_raw/rock.glb.meta" );
    const AbsolutePath       mesh       = sw::test::makeScratchPath( "common/models/rock.mesh" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( sourceMeta ) && FileUtil::writeTextFile( sourceMeta, AssetMeta::makeText( "common/models_raw/rock.glb", source ) ) );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( mesh ) && FileUtil::writeTextFile( mesh, "mesh" ) );
    const optional<AssetGUID> guid = rules.readIdentity( mesh, "common/models/rock.mesh" );
    SW_REQUIRE_TRUE( guid.has_value() );
    SW_EXPECT_TRUE( *guid == AssetGUID::fromName( source, ".mesh" ) );
    SW_EXPECT_FALSE( rules.readIdentity( mesh, "common/models/gone.mesh" ).has_value() );
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
    const AbsolutePath       prefabAbs  = sw::test::makeScratchPath( "copy.prefab.xml" );
    const AbsolutePath       soundAbs   = sw::test::makeScratchPath( "copy.ogg" );
    const AbsolutePath       newTextAbs = sw::test::makeScratchPath( "incoming.xml" );
    const AbsolutePath       newBytes   = sw::test::makeScratchPath( "incoming.ogg" );
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
    SW_EXPECT_EQUAL( string( "a/b/hit.ogg.meta" ), AssetMeta::makePath( "a/b/hit.ogg" ).str() );
    const AbsolutePath diskSource = sw::test::makeScratchPath( "a/b/hit.ogg" );
    SW_EXPECT_TRUE( AssetMeta::makePath( diskSource ) == diskSource.getParent().join( "hit.ogg.meta" ) ); // 디스크 판은 형식으로 곁 파일을 잇는다

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

/**
 * @brief [AssetMetaTest] 쓴 서술이 저장소의 정규형(`Scripts/lint/asset_identity.py` make_meta_text — 끝 줄바꿈 없음)과 바이트로 같다
 * @details 변이: 직렬화기의 끝 줄바꿈을 남기면 인스펙터가 예외를 지운 서술이 원본과 한 바이트 달라 editor.importsettings 의 왕복이 졌다.
 */
SW_TEST_CASE( AssetMetaTest, TextMatchesTheRepositoryForm )
{
    const optional<AssetGUID> guid = AssetGUID::tryParse( "6bf1c6b3381eb58dd5e456b5cb4f5b77" );
    SW_REQUIRE_TRUE( guid.has_value() );
    SW_EXPECT_EQUAL( string( "<AssetMeta _guid=\"6bf1c6b3381eb58dd5e456b5cb4f5b77\" _source=\"splash.png\" />" ), AssetMeta::makeText( "editor/textures_raw/splash.png", *guid ) );
}

/** @brief [AssetMetaTest] 원본의 자리는 곁의 파일 이름과 `store:<SHA-256>/<이름>` 두 꼴뿐이고(파이썬 self-check 와 같은 사례), 옮긴 서술은 내용 주소 · 임포트 예외를 지킨다 */
SW_TEST_CASE( AssetMetaTest, StoredSourceAddressIsOneOfTwoForms )
{
    using Util                                    = AssetDatabaseTestUtil;
    const string                       storedText = Util::makeStoredText( 'a', "/horse.glb" );
    const optional<AssetSourceAddress> stored     = AssetSourceAddress::tryParse( storedText );
    SW_REQUIRE_TRUE( stored.has_value() );
    SW_EXPECT_TRUE( stored->isStored() );
    SW_EXPECT_EQUAL( string( "horse.glb" ), stored->_name );
    SW_EXPECT_EQUAL( storedText, stored->toText() );
    const optional<AssetSourceAddress> beside = AssetSourceAddress::tryParse( "horse.glb" );
    SW_REQUIRE_TRUE( beside.has_value() );
    SW_EXPECT_FALSE( beside->isStored() );
    SW_EXPECT_EQUAL( string( "horse.glb" ), beside->toText() );
    SW_EXPECT_FALSE( AssetSourceAddress::tryParse( Util::makeStoredText( 'A', "/horse.glb" ) ).has_value() );
    SW_EXPECT_FALSE( AssetSourceAddress::tryParse( Util::makeStoredText( 'a', "/" ) ).has_value() );
    SW_EXPECT_FALSE( AssetSourceAddress::tryParse( Util::makeStoredText( 'a', "/a/horse.glb" ) ).has_value() );
    for ( const string_view wrong : { "store:0a/horse.glb", "a/horse.glb", "..", "C:horse.glb", "" } )
        SW_EXPECT_FALSE( AssetSourceAddress::tryParse( wrong ).has_value() );

    AssetMeta    meta;
    const string text = Util::makeMetaText( storedText, "<_listImportOverride><AssetImportOverride _key=\"generateMips\" _value=\"false\"/></_listImportOverride>" );
    SW_REQUIRE_TRUE( meta.parseXML( text, "horse.glb.meta" ).has_value() );
    meta.renameSource( "b/pony.glb" );
    AssetMeta moved;
    SW_REQUIRE_TRUE( moved.parseXML( meta.toXML(), "pony.glb.meta" ).has_value() );
    SW_EXPECT_EQUAL( Util::makeStoredText( 'a', "/pony.glb" ), moved._source );
    SW_EXPECT_EQUAL( size_t{ 1 }, moved._listImportOverride.size() );

    SW_EXPECT_LOG( "AssetMeta.InvalidSource" );
    AssetMeta wrongSource;
    SW_EXPECT_FALSE( wrongSource.parseXML( Util::makeMetaText( "a/horse.glb", {} ), "horse.glb.meta" ).has_value() );
}

/** @brief [AssetDatabaseTest] 저장소 밖 원본은 서술만으로 색인에 오르고(짝 잃은 서술이 아니다), 곁에 파일도 있으면 결함이다 */
SW_TEST_CASE( AssetDatabaseTest, StoredSourceIsIndexedWithoutAFile )
{
    using Util                      = AssetDatabaseTestUtil;
    const AssetIdentityRules  rules = Util::makeRules();
    vector<AssetIdentityFile> listFile;
    listFile.push_back( AssetIdentityFile{ "common/a/horse.glb.meta", Util::makeMetaText( Util::makeStoredText( 'a', "/horse.glb" ), {} ) } );
    vector<AssetIdentityIssue> listIssue;
    const AssetDatabase        database = AssetDatabase::build( listFile, rules, listIssue );
    SW_EXPECT_TRUE( listIssue.empty() );
    SW_EXPECT_TRUE( database.findGUID( "common/a/horse.glb" ).has_value() );

    listFile.push_back( AssetIdentityFile{ "common/a/horse.glb", {} } );
    const AssetDatabase both = AssetDatabase::build( listFile, rules, listIssue );
    SW_EXPECT_FALSE( both.findGUID( "common/a/horse.glb" ).has_value() );
    SW_EXPECT_TRUE( AssetDatabaseTestUtil::hasIssue( listIssue, AssetIdentityIssueKind::InvalidMeta, "common/a/horse.glb.meta" ) );
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

/** @brief [AssetDatabaseTest] 훑기 캐시는 크기 · 시각이 그대로인 파일을 열지 않고, 시각이 바뀐 파일 · 사라진 파일은 다시 본다 */
SW_TEST_CASE( AssetDatabaseTest, ScanCacheReopensOnlyChangedFiles )
{
    const AssetIdentityRules rules      = AssetDatabaseTestUtil::makeRules();
    const AbsolutePath       root       = sw::test::makeScratchPath( "content" );
    const AbsolutePath       cachePath  = sw::test::makeScratchPath( "scan.bin" );
    const AbsolutePath       prefabPath = root.join( "common/prefabs/bow.prefab.xml" );
    const AbsolutePath       gonePath   = root.join( "common/prefabs/gone.prefab.xml" );
    const AssetGUID          first      = AssetGUID::create();
    const AssetGUID          second     = AssetGUID::create();
    const AssetGUID          gone       = AssetGUID::create();
    const int64              stable     = FileUtil::getCurrentFileWriteTime() - 3600 * FileUtil::kFileTimeTicksPerSecond;
    const auto               write      = []( const AbsolutePath& path, const AssetGUID& guid, int64 ticks )
    {
        SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, AssetDatabaseTestUtil::makeHeaderText( guid ) ) );
        SW_REQUIRE_TRUE( FileUtil::setFileWriteTime( path, ticks ) );
    };
    const auto scan = [&rules, &root, &cachePath]()
    {
        AssetScanCache cache{ AssetDatabase::kScanPayloadVersion };
        SW_EXPECT_TRUE( cache.load( cachePath ).has_value() );
        vector<AssetIdentityIssue> listIssue;
        AssetDatabase              database = AssetDatabase::scan( root, rules, listIssue, &cache );
        SW_EXPECT_TRUE( listIssue.empty() );
        SW_EXPECT_TRUE( cache.save( cachePath ).has_value() );
        return database;
    };
    write( prefabPath, first, stable );
    write( gonePath, gone, stable );
    SW_EXPECT_EQUAL( size_t{ 2 }, scan().getCount() );

    write( prefabPath, second, stable ); // 크기 · 시각이 같다 — 열지 않았으니 앞 정체성이 보인다
    SW_REQUIRE_TRUE( FileUtil::removeFile( gonePath ) );
    const AssetDatabase cached = scan();
    SW_EXPECT_EQUAL( size_t{ 1 }, cached.getCount() );
    SW_EXPECT_TRUE( cached.findPath( first ).has_value() );

    write( prefabPath, second, stable + FileUtil::kFileTimeTicksPerSecond );
    SW_EXPECT_TRUE( scan().findPath( second ).has_value() );
}

/** @brief [AssetScanCacheTest] 막 쓴 파일은 캐시에 두지 않고, 다른 판은 빈 캐시, 깨진 파일은 오류다 */
SW_TEST_CASE( AssetScanCacheTest, KeepsStableRowsOfItsOwnVersion )
{
    const AbsolutePath path = sw::test::makeScratchPath( "cache.bin" );
    const FileStamp    stable{ 4u, static_cast<uint64>( FileUtil::getCurrentFileWriteTime() - 3600 * FileUtil::kFileTimeTicksPerSecond ) };
    const FileStamp    fresh{ 4u, static_cast<uint64>( FileUtil::getCurrentFileWriteTime() ) };
    {
        AssetScanCache cache{ 1u };
        cache.record( "a.xml", stable, { uint8{ 7 } } );
        cache.record( "b.xml", fresh, { uint8{ 8 } } );
        SW_EXPECT_TRUE( cache.find( "b.xml", fresh ) == nullptr );
        SW_REQUIRE_TRUE( cache.save( path ).has_value() );
    }
    AssetScanCache sameVersion{ 1u };
    SW_REQUIRE_TRUE( sameVersion.load( path ).has_value() );
    const vector<uint8>* pPayload = sameVersion.find( "a.xml", stable );
    SW_REQUIRE_TRUE( pPayload != nullptr );
    SW_EXPECT_TRUE( *pPayload == ( vector<uint8>{ uint8{ 7 } } ) );
    SW_EXPECT_TRUE( sameVersion.find( "a.xml", fresh ) == nullptr );

    AssetScanCache otherVersion{ 2u };
    SW_EXPECT_TRUE( otherVersion.load( path ).has_value() );
    SW_EXPECT_TRUE( otherVersion.find( "a.xml", stable ) == nullptr );

    vector<uint8> bytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, bytes ) );
    bytes.back() ^= uint8{ 0xFF };
    SW_REQUIRE_TRUE( FileUtil::writeFile( path, bytes.data(), bytes.size() ) );
    SW_EXPECT_FALSE( sameVersion.load( path ).has_value() );
    SW_EXPECT_TRUE( sameVersion.find( "a.xml", stable ) == nullptr );
}
