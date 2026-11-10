#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModAllowList.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModManifest.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModResolver.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSetDigest.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

// 모드 매니페스트 · 순서 · 구성 해시 · 허용 목록 — 바깥 입력의 거절과 결정성. 파일을 읽지 않는 순수 판단이다(마운트는 TestModSession).

using namespace sw;

namespace
{
    struct TestModManifestInternal
    {
        static ModSupportSettings makeSettings()
        {
            ModSupportSettings settings;
            settings._contentVersion        = 2;
            settings._maxModCount           = 4;
            settings._maxManifestBytes      = 4096;
            settings._maxPackBytes          = 1u << 20;
            settings._maxEntryCount         = 16;
            settings._maxEntryBytes         = 4096;
            settings._listOverridablePrefix = { "game/test/data/" };
            return settings;
        }

        static ModManifest makeManifest( string_view id, int32 priority )
        {
            ModManifest manifest;
            manifest._id                = string{ id };
            manifest._version           = ModVersion{ 1, 0, 0 };
            manifest._minContentVersion = 1;
            manifest._maxContentVersion = 3;
            manifest._priority          = priority;
            return manifest;
        }

        static void addDependency( ModManifest& manifest, string_view id, bool bOptional )
        {
            ModDependency dependency;
            dependency._id         = string{ id };
            dependency._minVersion = ModVersion{ 1, 0, 0 };
            dependency._bOptional  = bOptional;
            manifest._listDependency.push_back( dependency );
        }

        /** @brief 순서를 id 글(`a,b,c`)로 돌려줍니다. 거절이면 `<rejected>` 입니다. */
        static string resolveIDs( const vector<ModManifest>& listManifest )
        {
            vector<size_t> listOrder;
            if ( ModResolver::resolveOrder( listManifest, makeSettings(), listOrder ) == false )
                return "<rejected>";
            string text;
            for ( const size_t index : listOrder )
            {
                if ( text.empty() == false )
                    text += ',';
                text += listManifest[index]._id;
            }
            return text;
        }

        static bool parseOverride( string_view path )
        {
            ModManifest  manifest;
            const string text = R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Override path=")" + string{ path } + R"("/></Mod>)";
            return ModManifest::parseXMLText( text, "test", manifest );
        }
    };
} // namespace

SW_TEST_CASE( ModManifestTest, ReadsEveryField )
{
    ModManifest manifest;
    SW_REQUIRE_TRUE( ModManifest::parseXMLText( R"(<Mod id="lantern_2" version="1.2.3" minContentVersion="1" maxContentVersion="4" priority="-5">
        <Dependency id="base_pack" minVersion="0.9.0" optional="false"/>
        <Dependency id="extras" minVersion="1.0.0" optional="true"/>
        <Conflict id="dark_mode"/>
        <Override path="game/test/data/welcome.xml"/>
    </Mod>)",
                                                "test", manifest ) );
    SW_EXPECT_EQUAL( string( "lantern_2" ), manifest._id );
    SW_EXPECT_EQUAL( string( "1.2.3" ), manifest._version.toString() );
    SW_EXPECT_EQUAL( 4u, manifest._maxContentVersion );
    SW_EXPECT_EQUAL( -5, manifest._priority );
    SW_REQUIRE_EQUAL( size_t{ 2 }, manifest._listDependency.size() );
    SW_EXPECT_TRUE( manifest._listDependency[1]._bOptional );
    SW_EXPECT_EQUAL( string( "dark_mode" ), manifest._listConflict[0] );
    SW_EXPECT_EQUAL( string( "game/test/data/welcome.xml" ), manifest._listOverride[0] );
    SW_EXPECT_EQUAL( string( "mods/lantern_2/" ), ModManifest::makeContentPrefix( manifest._id ) );
}

