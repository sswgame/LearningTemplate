#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureTown.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"

// CreatureTownReadStateTest — 생물 마을 상태 바이트의 신뢰 경계: 없는 종 · 서식지 · 집, 겹친 항목, 서로 맞지 않는 입주, 범위 밖 값을 거절하고 마을을 그대로 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨린다.

namespace
{
    constexpr int32       kTownSize   = 4;
    constexpr const utf8* kCatalogXML = R"(<CreatureLifeCatalog>
  <Habitat id="grove" name="Grove" capacity="1">
    <Key symbol="T" object="tree"/>
    <Row cells="T"/>
  </Habitat>
  <Species id="sprout" name="Sprout" habitats="grove" chance="1" abilities="plant"/>
  <Species id="moss" name="Moss" habitats="grove" chance="1"/>
  <Ability id="plant" uses="2"><Rule from="empty" to="tree"/></Ability>
  <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/></Appeal>
</CreatureLifeCatalog>)";

    /** @brief 서식지 하나에 생물 하나가 머물고 집 하나에 사는 마을을 꾸밉니다. */
    struct CreatureTownFixture
    {
        sw::vector<sw::HabitatInstance> _listHabitat{};
        sw::vector<sw::TownCreature>    _listCreature{};
        sw::vector<sw::CreatureHouse>   _listHouse{};
        sw::vector<sw::hashed_string>   _listOpenRequest{};
        int32                           _nextHabitatID{ 2 };
        int32                           _lastAttractKey{ -1 };

        CreatureTownFixture()
        {
            sw::HabitatInstance habitat;
            habitat._habitatID = "grove";
            habitat._id        = 1;
            habitat._listTile  = { 0 };
            _listHabitat.push_back( habitat );

            sw::TownCreature creature;
            creature._speciesID      = "sprout";
            creature._listAbilityUse = { 1 };
            creature._habitat        = 1;
            creature._house          = 0;
            _listCreature.push_back( creature );

            sw::CreatureHouse house;
            house._listResident = { "sprout" };
            house._tile         = sw::int2{ 3, 3 };
            house._capacity     = 1;
            _listHouse.push_back( house );
        }

        bool readInto( sw::CreatureTown& inoutTown ) const
        {
            sw::Archive written;
            written << kTownSize;
            written << kTownSize;
            for ( int32 tile = 0; tile < kTownSize * kTownSize; ++tile )
            {
                sw::StateIO::writeName( written, sw::hashed_string{} );
            }
            written << static_cast<uint32>( _listHabitat.size() );
            for ( const sw::HabitatInstance& habitat : _listHabitat )
            {
                sw::StateIO::writeName( written, habitat._habitatID );
                sw::StateIO::writeInt2( written, habitat._origin );
                written << habitat._id;
                written << habitat._rotation;
                written << static_cast<uint32>( habitat._listTile.size() );
                for ( const int32 tileIndex : habitat._listTile )
                {
                    written << tileIndex;
                }
            }
            written << static_cast<uint32>( _listCreature.size() );
            for ( const sw::TownCreature& creature : _listCreature )
            {
                sw::StateIO::writeName( written, creature._speciesID );
                written << static_cast<uint32>( creature._listAbilityUse.size() );
                for ( const int32 useCount : creature._listAbilityUse )
                {
                    written << useCount;
                }
                written << creature._habitat;
                written << creature._house;
                written << creature._lastTalkDay;
                written << creature._lastGiftDay;
                written << creature._arrivalDay;
            }
            written << static_cast<uint32>( _listHouse.size() );
            for ( const sw::CreatureHouse& house : _listHouse )
            {
                written << static_cast<uint32>( house._listResident.size() );
                for ( const sw::hashed_string& resident : house._listResident )
                {
                    sw::StateIO::writeName( written, resident );
                }
                sw::StateIO::writeInt2( written, house._tile );
                written << house._capacity;
            }
            written << static_cast<uint32>( _listOpenRequest.size() );
            for ( const sw::hashed_string& questID : _listOpenRequest )
            {
                sw::StateIO::writeName( written, questID );
            }
            written << _nextHabitatID;
            written << _lastAttractKey;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutTown.readState( reading );
        }
    };

    struct CreatureTownReadStateTest
    {
        sw::CreatureLifeCatalog _catalog{};
        sw::CreatureTown        _town{};

        CreatureTownReadStateTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kCatalogXML, "CreatureTownReadStateTest" ) );
            SW_REQUIRE_TRUE( _town.initialize( &_catalog, sw::SessionStateRefs{}, kTownSize, kTownSize, sw::CreatureTownSettings{} ) );
        }
    };
} // namespace

