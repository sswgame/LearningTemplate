// 형식별 읽기 입구가 리소스 id 를 합성에 넘기는가 — 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Facial/LiveLinkFace.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/Visual/MeshLODSettings.h"

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    /** @brief 케이스 동안 태그 · 층을 바꾸고 소멸자에서 되돌립니다. */
    class XEntranceScope
    {
    public:
        XEntranceScope()
            : _previous{ engine::getSubsystem<ResourceTags>().exchange( makeTags() ) }
            , _listPreviousLayer{ engine::getSubsystem<ResourceComposition>().getLayers() }
        {
        }
        ~XEntranceScope()
        {
            (void)engine::getSubsystem<ResourceTags>().exchange( _previous );
            (void)engine::getSubsystem<ResourceComposition>().setLayers( _listPreviousLayer );
            vector<string> listPending;
            engine::getSubsystem<ResourceComposition>().takePendingReload( listPending ); // 케이스가 만든 다시 읽기 목록을 다음 케이스에 남기지 않는다
        }
        XEntranceScope( const XEntranceScope& )            = delete;
        XEntranceScope& operator=( const XEntranceScope& ) = delete;

    private:
        static ResourceTagSet makeTags()
        {
            ResourceTagSet tags;
            tags.add( "region.kr" );
            return tags;
        }

        ResourceTagSet _previous;
        vector<string> _listPreviousLayer;
    };

    struct XEntranceTestUtil
    {
        /** 거짓인 로드 축 조건입니다 — 값은 바꾸지 않고 "이 합성은 품질에 기댄다" 는 기록만 남긴다. */
        static constexpr const utf8* kWhen = R"(<_when _if="region.us" _neverRead="1" />)";

        static bool isRecorded( const AssetID& id )
        {
            vector<ResourceDigestEntry> listEntry;
            engine::getSubsystem<ResourceComposition>().collectDigest( listEntry );
            return VectorUtil::containsIf( listEntry, [&id]( const ResourceDigestEntry& entry )
            { return entry._assetID == id.str(); } );
        }

        static void write( const ScopedScratchDomain& domain, const utf8* pFile, string_view text )
        {
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( pFile ), text ) );
        }

        static string makeMaterial( const utf8* pName, const string& inner )
        {
            StringBuilder<constant::buffer::kMax512> builder;
            builder.appendFormat( R"(<MaterialDef formatVersion="0" name="%#" shaderPath="engine/shaders/forward_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">%#</MaterialDef>)", pName, inner );
            return string( builder.view() );
        }

        static string makeScene( const utf8* pName, const string& inner )
        {
            StringBuilder<constant::buffer::kMax512> builder;
            builder.appendFormat( R"(<Scene formatVersion="2" name="%#"><SceneSettings _bStreaming="false" _unloadMargin="0" _maxCellLoadsPerFrame="0" _addToWorldBudgetMicroseconds="0"><_cellGrid _cellSize="0" _loadingRadius="0" /></SceneSettings>%#</Scene>)", pName, inner );
            return string( builder.view() );
        }
    };
    using Util = XEntranceTestUtil;
} // namespace