// 층은 덮어쓰기와 같은 경로 규칙이고 같은 정책을 받는다 — 한 리소스를 덮어쓰면서 층을 얹지 않고, 층 뿌리는 모드 도메인 안이다.
SW_TEST_CASE( ModManifestTest, ReadsLayersUnderTheOverrideRules )
{
    ModManifest manifest;
    SW_REQUIRE_TRUE( ModManifest::parseXMLText( R"(<Mod id="tide" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0">
        <Layer path="game/test/data/quests.xml"/>
        <Layer path="game/test/data/monsters.xml"/>
    </Mod>)",
                                                "test", manifest ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, manifest._listLayer.size() );
    SW_EXPECT_EQUAL( string( "game/test/data/monsters.xml" ), manifest._listLayer[1] );
    SW_EXPECT_TRUE( manifest._listOverride.empty() );
    SW_EXPECT_EQUAL( string( "mods/tide/layer" ), ModManifest::makeLayerRoot( manifest._id ) );

    SW_EXPECT_LOG( "ModManifest.DuplicateOverride" ); // 같은 리소스를 덮어쓰면서 층을 얹는다
    SW_EXPECT_FALSE( ModManifest::parseXMLText(
        R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Override path="game/test/data/q.xml"/><Layer path="game/test/data/q.xml"/></Mod>)", "test",
        manifest ) );
    SW_EXPECT_LOG( "ModManifest.OverridesModContent" ); // 다른 모드의 콘텐츠에는 얹지 않는다
    SW_EXPECT_FALSE(
        ModManifest::parseXMLText( R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Layer path="mods/b/data/q.xml"/></Mod>)", "test", manifest ) );

    // 정책 밖의 층은 거절, 두 모드가 같은 표에 얹는 것은 충돌이 아니다.
    vector<ModManifest> listManifest{ TestModManifestInternal::makeManifest( "outside", 0 ) };
    listManifest[0]._listLayer.push_back( "game/test/database.xml" );
    SW_EXPECT_LOG( "ModResolver.LayerNotAllowed" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), TestModManifestInternal::resolveIDs( listManifest ) );
    listManifest = { TestModManifestInternal::makeManifest( "first", 0 ), TestModManifestInternal::makeManifest( "second", 1 ) };
    listManifest[0]._listLayer.push_back( "game/test/data/quests.xml" );
    listManifest[1]._listLayer.push_back( "game/test/data/quests.xml" );
    SW_EXPECT_EQUAL( string( "first,second" ), TestModManifestInternal::resolveIDs( listManifest ) );
}

// id 는 폴더 이름과 리소스 경로가 된다 — 구분자 · `..` · 대문자 · 드라이브는 형식에서 막는다.
SW_TEST_CASE( ModManifestTest, IDRuleBlocksPathEscape )
{
    SW_EXPECT_TRUE( ModManifest::isValidID( "demo_lantern" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( ".." ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "../evil" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "a/b" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "a\\b" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "C:" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( "Lantern" ) );
    SW_EXPECT_FALSE( ModManifest::isValidID( string( ModManifest::kMaxIDLength + 1, 'a' ) ) );
}

SW_TEST_CASE( ModManifestTest, RejectsMalformedManifests )
{
    ModManifest manifest;
    SW_EXPECT_LOG( "Xml.ParseError" ); // 문서 파서가 먼저 알린다
    SW_EXPECT_LOG( "ModManifest.NotXML" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText( "<Mod", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.WrongRoot" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Plugin id="a"/>)", "test", manifest ) );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" ); // 속성은 리플렉션 필수 칸이다(ModManifestRootDef)
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1"/>)", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.BadID" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Mod id="../a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"/>)", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.BadVersion" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Mod id="a" version="1.0" minContentVersion="1" maxContentVersion="1" priority="0"/>)", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.BadContentRange" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Mod id="a" version="1.0.0" minContentVersion="3" maxContentVersion="1" priority="0"/>)", "test", manifest ) );
    SW_EXPECT_LOG( "Xml.UnknownElement" ); // 스크립트 · 네이티브 코드를 가리키는 원소는 형식에 없다
    SW_EXPECT_FALSE( ModManifest::parseXMLText( R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Script file="x.lua"/></Mod>)", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.BadDependency" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText(
        R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Dependency id="a" minVersion="1.0.0" optional="false"/></Mod>)", "test", manifest ) );
    SW_EXPECT_LOG( "ModManifest.DependsOnConflict" );
    SW_EXPECT_FALSE( ModManifest::parseXMLText(
        R"(<Mod id="a" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0"><Dependency id="b" minVersion="1.0.0" optional="false"/><Conflict id="b"/></Mod>)", "test",
        manifest ) );
}

