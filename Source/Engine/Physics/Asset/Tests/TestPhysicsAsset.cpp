// PhysicsAssetTest — 물리 에셋(`PhysicsAsset`) 저장: 부모(`_base`)를 가리키던 에셋은 부모와 다른 칸만 쓰고 다시 읽으면 같은 값이다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"

using namespace sw;

namespace
{
    struct PhysicsAssetTestUtil
    {
        /** @brief 셰이프 하나(기본 상자)를 가진 바디입니다. */
        static PhysicsAssetBodyDef makeBody( const utf8* pBone, float32 mass )
        {
            PhysicsAssetBodyDef body;
            body._bone = hashed_string( pBone );
            body._mass = mass;
            body._listShape.emplace_back();
            return body;
        }
    };
} // namespace

/**
 * @brief [PhysicsAssetTest] 변형 저장 — 부모를 가리키던 에셋은 부모와 다른 칸만 쓰고(고치지 않은 바디 · 셰이프는 적지 않는다), 저장 → 다시 읽기가 같은 값이다
 */
SW_TEST_CASE( PhysicsAssetTest, ChildSavesOnlyItsDifference )
{
    const test::ScopedScratchDomain domain;
    PhysicsAsset                    base;
    base._listBody.push_back( PhysicsAssetTestUtil::makeBody( "pelvis", 10.0f ) );
    base._listBody.push_back( PhysicsAssetTestUtil::makeBody( "spine", 6.0f ) );
    const std::optional<AbsolutePath> basePath = AbsolutePath::tryMake( domain.makeDiskPath( "base.physics.xml" ) );
    SW_REQUIRE_TRUE( basePath.has_value() );
    SW_REQUIRE_TRUE( base.saveToFile( *basePath ) );
    const string baseID = test::ScopedScratchDomain::makeID( "base.physics.xml" ).str();

    PhysicsAsset child;
    SW_REQUIRE_TRUE( child.loadFromXMLText( "<PhysicsAsset _base=\"" + baseID + "\" />", "child.physics.xml" ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( child._listBody.size() ) );
    SW_EXPECT_EQUAL( baseID, child._baseID );
    child._listBody[1]._mass = 4.0f;

    const std::optional<AbsolutePath> childPath = AbsolutePath::tryMake( domain.makeDiskPath( "child.physics.xml" ) );
    SW_REQUIRE_TRUE( childPath.has_value() );
    SW_REQUIRE_TRUE( child.saveToFile( *childPath ) );
    string saved;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath->str(), saved ) );
    SW_EXPECT_TRUE( saved.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "pelvis" ) == string::npos ); // 고치지 않은 바디는 물려받는다

    PhysicsAsset reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromAsset( test::ScopedScratchDomain::makeID( "child.physics.xml" ) ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( reloaded._listBody.size() ) );
    SW_EXPECT_NEAR( 10.0f, reloaded._listBody[0]._mass, 0.0f );
    SW_EXPECT_NEAR( 4.0f, reloaded._listBody[1]._mass, 0.0f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( reloaded._listBody[1]._listShape.size() ) );
    SW_EXPECT_EQUAL( baseID, reloaded._baseID );
}