/** @brief [ResourceEntranceTest] 손 형식 · 리플렉션 형식 · 합치기 함수 없는 일반 형식이 리소스 id 로 읽으면 로드 축 조건에 기댄 합성이 기록된다(접속 대조 · 다시 읽기의 입력) */
SW_TEST_CASE( ResourceEntranceTest, ReadingByIDRecordsTheComposition )
{
    const ScopedScratchDomain domain;
    const XEntranceScope      scope;

    StringBuilder<constant::buffer::kMax512> retargetXML;
    retargetXML.appendFormat( R"(<RetargetProfile><root source="root" target="root" /><pelvis source="pelvis" target="hips" /><chains><item name="LeftArm"><source><item>a</item></source><target><item>b</item></target></item></chains>%#</RetargetProfile>)", Util::kWhen );
    Util::write( domain, "a.retarget.xml", retargetXML.view() );
    const AssetID retargetID = ScopedScratchDomain::makeID( "a.retarget.xml" );
    SW_REQUIRE_TRUE( SharedAssetCache<RetargetProfile>::acquire( retargetID.str() ).snapshot() != nullptr );
    SW_EXPECT_TRUE( Util::isRecorded( retargetID ) );

    StringBuilder<constant::buffer::kMax512> rigXML;
    rigXML.appendFormat( R"(<RigAsset><targets><item name="Grip" bone="hand.r" /></targets><nodes><item type="Position" name="Follow" bone="weapon" target="Grip" /></nodes>%#</RigAsset>)", Util::kWhen );
    Util::write( domain, "a.rig.xml", rigXML.view() );
    const AssetID rigID = ScopedScratchDomain::makeID( "a.rig.xml" );
    SW_REQUIRE_TRUE( SharedAssetCache<RigAsset>::acquire( rigID.str() ).snapshot() != nullptr );
    SW_EXPECT_TRUE( Util::isRecorded( rigID ) );

    StringBuilder<constant::buffer::kMax256> physicsXML;
    physicsXML.appendFormat( "<PhysicsAsset>%#</PhysicsAsset>", Util::kWhen );
    Util::write( domain, "a.physics.xml", physicsXML.view() );
    const AssetID physicsID = ScopedScratchDomain::makeID( "a.physics.xml" );
    SW_REQUIRE_TRUE( SharedAssetCache<PhysicsAsset>::acquire( physicsID.str() ).snapshot() != nullptr );
    SW_EXPECT_TRUE( Util::isRecorded( physicsID ) );

    Util::write( domain, "a.material", Util::makeMaterial( "A", Util::kWhen ) );
    const AssetID              materialID = ScopedScratchDomain::makeID( "a.material" );
    const shared_ptr<Material> material   = Material::create();
    SW_REQUIRE_TRUE( material->loadFromAsset( materialID ) );
    SW_EXPECT_TRUE( Util::isRecorded( materialID ) );

    Util::write( domain, "a.scene.xml", Util::makeScene( "A", Util::kWhen ) );
    const AssetID sceneID = ScopedScratchDomain::makeID( "a.scene.xml" );
    SceneDocument scene{};
    SW_REQUIRE_TRUE( scene.loadXML( sceneID ) );
    SW_EXPECT_TRUE( Util::isRecorded( sceneID ) );
}

/**
 * @brief [ResourceEntranceTest] 설정 문서 길(`ConfigManager::readComposedXMLDocument`)과 루트 찾기 길(`XMLReadUtil::parseResourceRoot`)로 읽는 형식도 id 로 읽으면 기록된다
 * @details 예전에는 이 형식들의 `loadFromAsset` 이 글을 읽은 뒤 id 없이 글 입구로 넘겨 층 · 다시 읽기 · 접속 대조를 조용히 받지 못했다(린트 `common-shapes` 의 assetEntrance 가 그 모양을 막는다).
 */
SW_TEST_CASE( ResourceEntranceTest, SettingsAndRootFormatsRecordTheComposition )
{
    const ScopedScratchDomain domain;
    const XEntranceScope      scope;

    StringBuilder<constant::buffer::kMax256> meshLODText;
    meshLODText.appendFormat( R"(<MeshLODSettings _refineHysteresis="0.1" _skinnedBoundsScale="1.2">%#</MeshLODSettings>)", Util::kWhen );
    Util::write( domain, "a.meshlod.xml", meshLODText.view() );
    const AssetID   meshLODID = ScopedScratchDomain::makeID( "a.meshlod.xml" );
    MeshLODSettings meshLOD;
    SW_REQUIRE_TRUE( meshLOD.loadFromAsset( meshLODID ) );
    SW_EXPECT_TRUE( Util::isRecorded( meshLODID ) );

    StringBuilder<constant::buffer::kMax256> liveLinkXML;
    liveLinkXML.appendFormat( R"(<LiveLinkFaceLayout _packetVersion="6"><_listChannel><item>JawOpen</item></_listChannel>%#</LiveLinkFaceLayout>)", Util::kWhen );
    Util::write( domain, "a.livelinkface.xml", liveLinkXML.view() );
    const AssetID      layoutID = ScopedScratchDomain::makeID( "a.livelinkface.xml" );
    LiveLinkFaceLayout layout;
    SW_REQUIRE_TRUE( layout.loadFromAsset( layoutID ) );
    SW_EXPECT_TRUE( Util::isRecorded( layoutID ) );

    StringBuilder<constant::buffer::kMax256> visemeXML;
    visemeXML.appendFormat( R"(<VisemeTrack _frameRate="30"><_listViseme><item>AA</item></_listViseme><_listFrame><Frame>0.5</Frame></_listFrame>%#</VisemeTrack>)", Util::kWhen );
    Util::write( domain, "a.visemes.xml", visemeXML.view() );
    const AssetID trackID = ScopedScratchDomain::makeID( "a.visemes.xml" );
    VisemeTrack   track;
    SW_REQUIRE_TRUE( track.loadFromAsset( trackID ) );
    SW_EXPECT_EQUAL( 1u, track.getFrameCount() );
    SW_EXPECT_TRUE( Util::isRecorded( trackID ) );
}