// 덮어쓰기는 정규형 기본 리소스 id 만 — 경로 탈출 · 절대 경로 · 대문자 · 다른 모드의 리소스는 거절한다.
SW_TEST_CASE( ModManifestTest, RejectsOverridesOutsideBaseResources )
{
    using Internal = TestModManifestInternal;
    SW_EXPECT_LOG( "ModManifest.BadOverride" );
    SW_EXPECT_FALSE( Internal::parseOverride( "game/test/../../secret.xml" ) );
    SW_EXPECT_FALSE( Internal::parseOverride( "C:/Windows/system.ini" ) );
    SW_EXPECT_FALSE( Internal::parseOverride( "Game/Test/Data/Welcome.xml" ) );
    SW_EXPECT_FALSE( Internal::parseOverride( "welcome.xml" ) );
    SW_EXPECT_LOG( "ModManifest.OverridesModContent" );
    SW_EXPECT_FALSE( Internal::parseOverride( "mods/other/data/x.xml" ) );
    SW_EXPECT_TRUE( Internal::parseOverride( "game/test/data/welcome.xml" ) );
}

// 같은 집합이면 넣은 순서와 상관없이 같은 순서 — 의존이 아래, 그다음 우선순위 · id.
SW_TEST_CASE( ModResolverTest, OrderIsDeterministic )
{
    using Internal  = TestModManifestInternal;
    ModManifest top = Internal::makeManifest( "top", -10 );
    Internal::addDependency( top, "base", false );
    Internal::addDependency( top, "absent", true ); // 켜지 않은 선택 의존은 무시한다
    const ModManifest base  = Internal::makeManifest( "base", 50 );
    const ModManifest alpha = Internal::makeManifest( "alpha", 0 );
    const ModManifest beta  = Internal::makeManifest( "beta", 0 );

    SW_EXPECT_EQUAL( string( "alpha,beta,base,top" ), Internal::resolveIDs( { top, base, alpha, beta } ) );
    SW_EXPECT_EQUAL( string( "alpha,beta,base,top" ), Internal::resolveIDs( { beta, alpha, top, base } ) );
}

SW_TEST_CASE( ModResolverTest, RejectsBrokenSets )
{
    using Internal        = TestModManifestInternal;
    ModManifest needsBase = Internal::makeManifest( "needs_base", 0 );
    Internal::addDependency( needsBase, "base", false );
    SW_EXPECT_LOG( "ModResolver.MissingDependency" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { needsBase } ) );

    needsBase._listDependency[0]._minVersion = ModVersion{ 2, 0, 0 };
    SW_EXPECT_LOG( "ModResolver.DependencyTooOld" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { needsBase, Internal::makeManifest( "base", 0 ) } ) );

    ModManifest first  = Internal::makeManifest( "first", 0 );
    ModManifest second = Internal::makeManifest( "second", 0 );
    Internal::addDependency( first, "second", false );
    Internal::addDependency( second, "first", false );
    SW_EXPECT_LOG( "ModResolver.DependencyCycle" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { first, second } ) );

    ModManifest light = Internal::makeManifest( "light", 0 );
    light._listConflict.push_back( "dark" );
    SW_EXPECT_LOG( "ModResolver.Conflict" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { light, Internal::makeManifest( "dark", 0 ) } ) );

    ModManifest future        = Internal::makeManifest( "future", 0 );
    future._minContentVersion = 3;
    SW_EXPECT_LOG( "ModResolver.ContentVersion" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { future } ) );

    ModManifest engineSkin = Internal::makeManifest( "engine_skin", 0 );
    engineSkin._listOverride.push_back( "engine/shaders/core.hlsl" );
    SW_EXPECT_LOG( "ModResolver.OverrideNotAllowed" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { engineSkin } ) );

    SW_EXPECT_LOG( "ModResolver.DuplicateID" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { Internal::makeManifest( "twin", 0 ), Internal::makeManifest( "twin", 1 ) } ) );
    SW_EXPECT_LOG( "ModResolver.TooManyMods" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { Internal::makeManifest( "m1", 0 ), Internal::makeManifest( "m2", 0 ), Internal::makeManifest( "m3", 0 ),
                                                                     Internal::makeManifest( "m4", 0 ), Internal::makeManifest( "m5", 0 ) } ) );
}

