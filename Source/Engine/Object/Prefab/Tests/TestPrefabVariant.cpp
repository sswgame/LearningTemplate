// PrefabVariantTest — 프리팹 변형(`_base`): 부모 위에 차이를 얹어 읽고, 저장 · 쿠킹본 · 다시 읽기가 같은 값을 잇는다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    struct PrefabVariantTestUtil
    {
        static string makeBase( const utf8* pPosition )
        {
            string text = "<Prefab formatVersion=\"0\" name=\"Base\"><GameObject _schemaVersion=\"0\" _name=\"Base\" _bActive=\"true\"><_listComponent>"
                          "<SceneComponent _componentName=\"Root\" _bActive=\"true\" _localScale=\"2,2,2\" _localPosition=\"";
            text += pPosition;
            text += "\" /></_listComponent></GameObject></Prefab>";
            return text;
        }

        static string makeVariant( const string& baseID, const utf8* pOverrides )
        {
            return "<Prefab formatVersion=\"0\" _base=\"" + baseID + "\" name=\"Child\">" + pOverrides + "</Prefab>";
        }

        static constexpr const utf8* kMoveRoot = "<PrefabOverrides><Override key=\"Root#0\"><SceneComponent _localPosition=\"9,9,9\" /></Override></PrefabOverrides>";

        /** @brief 부모 `base.prefab.xml` 과 자식 `child.prefab.xml` 을 씁니다. */
        static void writePair( const test::ScopedScratchDomain& domain, const utf8* pOverrides = kMoveRoot )
        {
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.prefab.xml" ), makeBase( "1,2,3" ) ) );
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "child.prefab.xml" ), makeVariant( test::ScopedScratchDomain::makeID( "base.prefab.xml" ).str(), pOverrides ) ) );
        }

        /** @brief 칸 하나를 상태 글에서 읽습니다. */
        static string readRootField( const PrefabAsset& asset, const utf8* pField )
        {
            XMLDocument doc;
            SW_EXPECT_TRUE( doc.parse( asset.getStateData() ) );
            return string{ doc.getRoot().findChild( "_listComponent" ).findChild().getAttributeText( pField ) };
        }

        static AbsolutePath makeAbsolute( const string& diskPath )
        {
            const optional<AbsolutePath> absPath = FileUtil::requireAbsolutePath( diskPath );
            SW_EXPECT_TRUE( absPath.has_value() );
            return *absPath;
        }
    };
    using Util = PrefabVariantTestUtil;
} // namespace

/** @brief [PrefabVariantTest] 변형을 읽으면 상태는 부모 위에 차이를 얹은 값이고, 이름은 변형의 것이다 */
SW_TEST_CASE( PrefabVariantTest, VariantReadsAsMergedState )
{
    const test::ScopedScratchDomain domain;
    Util::writePair( domain );
    PrefabAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLFile( test::ScopedScratchDomain::makeID( "child.prefab.xml" ) ) );
    SW_EXPECT_EQUAL( string( "Child" ), asset.getName() );
    SW_EXPECT_EQUAL( test::ScopedScratchDomain::makeID( "base.prefab.xml" ).str(), asset.getBasePath() );
    SW_EXPECT_EQUAL( string( "9,9,9" ), Util::readRootField( asset, "_localPosition" ) );
    SW_EXPECT_EQUAL( string( "2,2,2" ), Util::readRootField( asset, "_localScale" ) ); // 적지 않은 칸은 부모 것
    XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( asset.getStateData() ) );
    SW_EXPECT_EQUAL( string( "Child" ), string( doc.getRoot().getAttributeText( "_name" ) ) );
}

/** @brief [PrefabVariantTest] 쿠킹본은 원본을 합쳐 읽은 것과 같은 상태이고 부모를 가리키지 않는다 */
SW_TEST_CASE( PrefabVariantTest, CookedBinaryMatchesTheMergedSource )
{
    const test::ScopedScratchDomain domain;
    Util::writePair( domain );
    PrefabAsset source;
    SW_REQUIRE_TRUE( source.loadFromDiskFile( domain.makeDiskPath( "child.prefab.xml" ) ) );
    SW_REQUIRE_TRUE( source.saveToBinaryFile( Util::makeAbsolute( domain.makeDiskPath( "child.prefab.bin" ) ) ) );
    PrefabAsset cooked;
    SW_REQUIRE_TRUE( cooked.loadFromBinaryFile( test::ScopedScratchDomain::makeID( "child.prefab.bin" ) ) );
    SW_EXPECT_EQUAL( source.getName(), cooked.getName() );
    SW_EXPECT_TRUE( cooked.getBasePath().empty() );
    SW_EXPECT_EQUAL( Util::readRootField( source, "_localPosition" ), Util::readRootField( cooked, "_localPosition" ) );
    SW_EXPECT_EQUAL( Util::readRootField( source, "_localScale" ), Util::readRootField( cooked, "_localScale" ) );
}