/**
 * @brief [ResourceEntranceTest] 이름이 `loadFromAsset` 이 아닌 입구(타일맵 `load`)도 id 로 읽으면 기록된다
 * @details 예전에는 타일맵이 글을 읽은 뒤 글 입구(`loadFromXML`)로 넘겨 층 · 다시 읽기 · 접속 대조를 조용히 받지 못했다. 린트 assetEntrance 가 이제 `load…` 이름을 모두 본다.
 */
SW_TEST_CASE( ResourceEntranceTest, TileMapLoadRecordsTheComposition )
{
    const ScopedScratchDomain                domain;
    const XEntranceScope                     scope;
    StringBuilder<constant::buffer::kMax256> tileMapXML;
    tileMapXML.appendFormat( R"(<TileMap width="2" height="1">%#</TileMap>)", Util::kWhen );
    Util::write( domain, "a.tilemap.xml", tileMapXML.view() );
    const AssetID  tileMapID = ScopedScratchDomain::makeID( "a.tilemap.xml" );
    TileMapXMLData tileMap;
    SW_REQUIRE_TRUE( tileMap.load( tileMapID ) );
    SW_EXPECT_EQUAL( 2, tileMap._width );
    SW_EXPECT_TRUE( Util::isRecorded( tileMapID ) );
}

/** @brief [ResourceEntranceTest] 글 입구는 id 가 없어 기록을 남기지 않는다 — 같은 글을 id 로 읽은 것과 갈린다 */
SW_TEST_CASE( ResourceEntranceTest, ReadingTextRecordsNothing )
{
    const XEntranceScope                     scope;
    PhysicsAsset                             physics;
    StringBuilder<constant::buffer::kMax256> xml;
    xml.appendFormat( "<PhysicsAsset>%#</PhysicsAsset>", Util::kWhen );
    SW_REQUIRE_TRUE( physics.loadFromXMLText( xml.view(), "testproject/text-only.physics.xml" ) );
    SW_EXPECT_FALSE( Util::isRecorded( AssetID{ "testproject/text-only.physics.xml" } ) );
}

