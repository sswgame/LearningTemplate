#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/FirstPersonLook.h"
#include "GameFramework/Base/FixedStepTimer.h"
#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Base/RayMath.h"
#include "GameFramework/Base/TimingJudge.h"
#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/GameDataXml.h"
#include "GameFramework/Data/ItemBag.h"
#include "GameFramework/Data/StatBlock.h"

#include "TestFramework/TestFramework.h"

// 장르를 가리지 않는 게임 프레임워크 도구 — 결정적 난수 · 좌표 해시, 고정 스텝, 광선 판정, 1인칭 시점, 데이터 XML 읽기, id 카탈로그, 아이템 봉투.

using namespace sw;

namespace
{
    struct GameFrameworkUtilTestDef
    {
        hashed_string _id{};
        int32         _value{ 0 };
    };
} // namespace

/**
 * @brief [GameFrameworkUtilTest] 난수는 씨앗이 같으면 같은 수열이고 범위를 지킨다 · 씨앗 0 은 고정점이라 기본 씨앗이 된다 · 좌표 해시는 순서와 상관없다
 */
SW_TEST_CASE( GameFrameworkUtilTest, RandomIsDeterministicAndBounded )
{
    GameRandom first( 1234u );
    GameRandom second( 1234u );
    GameRandom zero( 0u );
    bool       bSame     = true;
    bool       bInRange  = true;
    int32      arrHit[3] = { 0, 0, 0 };
    for ( int32 drawIndex = 0; drawIndex < 3000; ++drawIndex )
    {
        bSame               = bSame && first.nextUint() == second.nextUint();
        const float32 value = first.nextFloat();
        (void)second.nextFloat();
        bInRange           = bInRange && value >= 0.0f && value < 1.0f;
        const int32 number = first.nextInt( 4, 6 );
        (void)second.nextInt( 4, 6 );
        bInRange = bInRange && number >= 4 && number <= 6;
        if ( number >= 4 && number <= 6 )
            ++arrHit[number - 4];
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bInRange );
    SW_EXPECT_TRUE( arrHit[0] > 800 && arrHit[1] > 800 && arrHit[2] > 800 ); // 양 끝도 고르게 나온다
    SW_EXPECT_EQUAL( GameRandom::kDefaultSeed, zero.getState() );
    SW_EXPECT_EQUAL( 7, first.nextInt( 7, 7 ) );

    constexpr uint32 kHash = GameHash::hashCoord( 3, -5, 99u );
    SW_EXPECT_EQUAL( kHash, GameHash::hashCoord( 3, -5, 99u ) );
    SW_EXPECT_TRUE( GameHash::hashCoord( 3, -5, 99u ) != GameHash::hashCoord( -5, 3, 99u ) );
    SW_EXPECT_TRUE( GameHash::hashCoord( 1, 2, 3, 7u ) != GameHash::hashCoord( 1, 3, 3, 7u ) );
    SW_EXPECT_TRUE( GameHash::toUnitFloat( 0xffffffu ) == 1.0f );
}

/**
 * @brief [GameFrameworkUtilTest] 고정 스텝은 프레임을 같은 걸음으로 쪼개고 남은 시간을 넘기며, 한 프레임에 받는 시간을 자른다
 */
SW_TEST_CASE( GameFrameworkUtilTest, FixedStepTimerCarriesRemainderAndClampsFrames )
{
    FixedStepTimer timer( 0.1f, 0.25f );
    SW_EXPECT_EQUAL( 0, timer.consume( 0.05f ) );
    SW_EXPECT_EQUAL( 1, timer.consume( 0.06f ) ); // 0.11 — 한 걸음, 0.01 남는다
    SW_EXPECT_NEAR_EQUAL( 0.1f, timer.getAlpha(), 1.0e-3f );
    SW_EXPECT_EQUAL( 2, timer.consume( 10.0f ) ); // 0.25 로 잘린다 → 0.26
    SW_EXPECT_EQUAL( 0, timer.consume( -1.0f ) );
    timer.reset();
    SW_EXPECT_NEAR_EQUAL( 0.0f, timer.getAlpha(), 1.0e-6f );

    // 프레임을 어떻게 쪼개도 걸음 수의 합은 같다.
    FixedStepTimer coarse( 1.0f / 60.0f, 0.25f );
    FixedStepTimer fine( 1.0f / 60.0f, 0.25f );
    int32          coarseSteps = 0;
    int32          fineSteps   = 0;
    for ( int32 frameIndex = 0; frameIndex < 30; ++frameIndex )
        coarseSteps += coarse.consume( 1.0f / 30.0f );
    for ( int32 frameIndex = 0; frameIndex < 144; ++frameIndex )
        fineSteps += fine.consume( 1.0f / 144.0f );
    SW_EXPECT_TRUE( MathUtil::abs( coarseSteps - fineSteps ) <= 1 );
}