/**
 * @brief [CreatureTownReadStateTest] 서식지 · 생물 · 집이 있는 실제 마을은 왕복하고, 형식에 맞게 꾸민 마을도 읽힌다
 */
SW_TEST_CASE_F( CreatureTownReadStateTest, RealAndForgedStatesRead )
{
    SW_REQUIRE_TRUE( _town.setObject( 0, 0, "tree" ) );
    SW_REQUIRE_EQUAL( 1, _town.attractVisitorsAt( 0, 8, sw::DayPhase::Day, "sunny" ) );
    SW_REQUIRE_EQUAL( 0, _town.placeHouse( 3, 3, 2 ) );
    SW_REQUIRE_EQUAL( 1, _town.assignHomeless() );

    sw::Archive written;
    _town.writeState( written );
    sw::CreatureTown restored;
    SW_REQUIRE_TRUE( restored.initialize( &_catalog, sw::SessionStateRefs{}, kTownSize, kTownSize, sw::CreatureTownSettings{} ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( size_t( 1 ), restored.getCreatures().size() );
    SW_EXPECT_EQUAL( 0, restored.getCreatures()[0]._house );

    SW_REQUIRE_TRUE( CreatureTownFixture{}.readInto( restored ) );
    SW_EXPECT_EQUAL( size_t( 1 ), restored.getHabitats().size() );
}

/**
 * @brief [CreatureTownReadStateTest] 결함: 서식지의 정의 · 셀 · 번호를 보지 않았다. 모르는 서식지, 빈 셀 목록, 범위 밖 회전, 겹친 번호와 셀, 다음 번호를 넘는 번호, 정원을 넘는 생물을 거절하고 마을을 둔다
 */
SW_TEST_CASE_F( CreatureTownReadStateTest, RefusesBrokenHabitats )
{
    SW_REQUIRE_TRUE( CreatureTownFixture{}.readInto( _town ) );

    CreatureTownFixture unknown;
    unknown._listHabitat[0]._habitatID = "swamp";
    SW_EXPECT_FALSE( unknown.readInto( _town ) );

    CreatureTownFixture noTiles;
    noTiles._listHabitat[0]._listTile.clear();
    SW_EXPECT_FALSE( noTiles.readInto( _town ) );

    CreatureTownFixture tileOutside;
    tileOutside._listHabitat[0]._listTile = { kTownSize * kTownSize };
    SW_EXPECT_FALSE( tileOutside.readInto( _town ) );

    CreatureTownFixture originOutside;
    originOutside._listHabitat[0]._origin = sw::int2{ kTownSize, 0 };
    SW_EXPECT_FALSE( originOutside.readInto( _town ) );

    CreatureTownFixture rotationOutside;
    rotationOutside._listHabitat[0]._rotation = 4;
    SW_EXPECT_FALSE( rotationOutside.readInto( _town ) );

    CreatureTownFixture idZero;
    idZero._listHabitat[0]._id = 0;
    SW_EXPECT_FALSE( idZero.readInto( _town ) );

    CreatureTownFixture idAhead;
    idAhead._nextHabitatID = 1;
    SW_EXPECT_FALSE( idAhead.readInto( _town ) );

    CreatureTownFixture idRepeated;
    idRepeated._listHabitat.push_back( idRepeated._listHabitat[0] );
    idRepeated._listHabitat[1]._listTile = { 1 };
    SW_EXPECT_FALSE( idRepeated.readInto( _town ) );

    CreatureTownFixture tileShared;
    tileShared._listHabitat.push_back( tileShared._listHabitat[0] );
    tileShared._listHabitat[1]._id = 2;
    tileShared._nextHabitatID      = 3;
    SW_EXPECT_FALSE( tileShared.readInto( _town ) );

    CreatureTownFixture overCapacity;
    sw::TownCreature    second;
    second._speciesID = "moss";
    second._habitat   = 1;
    overCapacity._listCreature.push_back( second );
    SW_EXPECT_FALSE( overCapacity.readInto( _town ) );

    SW_EXPECT_EQUAL( size_t( 1 ), _town.getHabitats().size() );
    SW_EXPECT_EQUAL( 1, _town.getHabitats()[0]._id );
}

/**
 * @brief [CreatureTownReadStateTest] 결함: 종과 사용 횟수를 보지 않았다. 모르는 종, 겹친 종, 음수 · 하루 횟수를 넘는 사용, 종의 능력보다 많은 칸, 없는 서식지, 깨진 날을 거절하고 마을을 둔다
 */
SW_TEST_CASE_F( CreatureTownReadStateTest, RefusesBrokenCreatures )
{
    SW_REQUIRE_TRUE( CreatureTownFixture{}.readInto( _town ) );

    CreatureTownFixture unknown;
    unknown._listCreature[0]._speciesID = "ghost";
    unknown._listHouse[0]._listResident = { "ghost" };
    SW_EXPECT_FALSE( unknown.readInto( _town ) );

    CreatureTownFixture repeated;
    repeated._listCreature.push_back( repeated._listCreature[0] );
    repeated._listCreature[1]._habitat = -1;
    SW_EXPECT_FALSE( repeated.readInto( _town ) );

    CreatureTownFixture useNegative;
    useNegative._listCreature[0]._listAbilityUse = { -1 };
    SW_EXPECT_FALSE( useNegative.readInto( _town ) );

    CreatureTownFixture useOver;
    useOver._listCreature[0]._listAbilityUse = { 3 }; // 하루 둘이다
    SW_EXPECT_FALSE( useOver.readInto( _town ) );

    CreatureTownFixture useSlots;
    useSlots._listCreature[0]._listAbilityUse = { 0, 0 };
    SW_EXPECT_FALSE( useSlots.readInto( _town ) );

    CreatureTownFixture habitatMissing;
    habitatMissing._listCreature[0]._habitat = 9;
    SW_EXPECT_FALSE( habitatMissing.readInto( _town ) );

    CreatureTownFixture habitatBroken;
    habitatBroken._listCreature[0]._habitat = -2;
    SW_EXPECT_FALSE( habitatBroken.readInto( _town ) );

    CreatureTownFixture talkBroken;
    talkBroken._listCreature[0]._lastTalkDay = -2;
    SW_EXPECT_FALSE( talkBroken.readInto( _town ) );

    CreatureTownFixture arrivalBroken;
    arrivalBroken._listCreature[0]._arrivalDay = -1;
    SW_EXPECT_FALSE( arrivalBroken.readInto( _town ) );

    SW_EXPECT_EQUAL( size_t( 1 ), _town.getCreatures().size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "sprout" ), _town.getCreatures()[0]._speciesID );
}

/**
 * @brief [CreatureTownReadStateTest] 결함: 집과 생물이 서로를 가리키는지 보지 않아 이사가 남의 입주자를 지울 수 있었다. 없는 집, 입주자로 적히지 않은 생물, 다른 집을 적은 입주자, 겹친 입주자, 정원 초과, 격자 밖 집, 0 정원, 겹친 · 빈 부탁을 거절하고 마을을 둔다
 */
SW_TEST_CASE_F( CreatureTownReadStateTest, RefusesBrokenHousesAndRequests )
{
    SW_REQUIRE_TRUE( CreatureTownFixture{}.readInto( _town ) );

    CreatureTownFixture houseMissing;
    houseMissing._listCreature[0]._house = 1;
    SW_EXPECT_FALSE( houseMissing.readInto( _town ) );

    CreatureTownFixture notResident;
    notResident._listHouse[0]._listResident.clear();
    SW_EXPECT_FALSE( notResident.readInto( _town ) );

    CreatureTownFixture residentElsewhere;
    residentElsewhere._listCreature[0]._house = -1;
    SW_EXPECT_FALSE( residentElsewhere.readInto( _town ) );

    CreatureTownFixture residentUnknown;
    residentUnknown._listHouse[0]._capacity     = 2;
    residentUnknown._listHouse[0]._listResident = { "sprout", "moss" };
    SW_EXPECT_FALSE( residentUnknown.readInto( _town ) );

    CreatureTownFixture residentRepeated;
    residentRepeated._listHouse[0]._capacity     = 2;
    residentRepeated._listHouse[0]._listResident = { "sprout", "sprout" };
    SW_EXPECT_FALSE( residentRepeated.readInto( _town ) );

    CreatureTownFixture overFull;
    overFull._listHouse[0]._capacity = 0;
    SW_EXPECT_FALSE( overFull.readInto( _town ) );

    CreatureTownFixture houseOutside;
    houseOutside._listHouse[0]._tile = sw::int2{ kTownSize, 0 };
    SW_EXPECT_FALSE( houseOutside.readInto( _town ) );

    CreatureTownFixture requestEmpty;
    requestEmpty._listOpenRequest = { sw::hashed_string{} };
    SW_EXPECT_FALSE( requestEmpty.readInto( _town ) );

    CreatureTownFixture requestRepeated;
    requestRepeated._listOpenRequest = { "quest", "quest" };
    SW_EXPECT_FALSE( requestRepeated.readInto( _town ) );

    CreatureTownFixture attractBroken;
    attractBroken._lastAttractKey = -2;
    SW_EXPECT_FALSE( attractBroken.readInto( _town ) );

    SW_EXPECT_EQUAL( 0, _town.getCreatures()[0]._house );
    SW_EXPECT_EQUAL( size_t( 1 ), _town.getHouses()[0]._listResident.size() );
}
