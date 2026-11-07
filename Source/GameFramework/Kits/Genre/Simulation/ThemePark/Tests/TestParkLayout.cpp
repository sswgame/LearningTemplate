// ParkLayoutTest — 공원 배치(`ParkLayout`)를 공유 캐시로 읽는다: 같은 경로는 같은 객체이고, 로더가 같은 팩의 코스터 레이아웃을 공유 캐시에서 받아 쥔다.
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

using namespace sw;

namespace
{
    struct ParkLayoutTestUtil
    {
        /** @brief 실제 데이터(`coasters.xml` 의 family)와 같은 회로 하나입니다. */
        static constexpr const utf8* kCoasters = R"(<CoasterLayoutCatalog>
  <Layout id="family" name="Little Dipper" startHeight="1">
    <Piece type="Station" length="10"/>
    <Piece type="LiftHill" length="16" height="8"/>
    <Piece type="Drop" length="16" height="8"/>
    <Piece type="TurnLeft" radius="10" angle="3.14159" bank="20"/>
    <Piece type="Straight" length="54"/>
    <Piece type="TurnLeft" radius="10" angle="3.14159" bank="15"/>
    <Piece type="Brakes" length="8"/>
  </Layout>
</CoasterLayoutCatalog>)";

        /** @brief 평지 하나와 코스터 하나의 배치입니다. @p pEntryFee 가 입장료입니다. */
        static string makeLayout( const utf8* pEntryFee )
        {
            return string( "<ParkLayout gateX=\"20\" gateZ=\"-40\" startingCash=\"14000\" entryFee=\"" ) + pEntryFee + "\">" +
                   "<FlatRide id=\"cups\" name=\"Spinning Cups\" excitement=\"2\" intensity=\"1\" nausea=\"1\" cycleTime=\"40\" capacity=\"16\" price=\"3\" runningCost=\"3\" cost=\"2000\" "
                   "x=\"45\" z=\"0\" entranceX=\"45\" entranceZ=\"-10\" shape=\"Cylinder\" sizeX=\"6\" sizeY=\"2\" sizeZ=\"6\" color=\"1,1,1,1\"/>"
                   "<Coaster layout=\"family\" x=\"90\" z=\"0\" heading=\"0\" color=\"0.3,0.75,0.4,1\" capacity=\"16\" loadTime=\"15\" cost=\"4000\"/>"
                   "</ParkLayout>";
        }
    };
    using Util = ParkLayoutTestUtil;
} // namespace

/**
 * @brief [ParkLayoutTest] 같은 경로는 공유 캐시의 같은 객체이고, 로더가 같은 팩(배치 종류의 접미를 뗀 곳)의 코스터 레이아웃을 공유 캐시의 한 벌로 쥐며, 다시 읽기는 제자리인지 검증
 * @details 팩은 테스트 도메인(`testproject/park`) 안에 있다 — 같은 팩의 종류 파일은 팩이 어디 있는지(`game/` 접두)가 아니라 두 종류의 접미 규칙으로 찾는다(`GamePackDataUtil::tryMakeSiblingKindPath`).
 */
SW_TEST_CASE( ParkLayoutTest, SharedCacheHoldsTheSamePackCoasterLayouts )
{
    const test::ScopedScratchDomain domain;
    (void)domain.makeDiskFolder( "park/data" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "park/data/coasters.xml" ), Util::kCoasters ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "park/data/rides.xml" ), Util::makeLayout( "5" ) ) );
    const string layoutID  = test::ScopedScratchDomain::makeID( "park/data/rides.xml" ).str();
    const string coasterID = test::ScopedScratchDomain::makeID( "park/data/coasters.xml" ).str();

    const shared_ptr<const ParkLayout> pHeld  = SharedAssetCache<ParkLayout>::acquire( layoutID );
    const shared_ptr<const ParkLayout> pAgain = SharedAssetCache<ParkLayout>::acquire( layoutID );
    SW_REQUIRE_TRUE( pHeld != nullptr );
    SW_EXPECT_TRUE( pHeld.get() == pAgain.get() );
    SW_EXPECT_EQUAL( 5, pHeld->getEntryFee() );
    SW_EXPECT_EQUAL( size_t{ 2 }, pHeld->getPlacements().size() );
    // 배치가 쥔 코스터 레이아웃은 같은 팩 파일의 공유 캐시 한 벌이다(게임 모드가 경로를 따로 적지 않는다).
    SW_REQUIRE_TRUE( pHeld->getCoasterLayouts() != nullptr );
    SW_EXPECT_TRUE( pHeld->getCoasterLayouts().get() == SharedAssetCache<CoasterLayoutCatalog>::acquire( coasterID ).get() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "park/data/rides.xml" ), Util::makeLayout( "9" ) ) );
    SW_REQUIRE_TRUE( SharedAssetCache<ParkLayout>::reloadShared( layoutID ) );
    SW_EXPECT_EQUAL( 9, pHeld->getEntryFee() ); // 제자리 다시 읽기 — 쥔 객체가 새 값을 본다
}