// 두 모드가 같은 기본 리소스를 덮어쓰면 우선순위 숫자의 우연으로 정하지 않는다 — 뒤 모드가 앞 모드에 의존할 때만 받는다.
SW_TEST_CASE( ModResolverTest, OverrideCollisionNeedsADependency )
{
    using Internal  = TestModManifestInternal;
    ModManifest red = Internal::makeManifest( "red", 0 );
    red._listOverride.push_back( "game/test/data/welcome.xml" );
    ModManifest blue = Internal::makeManifest( "blue", 1 );
    blue._listOverride.push_back( "game/test/data/welcome.xml" );
    SW_EXPECT_LOG( "ModResolver.OverrideCollision" );
    SW_EXPECT_EQUAL( string( "<rejected>" ), Internal::resolveIDs( { red, blue } ) );

    Internal::addDependency( blue, "red", false );
    SW_EXPECT_EQUAL( string( "red,blue" ), Internal::resolveIDs( { blue, red } ) );
}

SW_TEST_CASE( ModSetDigestTest, SameSetSameHashAndEmptyChangesNothing )
{
    const ModSetDigest empty = ModSetDigest::make( {} );
    SW_EXPECT_EQUAL( uint64{ 0 }, empty._configHash );
    SW_EXPECT_EQUAL( 0x1234u, empty.foldWireVersion( 0x1234u ) ); // 모드 없는 실행의 와이어 버전은 그대로다

    const ModSetDigest one   = ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 0xABCDu }
    } );
    const ModSetDigest again = ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 0xABCDu }
    } );
    const ModSetDigest other = ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 0xABCEu }
    } );
    SW_EXPECT_EQUAL( one._configHash, again._configHash );
    SW_EXPECT_TRUE( one._configHash != other._configHash ); // 같은 판의 다른 내용
    SW_EXPECT_TRUE( one.foldWireVersion( 0x1234u ) != 0x1234u );
    SW_EXPECT_TRUE( one.foldWireVersion( 0x1234u ) != other.foldWireVersion( 0x1234u ) ); // 다른 구성의 클라이언트는 핸드셰이크에서 갈린다
    SW_EXPECT_TRUE( one.describeDifference( again ).empty() );
    SW_EXPECT_EQUAL( string( "mod 'lantern' 1.0.0 has different content here" ), one.describeDifference( other ) );
    SW_EXPECT_EQUAL( string( "mod 'lantern' 1.0.0 is only here" ), one.describeDifference( empty ) );

    ModSetDigest parsed;
    SW_EXPECT_EQUAL( string( "lantern@1.0.0#000000000000abcd" ), one.toText() );
    SW_REQUIRE_TRUE( ModSetDigest::parseText( one.toText(), parsed ) );
    SW_EXPECT_EQUAL( one._configHash, parsed._configHash );
    SW_EXPECT_LOG( "ModSetDigest.BadText" );
    SW_EXPECT_FALSE( ModSetDigest::parseText( "../x@1.0.0#000000000000abcd", parsed ) );
}

// 제거된 모드가 든 세이브는 읽지 않는다. 새로 켠 모드는 받는다.
SW_TEST_CASE( ModSetDigestTest, SaveWithRemovedModIsRejected )
{
    const ModSetDigest saved   = ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 1u }
    } );
    const ModSetDigest none    = ModSetDigest::make( {} );
    const ModSetDigest more    = ModSetDigest::make( {
        ModDigestEntry{"lantern", ModVersion{ 1, 0, 0 }, 2u},
        ModDigestEntry{  "extra", ModVersion{ 1, 0, 0 }, 3u}
    } );
    const ModSetDigest updated = ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 2, 0, 0 }, 1u }
    } );
    SW_EXPECT_TRUE( none.checkSaveCompatible( none ) ); // 모드 없는 세이브 · 실행은 그대로다
    SW_EXPECT_TRUE( more.checkSaveCompatible( saved ) );
    SW_EXPECT_LOG( "ModSetDigest.SaveNeedsMod" );
    SW_EXPECT_FALSE( none.checkSaveCompatible( saved ) );
    SW_EXPECT_LOG( "ModSetDigest.SaveModVersion" );
    SW_EXPECT_FALSE( updated.checkSaveCompatible( saved ) );
}

