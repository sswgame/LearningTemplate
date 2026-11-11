// AnimationNotifyTableTest — 알림 테이블(`*.notifies.xml`)의 `<FootContact>` 줄 읽기(칸은 AnimationFootContactDef 의 PROPERTY).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Notify/AnimationNotifyTable.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

/**
 * @brief [AnimationNotifyTableTest] 발 닿음 — 밑줄 이름 칸(`contact_height` 등)을 읽고, 읽은 값을 그대로 둔다
 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactReadsUnderscoreProperties )
{
    const AnimationNotifyHandlerRegistry& registry = sw::engine::getSubsystem<sw::AnimationNotifyHandlerRegistry>();
    AnimationNotifyTable                  table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<AnimationNotifies>
        <Notify name="FootL" handler="Footstep" socket="foot_l"/>
        <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0.6"/>
    </AnimationNotifies>)",
                                            "good", registry ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( table.getFootContacts().size() ) );
    const AnimationFootContactDef& def = table.getFootContacts()[0];
    SW_EXPECT_TRUE( def._bone == hashed_string( "foot.l" ) );
    SW_EXPECT_NEAR( 0.05f, def._contactHeight, 1e-6f );
    SW_EXPECT_NEAR( 0.12f, def._releaseHeight, 1e-6f );
    SW_EXPECT_NEAR( 0.6f, def._maxVerticalSpeed, 1e-6f );
}

namespace
{
    /** @brief 표에 `<FootContact>` 한 줄을 붙여 읽습니다(알림 `FootL` 은 늘 있다). */
    bool loadFootContactLine( const utf8* pLine )
    {
        AnimationNotifyTable table;
        const string         xml = string( R"(<AnimationNotifies><Notify name="FootL" handler="Footstep" socket="foot_l"/>)" ) + pLine + "</AnimationNotifies>";
        return table.loadFromXMLText( xml, "bad", sw::engine::getSubsystem<sw::AnimationNotifyHandlerRegistry>() );
    }
} // namespace

/** @brief [AnimationNotifyTableTest] 발 닿음 — 필수 칸이 빠지면 읽기 오류다. 적지 않은 칸의 초기값(0)은 범위 오류를 덧붙이지 않는다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsMissingField )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12"/>)" ) );
}

/** @brief [AnimationNotifyTableTest] 발 닿음 — 세로 속도 0 은 PROPERTY( Min ) 밖이다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsZeroSpeed )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0"/>)" ) );
}

/** @brief [AnimationNotifyTableTest] 발 닿음 — 놓는 높이가 닿는 높이 위여야 한다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsHeightOrder )
{
    SW_EXPECT_LOG( "Character.FootContactHeightOrder" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.2" release_height="0.12" max_vertical_speed="0.6"/>)" ) );
}

/**
 * @brief [AnimationNotifyTableTest] 차이 파일 — 알림은 이름, 발 닿음은 알림 이름이 열쇠다. 처리기가 바뀌면 인자가 달라지므로 `_op="Replace"` 다
 */
SW_TEST_CASE( AnimationNotifyTableTest, ChildTableMergesByKeys )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.notifies.xml" ), R"(<AnimationNotifies>
        <Notify name="FootL" handler="Footstep" socket="foot_l"/>
        <Notify name="FootR" handler="Footstep" socket="foot_r"/>
        <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0.6"/>
    </AnimationNotifies>)" ) );
    const string         baseID = test::ScopedScratchDomain::makeID( "base.notifies.xml" ).str();
    AnimationNotifyTable table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( "<AnimationNotifies _base=\"" + baseID + "\"><Notify name=\"FootR\" _op=\"Remove\"/><FootContact notify=\"FootL\" bone=\"foot.b\"/></AnimationNotifies>", "child",
                                            sw::engine::getSubsystem<sw::AnimationNotifyHandlerRegistry>() ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, table.getEntries().size() );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( table.getFootContacts().size() ) );
    SW_EXPECT_TRUE( table.getFootContacts()[0]._bone == hashed_string( "foot.b" ) );
    SW_EXPECT_NEAR( 0.05f, table.getFootContacts()[0]._contactHeight, 1e-6f ); // 물려받음
}

/**
 * @brief [AnimationNotifyTableTest] 소켓 세트 차이 파일 — 소켓과 가상 뼈는 이름이 열쇠이고, 적지 않은 칸은 물려받는다
 */