/** @brief [ResourceEntranceTest] 손 형식의 층은 그 형식의 규칙으로 얹는다 — 머티리얼은 범용 문서 입구로 읽어 층이 규칙을 못 찾고(`ResourceLayer.NoRule`) 멈췄다 */
SW_TEST_CASE( ResourceEntranceTest, LayerOverHandFormatUsesItsRule )
{
    const ScopedScratchDomain domain;
    const XEntranceScope      scope;
    (void)domain.makeDiskFolder( "layers/night/testproject" ).str();

    Util::write( domain, "b.material", Util::makeMaterial( "Base", "" ) );
    Util::write( domain, "layers/night/testproject/b.material", R"(<MaterialDef name="FromLayer" />)" );
    Util::write( domain, "b.retarget.xml", R"(<RetargetProfile><root source="root" target="root" /><pelvis source="pelvis" target="hips" /><chains><item name="LeftArm"><source><item>a</item></source><target><item>b</item></target></item></chains></RetargetProfile>)" );
    Util::write( domain, "layers/night/testproject/b.retarget.xml", R"(<RetargetProfile><pelvis target="hips_layer" /></RetargetProfile>)" );
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceComposition>().setLayers( { ScopedScratchDomain::makeID( "layers/night" ).str() } ) );

    const shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromAsset( ScopedScratchDomain::makeID( "b.material" ) ) );
    SW_EXPECT_EQUAL( string( "FromLayer" ), material->getName() );

    const shared_ptr<const RetargetProfile> profile = SharedAssetCache<RetargetProfile>::acquire( ScopedScratchDomain::makeID( "b.retarget.xml" ).view() ).snapshot();
    SW_REQUIRE_TRUE( profile != nullptr );
    SW_EXPECT_TRUE( profile->toXML().find( "hips_layer" ) != string::npos );
    SW_EXPECT_TRUE( Util::isRecorded( ScopedScratchDomain::makeID( "b.retarget.xml" ) ) );
}

/** @brief [ResourceEntranceTest] 씬 상속을 리소스 id 로 읽어도 저장이 쓰는 부모 id 는 남는다(펼친 글에는 `_base` 가 없다) */
SW_TEST_CASE( ResourceEntranceTest, SceneKeepsItsBaseIDThroughTheRule )
{
    const ScopedScratchDomain domain;
    const XEntranceScope      scope;
    Util::write( domain, "base.scene.xml", Util::makeScene( "Base", "<entities />" ) );
    const string                             baseID = ScopedScratchDomain::makeID( "base.scene.xml" ).str();
    StringBuilder<constant::buffer::kMax256> sceneXML;
    sceneXML.appendFormat( R"(<Scene _base="%#" name="Child" />)", baseID );
    Util::write( domain, "child.scene.xml", sceneXML.view() );

    SceneDocument scene{};
    SW_REQUIRE_TRUE( scene.loadXML( ScopedScratchDomain::makeID( "child.scene.xml" ) ) );
    SW_EXPECT_EQUAL( string( "Child" ), scene._name );
    SW_EXPECT_EQUAL( baseID, scene._baseScenePath );
}

/** @brief [ResourceEntranceTest] 진짜 형식의 저장 입구를 지난다 — 조건을 푼 사본을 저장해도 파일에 조건이 남고, 다시 읽으면 같은 값이다 */
SW_TEST_CASE( ResourceEntranceTest, FormatSaveKeepsConditionsInTheFile )
{
    const ScopedScratchDomain domain;
    const XEntranceScope      scope;
    SW_REQUIRE_TRUE( engine::getSubsystem<ResourceTags>().setAxisValue( "quality", "low" ) );
    const AbsolutePath path = domain.makeDiskPath( "a.retarget.xml" );
    const string       text = R"(<RetargetProfile height_ratio="2"><_when _if="region.us" height_ratio="3" /><root source="root" target="root" /><pelvis source="pelvis" target="hips" />)"
                              R"(<chains><item name="LeftArm"><source><item>a</item></source><target><item>b</item></target></item></chains></RetargetProfile>)";
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, text ) );

    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( text, path.str() ) );
    const optional<AbsolutePath> absPath = AbsolutePath::tryMake( path.str() );
    SW_REQUIRE_TRUE( absPath.has_value() );
    SW_REQUIRE_TRUE( profile.saveToFile( *absPath ) );

    string saved;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, saved ) );
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( saved ) );
    SW_EXPECT_EQUAL( string( "2" ), string( doc.getRoot().getAttributeText( "height_ratio" ) ) );
    SW_EXPECT_TRUE( doc.getRoot().findChild( DataInheritance::kWhenElement ).isValid() );

    RetargetProfile reread;
    SW_REQUIRE_TRUE( reread.loadFromXMLText( saved, path.str() ) );
    SW_EXPECT_EQUAL( profile.toXML(), reread.toXML() );
}