/** @brief [PrefabVariantTest] 저장은 변형으로 남는다 — 파일에 부모 경로와 다른 칸만 적고, 다시 읽으면 같은 값이다. 부모의 다른 칸을 고치면 따라간다 */
SW_TEST_CASE( PrefabVariantTest, SaveKeepsVariantAndRoundTrips )
{
    const test::ScopedScratchDomain domain;
    Util::writePair( domain );
    PrefabAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLFile( test::ScopedScratchDomain::makeID( "child.prefab.xml" ) ) );
    const string savedPath = domain.makeDiskPath( "saved.prefab.xml" );
    SW_REQUIRE_TRUE( asset.saveToFile( Util::makeAbsolute( savedPath ) ) );

    string savedText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( savedPath, savedText ) );
    SW_EXPECT_EQUAL( asset.getBasePath(), PrefabAsset::readBasePathFromSource( savedText ) );
    SW_EXPECT_TRUE( savedText.find( "<GameObject" ) == string::npos );
    SW_EXPECT_TRUE( savedText.find( "_localScale" ) == string::npos ); // 부모와 같은 칸은 쓰지 않는다

    PrefabAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromDiskFile( savedPath ) );
    string before;
    string after;
    SW_REQUIRE_TRUE( PrefabOverrides::makeBaseState( asset, before ) );
    SW_REQUIRE_TRUE( PrefabOverrides::makeBaseState( reread, after ) );
    SW_EXPECT_EQUAL( before, after );
}

/** @brief [PrefabVariantTest] 부모 없는 저장은 형식 기본값과 같은 칸을 쓰지 않고, 다시 읽으면 같은 값이다 */
SW_TEST_CASE( PrefabVariantTest, SaveOmitsDefaultValues )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.prefab.xml" ), Util::makeBase( "1,2,3" ) ) );
    PrefabAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromDiskFile( domain.makeDiskPath( "base.prefab.xml" ) ) );
    const string savedPath = domain.makeDiskPath( "saved.prefab.xml" );
    SW_REQUIRE_TRUE( asset.saveToFile( Util::makeAbsolute( savedPath ) ) );

    string savedText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( savedPath, savedText ) );
    SW_EXPECT_TRUE( savedText.find( "_localPosition=\"1,2,3\"" ) != string::npos );
    SW_EXPECT_TRUE( savedText.find( "_bActive" ) == string::npos ); // 기본값(켜짐)이다

    PrefabAsset reread;
    SW_REQUIRE_TRUE( reread.loadFromDiskFile( savedPath ) );
    string before;
    string after;
    SW_REQUIRE_TRUE( PrefabOverrides::makeBaseState( asset, before ) );
    SW_REQUIRE_TRUE( PrefabOverrides::makeBaseState( reread, after ) );
    SW_EXPECT_EQUAL( before, after );
}

/** @brief [PrefabVariantTest] 없는 부모 · 순환 · 없는 대상은 로드 오류다. 본문과 부모를 함께 적은 파일도 오류다 */
SW_TEST_CASE( PrefabVariantTest, BadVariantsFailToLoad )
{
    const test::ScopedScratchDomain domain;
    const string                    idA = test::ScopedScratchDomain::makeID( "a.prefab.xml" ).str();
    const string                    idB = test::ScopedScratchDomain::makeID( "b.prefab.xml" ).str();
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "orphan.prefab.xml" ), Util::makeVariant( test::ScopedScratchDomain::makeID( "missing.prefab.xml" ).str(), "" ) ) );
        PrefabAsset asset;
        SW_EXPECT_LOG( "PrefabAsset.PrefabNotLoaded" );
        SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
        SW_EXPECT_FALSE( asset.loadFromDiskFile( domain.makeDiskPath( "orphan.prefab.xml" ) ) );
    }
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "a.prefab.xml" ), Util::makeVariant( idB, "" ) ) );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "b.prefab.xml" ), Util::makeVariant( idA, "" ) ) );
        PrefabAsset asset;
        SW_EXPECT_LOG( "DataInheritance.Cycle" );
        SW_EXPECT_FALSE( asset.loadFromDiskFile( domain.makeDiskPath( "a.prefab.xml" ) ) );
    }
    {
        Util::writePair( domain, "<PrefabOverrides><Remove key=\"Nope#0\" /></PrefabOverrides>" );
        PrefabAsset asset;
        SW_EXPECT_LOG( "PrefabOverrides.VariantTargetMissing" );
        SW_EXPECT_FALSE( asset.loadFromDiskFile( domain.makeDiskPath( "child.prefab.xml" ) ) );
    }
    {
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "both.prefab.xml" ), Util::makeVariant( test::ScopedScratchDomain::makeID( "base.prefab.xml" ).str(), "<GameObject _schemaVersion=\"0\" _name=\"X\" />" ) ) );
        PrefabAsset asset;
        SW_EXPECT_LOG( "PrefabAsset.VariantHasBody" );
        SW_EXPECT_FALSE( asset.loadFromDiskFile( domain.makeDiskPath( "both.prefab.xml" ) ) );
    }
}