/**
 * @brief [GameFrameworkUtilTest] 바닥 평면 판정은 앞쪽만 · 1인칭 시점의 이동 방향은 길이 1 을 넘지 않고 카메라 오일러는 피치가 뒤집힌다
 */
SW_TEST_CASE( GameFrameworkUtilTest, RayPlaneAndFirstPersonMoveDirection )
{
    GameRay ray;
    ray._origin      = float3{ 0.0f, 2.0f, 0.0f };
    ray._direction   = float3{ 0.0f, -0.6f, 0.8f };
    float32 distance = 0.0f;
    SW_ASSERT_TRUE( RayMath::intersectHorizontalPlane( ray, 0.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f / 0.6f, distance, 1.0e-4f );
    SW_EXPECT_FALSE( RayMath::intersectHorizontalPlane( ray, 5.0f, 100.0f, distance ) ); // 뒤쪽
    SW_EXPECT_FALSE( RayMath::intersectHorizontalPlane( ray, 0.0f, 1.0f, distance ) );   // 사거리 밖

    FirstPersonLook look;
    look.setAngles( 0.0f, 0.3f );
    const float3 diagonal = look.computeMoveDirection( 1.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, diagonal.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, diagonal._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( -0.3f, look.computeCameraEuler()._x, 1.0e-6f );
    look.setMaxPitch( 0.2f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, look.getPitch(), 1.0e-6f );
}

/**
 * @brief [GameFrameworkUtilTest] 데이터 XML — 루트 확인 · id 없는 원소 거르기 · 숫자 목록(빠진 성분은 기본값) · 빈 토큰을 건너뛰는 토큰 목록
 */
SW_TEST_CASE( GameFrameworkUtilTest, DataXmlReadsRootsIdsNumbersAndTokens )
{
    XmlDocument doc;
    XmlNode     root;
    SW_EXPECT_FALSE( GameDataXml::parseRoot( doc, "<Other/>", "GameFrameworkUtilTest", "Catalog", root ) );
    SW_ASSERT_TRUE( GameDataXml::parseRoot( doc, R"(<Catalog><Item id="a"/><Item name="NoId"/></Catalog>)", "GameFrameworkUtilTest", "Catalog", root ) );
    int32 idCount = 0;
    for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
        idCount += GameDataXml::findRequiredId( node, "GameFrameworkUtilTest" ) != nullptr ? 1 : 0;
    SW_EXPECT_EQUAL( 1, idCount );

    const float4 color = GameDataXml::parseFloat4( "0.5, 0.25  1", float4{ 9.0f, 9.0f, 9.0f, 0.75f } );
    SW_EXPECT_NEAR_EQUAL( 0.5f, color._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, color._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, color._w, 1.0e-6f );
    float32 arrValue[2] = { -1.0f, -1.0f };
    SW_EXPECT_EQUAL( 1u, GameDataXml::parseFloats( "x;3", arrValue, 2 ) ); // 못 읽은 칸은 그대로
    SW_EXPECT_NEAR_EQUAL( -1.0f, arrValue[0], 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, arrValue[1], 1.0e-6f );

    int32 tokenCount = 0;
    GameDataXml::forEachToken( ",Spring,, Fall ;", ",; ", [&]( string_view token )
    {
        ++tokenCount;
        SW_EXPECT_FALSE( token.empty() );
    } );
    SW_EXPECT_EQUAL( 2, tokenCount );
}

/**
 * @brief [GameFrameworkUtilTest] 카탈로그는 읽은 순서를 지키고 같은 id 는 그 자리에서 바꾸며 빈 id 는 받지 않는다 · 아이템 봉투는 0 이 되면 지우고 모자라면 옮기지 않는다
 */
SW_TEST_CASE( GameFrameworkUtilTest, CatalogKeepsOrderAndItemBagMovesItems )
{
    GameCatalog<GameFrameworkUtilTestDef> catalog;
    SW_EXPECT_EQUAL( 0, catalog.add( GameFrameworkUtilTestDef{ hashed_string( "b" ), 1 } ) );
    SW_EXPECT_EQUAL( 1, catalog.add( GameFrameworkUtilTestDef{ hashed_string( "a" ), 2 } ) );
    SW_EXPECT_EQUAL( 0, catalog.add( GameFrameworkUtilTestDef{ hashed_string( "B" ), 3 } ) ); // 이름은 대소문자를 가리지 않는다
    SW_EXPECT_EQUAL( -1, catalog.add( GameFrameworkUtilTestDef{} ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), catalog.getCount() );
    SW_ASSERT_NOT_NULL( catalog.find( "b" ) );
    SW_EXPECT_EQUAL( 3, catalog.find( "b" )->_value );
    SW_EXPECT_TRUE( catalog.find( "c" ) == nullptr );
    SW_EXPECT_EQUAL( 1, catalog.findIndex( "a" ) );
    SW_EXPECT_EQUAL( 2, catalog.findIf( []( const GameFrameworkUtilTestDef& def )
    { return def._value == 2; } )
                            ->_value );

    ItemBag bag;
    ItemBag box;
    bag.addItem( "apple", 3 );
    bag.addItem( "apple", 0 );
    bag.addItem( hashed_string{}, 5 );
    SW_EXPECT_EQUAL( 3, bag.getTotalCount() );
    SW_EXPECT_FALSE( bag.moveItemTo( box, "apple", 4 ) );
    SW_EXPECT_TRUE( bag.moveItemTo( box, "apple", 3 ) );
    SW_EXPECT_TRUE( bag.isEmpty() );
    SW_EXPECT_TRUE( box.hasItem( "apple", 3 ) );
    SW_EXPECT_FALSE( box.moveItemTo( box, "apple", 1 ) );
}

SW_TEST_CASE( GameFrameworkUtilTest, TimingJudgeGradesNarrowestWindowFirstWithLatencyAndScale )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( R"(<TimingWindows>
        <Window grade="Bad" width="0.15" score="10" breaksCombo="true"/>
        <Window grade="Cool" early="0.03" late="0.04" score="300"/>
        <Window grade="Good" width="0.08" score="100"/>
      </TimingWindows>)",
                                           "GameFrameworkUtilTest" ) );
    SW_EXPECT_TRUE( judge.getWindows().front()._grade == hashed_string( "Cool" ) );              // 좁은 것부터
    SW_EXPECT_TRUE( judge.judge( 10.0f, 10.035f )._pWindow->_grade == hashed_string( "Cool" ) ); // 늦은 쪽이 넓다
    SW_EXPECT_TRUE( judge.judge( 10.0f, 9.965f )._pWindow->_grade == hashed_string( "Good" ) );
    const TimingResult bad = judge.judge( 10.0f, 9.9f );
    SW_EXPECT_TRUE( bad.isHit() && bad.isEarly() && bad._pWindow->_bBreaksCombo != SW_FALSE );
    SW_EXPECT_FALSE( judge.judge( 10.0f, 10.2f ).isHit() );
    SW_EXPECT_FALSE( judge.hasExpired( 10.0f, 10.14f ) );
    SW_EXPECT_TRUE( judge.hasExpired( 10.0f, 10.16f ) );
    SW_EXPECT_NEAR_EQUAL( 0.15f, judge.getEarliestWidth(), 1.0e-5f );

    // 입력 지연 보정 · 창 넓히기.
    judge.setOffset( 0.05f );
    SW_EXPECT_TRUE( judge.judge( 10.0f, 10.05f )._pWindow->_grade == hashed_string( "Cool" ) );
    judge.setOffset( 0.0f );
    judge.setScale( 2.0f );
    SW_EXPECT_TRUE( judge.judge( 10.0f, 10.07f )._pWindow->_grade == hashed_string( "Cool" ) );
}

