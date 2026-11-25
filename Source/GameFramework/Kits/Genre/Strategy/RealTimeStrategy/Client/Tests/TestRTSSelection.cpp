// RTSSelectionTest — RTS 선택: 같은 종류 고르기, 겹친 유닛 · 상한을 넘는 상태 거절, 그리고 명령 입력 컴포넌트의 저장 계약(고른 유닛 · 부대는 이 컴포넌트의 구간이다). 키트 GF_Client_RealTimeStrategy(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

using namespace sw;

namespace
{
    constexpr const utf8* kUnitXML = R"(<RTSCatalog supplyMax="200">
  <Rules fixedStep="0.05" minimumDamage="0.5" repathInterval="0.5" visionInterval="0.25" interactSlack="0.35" resourceSearchRadius="8" constructionStartRatio="0.1" underAttackCooldown="10" autoTargetLeashScale="1.5" productionQueueMax="5" flowFieldGroupSize="6" bucketSize="4" mineralCurrency="Minerals" gasCurrency="Gas"/>
  <Unit id="command_center" name="command_center" kind="Building" hp="1500" armor="1" footprint="4" depot="true" provides="10" producedBy="worker" minerals="400" buildTime="75" sight="10"/>
  <Unit id="worker" name="worker" hp="60" speed="2.8" radius="0.35" damage="5" range="0.2" cooldown="1.1" worker="true" producedBy="command_center" minerals="50" supply="1" buildTime="13" gatherTime="2.8" cargo="8" sight="7"/>
  <Unit id="marine" name="marine" hp="40" speed="2.25" radius="0.35" damage="6" range="4" cooldown="0.86" targets="Ground,Air" producedBy="command_center" minerals="50" supply="1" buildTime="18" sight="7"/>
</RTSCatalog>)";

    constexpr int32 kMapSize = 48;

    /** @brief 두 플레이어가 해병 여덟(네 기 두 줄)과 일꾼 하나를 대칭으로 둔 월드입니다. */
    struct SelectionWorld
    {
        Wallet            _arrWallet[2];
        RTSWorld          _world;
        vector<RTSUnitID> _arrArmy[2];

        bool setUp( const RTSCatalog& catalog )
        {
            if ( _world.initialize( &catalog, kMapSize, kMapSize ) == false )
                return false;
            for ( int32 player = 0; player < 2; ++player )
            {
                (void)_world.addPlayer( player, _arrWallet[player], float3{} );
                const auto at = [player]( float32 x, float32 z )
                { return player == 0 ? float3{ x, 0.0f, z } : float3{ kMapSize - x, 0.0f, kMapSize - z }; };
                (void)_world.spawnUnit( hashed_string( "worker" ), player, at( 9.5f, 3.5f ) );
                for ( int32 index = 0; index < 8; ++index )
                    _arrArmy[player].push_back( _world.spawnUnit( hashed_string( "marine" ), player, at( 6.5f + static_cast<float32>( index % 4 ), 10.5f + static_cast<float32>( index / 4 ) ) ) );
            }
            return _arrArmy[0].front().isValid() && _arrArmy[1].front().isValid();
        }
    };
} // namespace

/**
 * @brief [RTSSelectionTest] 결함: 선택과 부대가 겹친 유닛과 상한을 넘는 선택을 그대로 받았다. 거절하고 선택을 둔다. 없는 유닛의 id 는 죽은 유닛의 id 와 같아 받는다
 */
SW_TEST_CASE( RTSSelectionTest, SelectionStateRefusesRepeatedAndOverLimit )
{
    const auto writeLists = []( const vector<uint64>& listSelected, const vector<uint64>& listFirstGroup, Archive& outArchive )
    {
        StateIO::writeIDList( outArchive, listSelected );
        StateIO::writeIDList( outArchive, listFirstGroup );
        for ( int32 group = 1; group < RTSSelection::kGroupCount; ++group )
        {
            StateIO::writeIDList( outArchive, vector<uint64>{} );
        }
    };
    const uint64 first  = RTSUnitID::make( 1, 1 ).packed();
    const uint64 second = RTSUnitID::make( 2, 1 ).packed();
    const uint64 third  = RTSUnitID::make( 3, 1 ).packed();

    RTSSelection selection;
    selection.setMaxCount( 2 );

    Archive writtenValid;
    writeLists( { first, second }, { third }, writtenValid );
    Archive readingValid( writtenValid.getData(), writtenValid.getSize() );
    SW_REQUIRE_TRUE( selection.readState( readingValid ) );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelected().size() );

    Archive writtenRepeated;
    writeLists( { third, third }, {}, writtenRepeated );
    Archive readingRepeated( writtenRepeated.getData(), writtenRepeated.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingRepeated ) );

    Archive writtenGroupRepeated;
    writeLists( { third }, { first, first }, writtenGroupRepeated );
    Archive readingGroupRepeated( writtenGroupRepeated.getData(), writtenGroupRepeated.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingGroupRepeated ) );

    Archive writtenOver;
    writeLists( { first, second, third }, {}, writtenOver );
    Archive readingOver( writtenOver.getData(), writtenOver.getSize() );
    SW_EXPECT_FALSE( selection.readState( readingOver ) );

    SW_REQUIRE_EQUAL( size_t( 2 ), selection.getSelected().size() );
    SW_EXPECT_EQUAL( first, selection.getSelected()[0].packed() );
}