/** @brief [PrefabVariantTest] 부모를 고치고 다시 읽으면 캐시된 변형도 고친 부모로 합쳐진다. 적은 칸은 변형 값이 남는다 */
SW_TEST_CASE( PrefabVariantTest, ReloadOfBaseReachesCachedVariant )
{
    const test::ScopedScratchDomain domain;
    Util::writePair( domain );
    const AssetID baseID  = test::ScopedScratchDomain::makeID( "base.prefab.xml" );
    const AssetID childID = test::ScopedScratchDomain::makeID( "child.prefab.xml" );
    PrefabCache   cache;
    SW_REQUIRE_TRUE( cache.loadPrefab( childID.view() ) != nullptr );

    const string edited = "<Prefab formatVersion=\"0\" name=\"Base\"><GameObject _schemaVersion=\"0\" _name=\"Base\" _bActive=\"true\"><_listComponent>"
                          "<SceneComponent _componentName=\"Root\" _bActive=\"true\" _localScale=\"5,5,5\" _localPosition=\"1,2,3\" /></_listComponent></GameObject></Prefab>";
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.prefab.xml" ), edited ) );
    cache.reload( baseID );

    const shared_ptr<const PrefabAsset> pChild = cache.loadPrefab( childID.view() );
    SW_REQUIRE_TRUE( pChild != nullptr );
    SW_EXPECT_EQUAL( string( "5,5,5" ), Util::readRootField( *pChild, "_localScale" ) );
    SW_EXPECT_EQUAL( string( "9,9,9" ), Util::readRootField( *pChild, "_localPosition" ) );
}

/** @brief [PrefabVariantTest] 칸의 출처는 그 칸을 적은 프리팹이다 */
SW_TEST_CASE( PrefabVariantTest, FieldOriginFollowsTheChain )
{
    const test::ScopedScratchDomain domain;
    Util::writePair( domain );
    const string baseID  = test::ScopedScratchDomain::makeID( "base.prefab.xml" ).str();
    const string childID = test::ScopedScratchDomain::makeID( "child.prefab.xml" ).str();
    PrefabCache  cache;
    SW_EXPECT_EQUAL( childID, cache.findFieldOrigin( childID, "Root#0", "_localPosition" ) );
    SW_EXPECT_EQUAL( baseID, cache.findFieldOrigin( childID, "Root#0", "_localScale" ) );
    SW_EXPECT_EQUAL( baseID, cache.findFieldOrigin( childID, "", "_bActive" ) );
}

/** @brief [PrefabVariantTest] 변형은 부모의 중첩 인스턴스를 물려받고 자기 노드를 뒤에 더한다 — 저장은 자기 노드만 적는다 */
SW_TEST_CASE( PrefabVariantTest, NestedInstancesAreInherited )
{
    const test::ScopedScratchDomain domain;
    const string                    partID = test::ScopedScratchDomain::makeID( "part.prefab.xml" ).str();
    const string                    baseID = test::ScopedScratchDomain::makeID( "nestedbase.prefab.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "part.prefab.xml" ), Util::makeBase( "0,0,0" ) ) );
    string baseText = Util::makeBase( "1,2,3" );
    baseText.insert( baseText.rfind( "</Prefab>" ), "<PrefabInstance name=\"Inherited\" prefab=\"" + partID + "\" />" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "nestedbase.prefab.xml" ), baseText ) );
    const string ownNode = "<PrefabInstance name=\"Own\" prefab=\"" + partID + "\" />";
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "child.prefab.xml" ), Util::makeVariant( baseID, ownNode.c_str() ) ) );

    PrefabAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromDiskFile( domain.makeDiskPath( "child.prefab.xml" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, asset.getNestedInstances().size() );
    SW_EXPECT_EQUAL( string( "Inherited" ), asset.getNestedInstances()[0]._name );
    SW_EXPECT_EQUAL( string( "Own" ), asset.getNestedInstances()[1]._name );

    const string savedPath = domain.makeDiskPath( "saved.prefab.xml" );
    SW_REQUIRE_TRUE( asset.saveToFile( Util::makeAbsolute( savedPath ) ) );
    string savedText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( savedPath, savedText ) );
    SW_EXPECT_TRUE( savedText.find( "Inherited" ) == string::npos );
    SW_EXPECT_TRUE( savedText.find( "Own" ) != string::npos );
}
