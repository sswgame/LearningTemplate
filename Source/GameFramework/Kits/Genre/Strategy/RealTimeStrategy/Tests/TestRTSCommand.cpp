#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommand.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// RTSCommandTest — 사람 명령의 값(`RTSCommand`): 바이트로 쓰고 읽으면 같은 명령이고, 판이 다르거나 깨진 바이트는 거절하며, 적용은 명령을 낸 플레이어의 유닛만 받는다.
// 락스텝 대전(StarSkirmish)이 이 바이트를 틱마다 나누므로 왕복과 주인 확인이 깨지면 두 프로세스의 판이 어긋난다. 키트 GF_RealTimeStrategy(nogpu).

namespace
{
    constexpr const utf8* kUnitXML = R"(<RTSCatalog supplyMax="200">
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" minerals="400" buildTime="75" sight="10"/>
  <Unit id="marine" name="marine" hp="40" speed="2.25" radius="0.35" damage="6" range="4" cooldown="0.86" targets="Ground,Air" producedBy="command_center" minerals="50" supply="1" buildTime="18" sight="7"/>
</RTSCatalog>)";

    sw::RTSCommand makeSample()
    {
        sw::RTSCommand command;
        command._kind        = sw::RTSCommandKind::GroupMove;
        command._listUnit    = { sw::RTSUnitID::make( 3, 1 ), sw::RTSUnitID::make( 7, 2 ) };
        command._target      = sw::float3{ 12.5f, 0.0f, 30.25f };
        command._targetID    = sw::RTSUnitID::make( 9, 4 );
        command._defID       = sw::hashed_string( "marine" );
        command._cell        = sw::int2{ 5, 6 };
        command._bQueue      = SW_TRUE;
        command._bAttackMove = SW_TRUE;
        return command;
    }
} // namespace

/** @brief [RTSCommandTest] 명령 목록을 쓰고 읽으면 칸마다 같고, 빈 바이트는 명령 없음(이탈한 플레이어의 빈 입력)이다 */
SW_TEST_CASE( RTSCommandTest, ListRoundTripKeepsEveryField )
{
    const sw::vector<sw::RTSCommand> listWritten = { makeSample(), sw::RTSCommand{} };
    sw::vector<uint8>                bytes;
    sw::RTSCommand::writeList( listWritten, bytes );
    sw::vector<sw::RTSCommand> listRead;
    SW_REQUIRE_TRUE( sw::RTSCommand::readList( bytes, listRead ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listRead.size() );
    const sw::RTSCommand& read = listRead[0];
    SW_EXPECT_TRUE( read._kind == sw::RTSCommandKind::GroupMove );
    SW_REQUIRE_EQUAL( size_t( 2 ), read._listUnit.size() );
    SW_EXPECT_TRUE( read._listUnit[1] == sw::RTSUnitID::make( 7, 2 ) );
    SW_EXPECT_TRUE( read._targetID == sw::RTSUnitID::make( 9, 4 ) );
    SW_EXPECT_EQUAL( 30.25f, read._target._z );
    SW_EXPECT_TRUE( read._defID == sw::hashed_string( "marine" ) );
    SW_EXPECT_EQUAL( 6, read._cell._y );
    SW_EXPECT_EQUAL( uint8( SW_TRUE ), read._bQueue );
    SW_EXPECT_EQUAL( uint8( SW_TRUE ), read._bAttackMove );
    SW_EXPECT_TRUE( listRead[1]._kind == sw::RTSCommandKind::Smart );

    SW_EXPECT_TRUE( sw::RTSCommand::readList( sw::vector<uint8>{}, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );
}

/** @brief [RTSCommandTest] 잘린 바이트와 다른 판의 바이트는 거절하고 목록을 비운다 */
SW_TEST_CASE( RTSCommandTest, ReadListRefusesDamagedBytes )
{
    sw::vector<uint8> bytes;
    sw::RTSCommand::writeList( { makeSample() }, bytes );
    sw::vector<uint8>          truncated( bytes.begin(), bytes.end() - 3 );
    sw::vector<sw::RTSCommand> listRead = { makeSample() };
    SW_EXPECT_FALSE( sw::RTSCommand::readList( truncated, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );
    sw::vector<uint8> otherVersion = bytes;
    otherVersion[0] ^= 0x7Fu; // 첫 칸이 판 번호다
    SW_EXPECT_FALSE( sw::RTSCommand::readList( otherVersion, listRead ) );
}

/** @brief [RTSCommandTest] 적용은 명령을 낸 플레이어의 유닛만 받는다 - 남의 유닛을 섞은 명령은 내 유닛에만 걸린다 */
SW_TEST_CASE( RTSCommandTest, ApplyTakesOnlyTheIssuersUnits )
{
    sw::RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSCommandTest" ) );
    sw::RTSWorld world;
    SW_REQUIRE_TRUE( world.initialize( &catalog, 32, 32, sw::RTSSettings{} ) );
    sw::Wallet arrWallet[2];
    (void)world.addPlayer( 0, &arrWallet[0], sw::float3{} );
    (void)world.addPlayer( 1, &arrWallet[1], sw::float3{} );
    const sw::RTSUnitID mine   = world.spawnUnit( sw::hashed_string( "marine" ), 0, sw::float3{ 4.5f, 0.0f, 4.5f } );
    const sw::RTSUnitID theirs = world.spawnUnit( sw::hashed_string( "marine" ), 1, sw::float3{ 20.5f, 0.0f, 20.5f } );
    SW_REQUIRE_TRUE( mine.isValid() && theirs.isValid() );

    sw::RTSCommand command;
    command._kind     = sw::RTSCommandKind::Stop;
    command._listUnit = { mine, theirs };
    SW_EXPECT_EQUAL( 1, world.applyCommand( 0, command ) );
    command._listUnit = { theirs };
    SW_EXPECT_EQUAL( 0, world.applyCommand( 0, command ) );
    SW_EXPECT_EQUAL( 1, world.applyCommand( 1, command ) );
}