SW_TEST_CASE( GameFrameworkUtilTest, StatBlockReadsAttributesAndMerges )
{
    XmlDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"(<Stats id="x" attack="5" speed="-0.5" label="fast"/>)" ) );
    StatBlock stats;
    SW_EXPECT_EQUAL( 2, static_cast<int32>( stats.loadFromAttributes( doc.getRoot(), "id" ) ) ); // 숫자가 아닌 label 은 건너뛴다
    SW_EXPECT_NEAR_EQUAL( -0.5f, stats.getValue( hashed_string( "speed" ) ), 1.0e-5f );
    StatBlock bonus;
    bonus.setValue( hashed_string( "attack" ), 2.0f );
    bonus.setValue( hashed_string( "luck" ), 1.0f );
    stats.merge( bonus, 3.0f );
    SW_EXPECT_NEAR_EQUAL( 11.0f, stats.getValue( hashed_string( "attack" ) ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, stats.getValue( hashed_string( "luck" ) ), 1.0e-5f );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( stats.getCount() ) );
    SW_EXPECT_FALSE( stats.hasValue( hashed_string( "label" ) ) );
}

SW_TEST_CASE( GameFrameworkUtilTest, WeightedPickShuffleConeAndCosts )
{
    // 가중치 — 0 · 음수는 뽑히지 않고, 비율은 가중치를 따른다. 같은 씨앗이면 같은 순서.
    struct Entry
    {
        float32 _weight;
    };
    const vector<Entry> listEntry{ { 1.0f }, { 0.0f }, { 3.0f }, { -2.0f } };
    GameRandom          random( 7u );
    int32               arrCount[4] = { 0, 0, 0, 0 };
    for ( int32 roll = 0; roll < 4000; ++roll )
    {
        const int32 index = random.pickWeightedIndex( listEntry, []( const Entry& entry )
        { return entry._weight; } );
        SW_ASSERT_TRUE( index >= 0 && index < 4 );
        ++arrCount[index];
    }
    SW_EXPECT_EQUAL( 0, arrCount[1] );
    SW_EXPECT_EQUAL( 0, arrCount[3] );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( arrCount[2] ) / static_cast<float32>( arrCount[0] ), 3.0f, 0.4f );
    const vector<Entry> listEmpty{ { 0.0f } };
    SW_EXPECT_EQUAL( -1, random.pickWeightedIndex( listEmpty, []( const Entry& entry )
    { return entry._weight; } ) );

    vector<int32> listFirst{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    vector<int32> listSecond = listFirst;
    GameRandom    firstRandom( 99u );
    GameRandom    secondRandom( 99u );
    firstRandom.shuffle( listFirst );
    secondRandom.shuffle( listSecond );
    SW_EXPECT_TRUE( listFirst == listSecond );
    int32 sum   = 0;
    int32 moved = 0;
    for ( int32 index = 0; index < 10; ++index )
    {
        sum += listFirst[static_cast<size_t>( index )];
        moved += listFirst[static_cast<size_t>( index )] != index ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 45, sum ); // 같은 원소들
    SW_EXPECT_TRUE( moved > 0 );

    // 원뿔 — 반각 30 도 · 10 m.
    const float3 origin{};
    const float3 forward{ 0.0f, 0.0f, 2.0f }; // 단위가 아니어도 된다
    SW_EXPECT_TRUE( RayMath::isInCone( origin, forward, 30.0f, 10.0f, float3{ 0.0f, 0.0f, 5.0f } ) );
    SW_EXPECT_TRUE( RayMath::isInCone( origin, forward, 30.0f, 10.0f, float3{ 2.0f, 0.0f, 5.0f } ) );     // 약 22 도
    SW_EXPECT_FALSE( RayMath::isInCone( origin, forward, 30.0f, 10.0f, float3{ 4.0f, 0.0f, 5.0f } ) );    // 약 39 도
    SW_EXPECT_FALSE( RayMath::isInCone( origin, forward, 30.0f, 10.0f, float3{ 0.0f, 0.0f, 11.0f } ) );   // 멀다
    SW_EXPECT_FALSE( RayMath::isInCone( origin, forward, 30.0f, 10.0f, float3{ 0.0f, 4.0f, 5.0f } ) );    // 위로 39 도
    SW_EXPECT_TRUE( RayMath::isInFlatCone( origin, forward, 30.0f, 10.0f, float3{ 0.0f, 4.0f, 5.0f } ) ); // 높이는 무시

    // 여러 자원 비용 — 다 되거나 아무것도.
    StatBlock wallet;
    wallet.setValue( hashed_string( "Wood" ), 50.0f );
    wallet.setValue( hashed_string( "Gold" ), 10.0f );
    StatBlock cost;
    cost.setValue( hashed_string( "Wood" ), 30.0f );
    cost.setValue( hashed_string( "Gold" ), 20.0f );
    SW_EXPECT_FALSE( wallet.canAfford( cost ) );
    SW_EXPECT_FALSE( wallet.trySpend( cost ) );
    SW_EXPECT_NEAR_EQUAL( wallet.getValue( hashed_string( "Wood" ) ), 50.0f, 0.001f );
    cost.setValue( hashed_string( "Gold" ), 10.0f );
    SW_EXPECT_TRUE( wallet.trySpend( cost ) );
    SW_EXPECT_NEAR_EQUAL( wallet.getValue( hashed_string( "Wood" ) ), 20.0f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( wallet.getValue( hashed_string( "Gold" ) ), 0.0f, 0.001f );
}