/** @brief [RTSSelectionTest] 더블 클릭 선택은 사각형 안의 같은 종류인 내 유닛만 고른다. 남의 유닛을 누르면 그것 하나만 고르고 명령은 줄 수 없다. */
SW_TEST_CASE( RTSSelectionTest, SelectSameTypePicksOwnUnitsOfThatKindInTheRect )
{
    RTSCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kUnitXML, "RTSSelectionTest" ) );
    SelectionWorld match;
    SW_REQUIRE_TRUE( match.setUp( catalog ) );
    RTSSelection selection;
    selection.setPlayer( 0 );
    const float3 mapMin{ 0.0f, 0.0f, 0.0f };
    const float3 mapMax{ static_cast<float32>( kMapSize ), 0.0f, static_cast<float32>( kMapSize ) };

    // 맵 전체: 내 해병 8 — 내 일꾼과 상대 해병은 빠진다
    selection.selectSameType( match._world, match._arrArmy[0][0], mapMin, mapMax );
    SW_REQUIRE_EQUAL( size_t( 8 ), selection.getSelected().size() );
    SW_EXPECT_EQUAL( match._arrArmy[0][0].packed(), selection.getPrimary().packed() ); // 누른 유닛이 맨 앞이다
    for ( const RTSUnitID unitID : selection.getSelected() )
    {
        const RTSUnit* pUnit = match._world.findUnit( unitID );
        SW_REQUIRE_NOT_NULL( pUnit );
        SW_EXPECT_EQUAL( 0, pUnit->_owner );
        SW_EXPECT_TRUE( pUnit->_pDef->_id == hashed_string( "marine" ) );
    }
    SW_EXPECT_TRUE( selection.isCommandable( match._world ) );

    // 좁은 사각형: 앞줄(z 10.5)의 네 기만 — 모서리 순서는 상관없다
    selection.selectSameType( match._world, match._arrArmy[0][0], float3{ 10.0f, 0.0f, 10.9f }, float3{ 6.0f, 0.0f, 10.0f } );
    SW_EXPECT_EQUAL( size_t( 4 ), selection.getSelected().size() );

    // 남의 유닛을 누르면 그것 하나만 고른다
    selection.selectSameType( match._world, match._arrArmy[1][0], mapMin, mapMax );
    SW_REQUIRE_EQUAL( size_t( 1 ), selection.getSelected().size() );
    SW_EXPECT_TRUE( selection.isSelected( match._arrArmy[1][0] ) );
    SW_EXPECT_FALSE( selection.isCommandable( match._world ) );
}

/** @brief [RTSSelectionTest] 저장 계약: 고른 유닛 · 부대는 명령 입력 컴포넌트의 구간이다 — 쓰고 다른 컴포넌트에 되돌리면 같고, 깨진 바이트는 거절하고 고르기를 둔다 */
SW_TEST_CASE( RTSSelectionTest, CommandInputKeepsTheSelectionInItsOwnState )
{
    GameObjectManager manager;
    GameObject* const pObject = manager.createGameObject( hashed_string( "Director" ) );
    SW_REQUIRE_NOT_NULL( pObject->addComponent<SkirmishGameModeComponent>() );
    RTSCommandInputComponent* const pCommandInput = pObject->addComponent<RTSCommandInputComponent>();
    SW_REQUIRE_NOT_NULL( pCommandInput );

    const RTSUnitID selected = RTSUnitID::make( 3, 1 );
    const RTSUnitID grouped  = RTSUnitID::make( 5, 2 );
    Archive         written;
    RTSCommandInputComponent::kStateFormat.write( written );
    written << uint64{ 7 }; // 이 고르기가 속한 판
    StateIO::writeIDList( written, vector<uint64>{ selected.packed() } );
    StateIO::writeIDList( written, vector<uint64>{ grouped.packed() } );
    for ( int32 group = 1; group < RTSSelection::kGroupCount; ++group )
    {
        StateIO::writeIDList( written, vector<uint64>{} );
    }
    vector<uint8> bytes;
    written.writeData( bytes );
    pCommandInput->restoreState( vector<uint8>( bytes ) );
    SW_EXPECT_TRUE( pCommandInput->isSelected( selected ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), pCommandInput->getSelection().getGroup( 0 ).size() );
    SW_EXPECT_EQUAL( grouped.packed(), pCommandInput->getSelection().getGroup( 0 )[0].packed() );

    // 쓴 바이트는 되돌린 것과 같다.
    Archive rewritten;
    pCommandInput->writeState( rewritten );
    vector<uint8> rewrittenBytes;
    rewritten.writeData( rewrittenBytes );
    SW_EXPECT_TRUE( rewrittenBytes == bytes );

    SW_EXPECT_LOG( "RTSCommandInput.SavedSelectionRefused" );
    bytes.pop_back();
    pCommandInput->restoreState( std::move( bytes ) );
    SW_EXPECT_TRUE( pCommandInput->isSelected( selected ) );
}