SW_TEST_CASE( ModAllowListTest, AllowsOnlyListedContent )
{
    ModAllowList list;
    SW_REQUIRE_TRUE( ModAllowList::parseXMLText( R"(<ModAllowList><Mod id="lantern" version="1.0.0" contentHash="000000000000abcd"/></ModAllowList>)", "test", list ) );
    SW_EXPECT_TRUE( list.checkDigest( ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 0xABCDu }
    } ) ) );
    SW_EXPECT_TRUE( list.checkDigest( ModSetDigest::make( {} ) ) );
    SW_EXPECT_LOG( "ModAllowList.Mismatch" );
    SW_EXPECT_FALSE( list.checkDigest( ModSetDigest::make( {
        ModDigestEntry{ "lantern", ModVersion{ 1, 0, 0 }, 0xABCEu }
    } ) ) );
    SW_EXPECT_LOG( "ModAllowList.NotAllowed" );
    SW_EXPECT_FALSE( list.checkDigest( ModSetDigest::make( {
        ModDigestEntry{ "cheat", ModVersion{ 1, 0, 0 }, 0xABCDu }
    } ) ) );
    SW_EXPECT_LOG( "ModAllowList.BadEntry" );
    SW_EXPECT_FALSE( ModAllowList::parseXMLText( R"(<ModAllowList><Mod id="lantern" version="1.0.0" contentHash="abcd"/></ModAllowList>)", "test", list ) );
    SW_EXPECT_LOG( "ModAllowList.DuplicateID" );
    SW_EXPECT_FALSE( ModAllowList::parseXMLText(
        R"(<ModAllowList><Mod id="a" version="1.0.0" contentHash="000000000000abcd"/><Mod id="a" version="1.0.1" contentHash="000000000000abcd"/></ModAllowList>)", "test", list ) );
}

SW_TEST_CASE( ModSupportSettingsTest, EveryLimitIsRequired )
{
    ModSupportSettings settings;
    SW_REQUIRE_TRUE( ModSupportSettings::parseXMLText(
        R"(<ModSupportSettings contentVersion="1" maxModCount="2" maxManifestBytes="100" maxPackBytes="5000000000" maxEntryCount="3" maxEntryBytes="4"><Overridable prefix="game/test/data/"/></ModSupportSettings>)",
        "test", settings ) );
    SW_EXPECT_EQUAL( uint64{ 5000000000ull }, settings._maxPackBytes );
    SW_EXPECT_TRUE( settings.isOverridable( "game/test/data/welcome.xml" ) );
    SW_EXPECT_FALSE( settings.isOverridable( "game/test/database.xml" ) );
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" ); // 한계는 리플렉션 필수 칸이다(XMLFields)
    SW_EXPECT_FALSE( ModSupportSettings::parseXMLText( R"(<ModSupportSettings contentVersion="1"/>)", "test", settings ) );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" ); // 0 인 한계는 Min = 1 밖이다
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_FALSE( ModSupportSettings::parseXMLText(
        R"(<ModSupportSettings contentVersion="1" maxModCount="0" maxManifestBytes="1" maxPackBytes="1" maxEntryCount="1" maxEntryBytes="1"/>)", "test", settings ) );
    SW_EXPECT_LOG( "ModSupportSettings.BadPrefix" );
    SW_EXPECT_FALSE( ModSupportSettings::parseXMLText(
        R"(<ModSupportSettings contentVersion="1" maxModCount="1" maxManifestBytes="1" maxPackBytes="1" maxEntryCount="1" maxEntryBytes="1"><Overridable prefix="game/test/data"/></ModSupportSettings>)",
        "test", settings ) );
}