SW_TEST_CASE( AnimationNotifyTableTest, ChildSocketSetMergesByName )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.sockets.xml" ), R"(<SocketSet>
        <Socket name="Eyes" parent="head" translation="0 1 0" kind="Attach"/>
        <Socket name="Chest" parent="spine" kind="Attach"/>
    </SocketSet>)" ) );
    const string    baseID = test::ScopedScratchDomain::makeID( "base.sockets.xml" ).str();
    SocketKindTable kinds;
    kinds.addKind( hashed_string( "Attach" ) );
    SocketSet sockets;
    SW_REQUIRE_TRUE( sockets.loadFromXMLText( "<SocketSet _base=\"" + baseID + "\"><Socket name=\"Eyes\" parent=\"head_b\"/><Socket name=\"Chest\" _op=\"Remove\"/></SocketSet>", "child", kinds ) );
    const SocketDef* pEyes = sockets.findSocket( hashed_string( "Eyes" ) );
    SW_REQUIRE_TRUE( pEyes != nullptr );
    SW_EXPECT_TRUE( pEyes->_parent == hashed_string( "head_b" ) );
    SW_EXPECT_NEAR( 1.0f, pEyes->_translation._y, 1e-6f ); // 물려받음
    SW_EXPECT_TRUE( sockets.findSocket( hashed_string( "Chest" ) ) == nullptr );
}

/**
 * @brief [AnimationNotifyTableTest] 소켓 세트 변형 저장 — 부모(`_base`)를 가리키던 세트는 부모와의 차이로 쓴다(물려받은 칸 · 지운 소켓). 저장 → 다시 읽기가 같은 세트다
 */
SW_TEST_CASE( AnimationNotifyTableTest, ChildSocketSetSavesOnlyItsDifference )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.sockets.xml" ), R"(<SocketSet>
        <Socket name="Eyes" parent="head" translation="0 1 0" kind="Attach"/>
        <Socket name="Chest" parent="spine" kind="Attach"/>
    </SocketSet>)" ) );
    const string    baseID = test::ScopedScratchDomain::makeID( "base.sockets.xml" ).str();
    SocketKindTable kinds;
    kinds.addKind( hashed_string( "Attach" ) );
    SocketSet sockets;
    SW_REQUIRE_TRUE( sockets.loadFromXMLText( "<SocketSet _base=\"" + baseID + "\"><Socket name=\"Eyes\" parent=\"head_b\"/><Socket name=\"Chest\" _op=\"Remove\"/></SocketSet>", "child", kinds ) );
    SW_EXPECT_EQUAL( baseID, sockets.getBaseID() );
    SocketDef eyes    = *sockets.findSocket( hashed_string( "Eyes" ) );
    eyes._translation = float3{ 0.0f, 2.0f, 0.0f };
    sockets.addSocket( eyes );

    const optional<AbsolutePath> childPath = AbsolutePath::tryMake( domain.makeDiskPath( "child.sockets.xml" ) );
    SW_REQUIRE_TRUE( childPath.has_value() );
    SW_REQUIRE_TRUE( sockets.saveToFile( *childPath, kinds ) );
    string saved;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath->str(), saved ) );
    SW_EXPECT_TRUE( saved.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "Remove" ) != string::npos ); // 부모의 Chest 를 지운 것도 차이다
    SW_EXPECT_TRUE( saved.find( "Attach" ) == string::npos ); // 물려받은 종류는 적지 않는다

    SocketSet reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromAsset( test::ScopedScratchDomain::makeID( "child.sockets.xml" ), kinds ) );
    const SocketDef* pEyes = reloaded.findSocket( hashed_string( "Eyes" ) );
    SW_REQUIRE_TRUE( pEyes != nullptr );
    SW_EXPECT_TRUE( pEyes->_parent == hashed_string( "head_b" ) );
    SW_EXPECT_TRUE( pEyes->_kind == hashed_string( "Attach" ) );
    SW_EXPECT_NEAR( 2.0f, pEyes->_translation._y, 1e-6f );
    SW_EXPECT_TRUE( reloaded.findSocket( hashed_string( "Chest" ) ) == nullptr );
    SW_EXPECT_EQUAL( baseID, reloaded.getBaseID() );
    SW_EXPECT_EQUAL( sockets.saveToXMLText(), reloaded.saveToXMLText() );
}
