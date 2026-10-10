#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Actor/Input/FirstPersonLook.h"
#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Foundation/Utility/Time/LifeSpanUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

#include "TestFramework/TestFramework.h"

// 장르를 가리지 않는 게임 프레임워크 도구 — 결정적 난수 · 좌표 해시, 고정 스텝, 남은 시간 · 비율 누적, 광선 판정, 1인칭 시점, 데이터 XML 읽기,
// id 카탈로그, 아이템 값 목록.

using namespace sw;

namespace
{
    struct GameFrameworkUtilTestDef
    {
        hashed_string _id{};
        int32         _value{ 0 };
    };

    /** @brief `XmlCatalog` 를 물려받아 루트 이름 · 비공개 루트 읽기만 둔 카탈로그입니다. */
    class GameFrameworkUtilTestCatalog : public XmlCatalog<GameFrameworkUtilTestCatalog>
    {
        friend class XmlCatalog<GameFrameworkUtilTestCatalog>;

    public:
        int32 getItemCount() const { return _itemCount; }

    private:
        static constexpr const utf8* kXmlRootName = "Catalog";
        uint32                       loadRoot( const XmlNode& root, string_view sourceName )
        {
            (void)sourceName;
            _itemCount = 0;
            for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
            {
                ++_itemCount;
            }
            return static_cast<uint32>( _itemCount );
        }

        int32 _itemCount{ 0 };
    };

    /** @brief 고정 dt 로 @p seconds 동안 끝날 때마다 다시 걸어 낸 횟수입니다(게임 루프 순서 — 흘리고, 끝났으면 내고 다시 건다). */
    int32 countRepeatsFor( float32 interval, float32 framesPerSecond, float32 seconds )
    {
        Countdown     cooldown;
        const float32 deltaTime  = 1.0f / framesPerSecond;
        const int32   frameCount = static_cast<int32>( seconds * framesPerSecond + 0.5f );
        int32         count      = 0;
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            cooldown.tick( deltaTime );
            if ( cooldown.isActive() )
                continue;
            ++count;
            cooldown.restart( interval );
        }
        return count;
    }

    /** @brief @p start 에서 @p blocked 한 칸만 막은 격자를 네 이웃 너비 우선으로 훑습니다. */
    void runGridSearch( GridSearchScratch& outSearch, const GridTopology& topology, const int2& start, const int2& blocked )
    {
        outSearch.begin( topology.getCellCount() );
        outSearch.visit( topology.toIndex( start ), -1 );
        while ( outSearch.hasNext() )
        {
            const int32 index = outSearch.popNext();
            for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
            {
                const int2 next  = GridTopology::getNeighbor( topology.toCell( index ), direction );
                const bool bOpen = topology.isInside( next ) && ( next._x != blocked._x || next._y != blocked._y );
                if ( bOpen )
                    outSearch.visit( topology.toIndex( next ), index );
            }
        }
    }
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
    {
        coarseSteps += coarse.consume( 1.0f / 30.0f );
    }
    for ( int32 frameIndex = 0; frameIndex < 144; ++frameIndex )
    {
        fineSteps += fine.consume( 1.0f / 144.0f );
    }
    SW_EXPECT_TRUE( MathUtil::abs( coarseSteps - fineSteps ) <= 1 );
}

/**
 * @brief [GameFrameworkUtilTest] 남은 시간은 끝난 걸음에 한 번 알리고, 그 걸음에 다시 걸면 지나친 몫(늦음)을 한 간격까지 다음 간격에서 뺀다
 * @details 쉬다가 다시 걸면 늦음을 잇지 않는다(다음 `tick` 이 지운다). 0 이하 걸음은 아무것도 하지 않는다. `extendTo` 는 더 길 때만 늘린다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, CountdownEndsOnceAndRestartCarriesOneIntervalOfLateness )
{
    Countdown countdown;
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_FALSE( countdown.tick( 0.1f ) ); // 꺼진 채 흘려도 끝나지 않는다
    countdown.start( 1.0f );
    SW_EXPECT_FALSE( countdown.tick( 0.6f ) );
    SW_EXPECT_TRUE( countdown.isActive() );
    SW_EXPECT_NEAR_EQUAL( 0.4f, countdown.getRemaining(), 1.0e-5f );
    SW_EXPECT_FALSE( countdown.tick( 0.0f ) ); // 0 걸음은 그대로
    SW_EXPECT_TRUE( countdown.tick( 0.6f ) );  // 이번 걸음에 끝났다 — 0.2 지나쳤다
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.restart( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, countdown.getRemaining(), 1.0e-5f ); // 늦은 0.2 를 뺐다
    SW_EXPECT_FALSE( countdown.tick( 0.7f ) );
    SW_EXPECT_TRUE( countdown.tick( 0.7f ) );
    SW_EXPECT_FALSE( countdown.tick( 0.1f ) ); // 다시 걸지 않았다 — 늦음이 지워지고 다시 알리지 않는다
    countdown.restart( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, countdown.getRemaining(), 1.0e-6f ); // 쉰 시간은 잇지 않는다

    // 걸음이 간격보다 길면 잇는 몫은 한 간격까지 — 결과가 0 이어도 걸음마다 한 번이지 몰아 내지 않는다.
    countdown.start( 0.1f );
    SW_EXPECT_TRUE( countdown.tick( 0.5f ) );
    countdown.restart( 0.25f );
    SW_EXPECT_FALSE( countdown.isActive() );
    SW_EXPECT_FALSE( countdown.tick( 0.5f ) );
    SW_EXPECT_FALSE( countdown.isActive() );

    // 아직 남아 있을 때 다시 걸면 새로 거는 것과 같다.
    countdown.start( 0.5f );
    countdown.restart( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.extendTo( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.extendTo( 3.0f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, countdown.getRemaining(), 1.0e-6f );
    countdown.clear();
    SW_EXPECT_FALSE( countdown.isActive() );
}

/**
 * @brief [GameFrameworkUtilTest] 끝난 걸음에 `restart` 로 다시 거는 반복은 프레임률과 상관없이 설계 빈도(시간 / 간격)를 낸다
 * @details 0.095 초 간격을 30 · 60 · 144 fps 로 10 초 — 설계 105.26 회 ±1. 늦음을 버리면(`start`) 간격이 `ceil( 간격 / dt ) × dt` 로 늘어
 *          30 · 60 fps 에서 100 회가 된다. 간격보다 긴 프레임(5 fps)은 프레임마다 한 번 — 50 회이고 몰아 내지 않는다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, CountdownRepeatRateDoesNotDependOnFrameRate )
{
    const float32 interval = 0.095f;
    const float32 seconds  = 10.0f;
    for ( const float32 framesPerSecond : { 30.0f, 60.0f, 144.0f } )
    {
        SW_EXPECT_NEAR_EQUAL( seconds / interval, static_cast<float32>( countRepeatsFor( interval, framesPerSecond, seconds ) ), 1.0f );
    }
    SW_EXPECT_EQUAL( 50, countRepeatsFor( interval, 5.0f, seconds ) );
}

/**
 * @brief [GameFrameworkUtilTest] `tickRepeat` — 누르고 있는 동안 간격마다 한 번, 지나친 몫을 이어 빈도가 걸음 크기에 매이지 않고, 쉬면 늦음을 버린다
 * @details 시험 게임 둘이 이 줄을 쓴다 — VoxelCraft 블록 놓기(0.25 초 간격, 걸음 0.1 초 상한)와 Shooter3D 적 휘두르기(1.5 초). 손으로 세던 판
 *          (`x -= dt; if ( want && x <= 0 ) x = interval;`)은 0.1 초 걸음에서 놓기 간격을 0.3 초로 늘렸다 — 10 초 누르면 41 이 아니라 34 번.
 */
SW_TEST_CASE( GameFrameworkUtilTest, CountdownTickRepeatKeepsTheHeldRate )
{
    BLOCK( "VoxelCraft 블록 놓기 — 0.25 초 간격을 0.1 초 걸음 · 60 fps 로 10 초 누르고 있으면 첫 누름 + 40 번" )
    {
        for ( const float32 deltaTime : { 0.1f, 1.0f / 60.0f } )
        {
            Countdown   placeCooldown;
            const int32 stepCount  = static_cast<int32>( 10.0f / deltaTime + 0.5f );
            int32       placeCount = 0;
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                placeCount += placeCooldown.tickRepeat( deltaTime, 0.25f, true ) ? 1 : 0;
            }
            SW_EXPECT_NEAR_EQUAL( 41.0f, static_cast<float32>( placeCount ), 1.0f );
        }
    }
    BLOCK( "Shooter3D 적 휘두르기 — 사정거리 밖에서 쉰 시간은 잇지 않는다(들어오면 한 번, 몰아 휘두르지 않는다)" )
    {
        Countdown swingCooldown;
        swingCooldown.start( 0.75f );
        int32 swingCount = 0;
        for ( int32 stepIndex = 0; stepIndex < 50; ++stepIndex ) // 5 초 사정거리 밖
        {
            swingCount += swingCooldown.tickRepeat( 0.1f, 1.5f, false ) ? 1 : 0;
        }
        SW_EXPECT_EQUAL( 0, swingCount );
        SW_EXPECT_TRUE( swingCooldown.tickRepeat( 0.1f, 1.5f, true ) ); // 들어온 걸음에 한 번
        SW_EXPECT_FALSE( swingCooldown.tickRepeat( 0.1f, 1.5f, true ) );
        SW_EXPECT_NEAR_EQUAL( 1.4f, swingCooldown.getRemaining(), 1.0e-4f );
    }
}

/**
 * @brief [GameFrameworkUtilTest] 수명은 흐른 시간이 수명 이상인 걸음부터 다했고, 흐림은 1 에서 0 으로 내려가 0 아래로 가지 않는다 — 수명 0 은 끝없음
 */
SW_TEST_CASE( GameFrameworkUtilTest, LifeSpanEndsAtItsLifeTimeAndFadesFromOneToZero )
{
    float32 elapsed = 0.0f;
    SW_EXPECT_FALSE( LifeSpanUtil::advance( elapsed, 1.0f, 0.5f ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, LifeSpanUtil::computeFade( elapsed, 1.0f ), 1.0e-6f );
    SW_EXPECT_TRUE( LifeSpanUtil::advance( elapsed, 1.0f, 0.5f ) ); // 정확히 수명 — 이 걸음에 다했다
    SW_EXPECT_NEAR_EQUAL( 0.0f, LifeSpanUtil::computeFade( elapsed, 1.0f ), 1.0e-6f );
    SW_EXPECT_TRUE( LifeSpanUtil::advance( elapsed, 1.0f, 0.5f ) ); // 다한 뒤로도 다했다
    SW_EXPECT_NEAR_EQUAL( 0.0f, LifeSpanUtil::computeFade( elapsed, 1.0f ), 1.0e-6f );

    float32 kept = 0.0f;
    SW_EXPECT_FALSE( LifeSpanUtil::advance( kept, 0.0f, 100.0f ) ); // 수명 0 — 지우지 않는 견본
    SW_EXPECT_NEAR_EQUAL( 1.0f, LifeSpanUtil::computeFade( kept, 0.0f ), 1.0e-6f );
}

/**
 * @brief [GameFrameworkUtilTest] 비율 누적기는 분수 몫을 다음 걸음으로 넘겨 긴 시간의 합이 비율 × 시간이 된다 — 하나씩 · 정수 부분 통째로
 */
SW_TEST_CASE( GameFrameworkUtilTest, RateAccumulatorCarriesTheFractionAcrossSteps )
{
    RateAccumulator arrival;
    int32           arrivalCount = 0;
    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        arrival.add( 0.25f ); // 2 진수로 정확한 몫 — 합이 정확히 25
        while ( arrival.takeOne() )
        {
            ++arrivalCount;
        }
    }
    SW_EXPECT_EQUAL( 25, arrivalCount );
    SW_EXPECT_TRUE( 0.0f <= arrival.getFraction() && arrival.getFraction() < 1.0f );

    RateAccumulator cost;
    int32           costTotal = 0;
    for ( int32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        cost.add( 2.5f / 60.0f * 7.0f ); // 초당 2.5 를 7 초짜리 걸음 60 번
        costTotal += cost.takeWhole();
    }
    SW_EXPECT_TRUE( MathUtil::abs( costTotal - 17 ) <= 1 );
    cost.reset();
    SW_EXPECT_NEAR_EQUAL( 0.0f, cost.getFraction(), 1.0e-6f );
}

/**
 * @brief [GameFrameworkUtilTest] 알림 버퍼는 빈 목록에 저장소를 맞바꿔 넘기고(복사 없음 — 두 버퍼가 번갈아 돈다), 찬 목록에는 뒤에 붙인다
 */
SW_TEST_CASE( GameFrameworkUtilTest, EventBufferSwapsIntoAnEmptyListAndAppendsToAFullOne )
{
    EventBuffer<int32> buffer;
    vector<int32>      listEvent;
    listEvent.reserve( 8 );
    const int32* pReceiving = listEvent.data();
    buffer.push( 1 );
    buffer.push( 2 );
    buffer.getLast() = 3; // 쌓은 뒤 칸을 고친다
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), buffer.getCount() );
    const int32* pPending = buffer.getPending().data();

    // 빈 목록 — 맞바꾼다. 받는 쪽은 쌓던 저장소를, 쌓는 쪽은 받는 쪽이 쓰던(비운) 저장소를 갖는다.
    buffer.drainTo( listEvent );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listEvent.size() );
    SW_EXPECT_EQUAL( 1, listEvent[0] );
    SW_EXPECT_EQUAL( 3, listEvent[1] );
    SW_EXPECT_TRUE( listEvent.data() == pPending );
    SW_EXPECT_TRUE( buffer.isEmpty() );
    buffer.push( 4 );
    SW_EXPECT_TRUE( buffer.getPending().data() == pReceiving );

    // 찬 목록 — 뒤에 붙이고 비운다(여러 곳의 알림을 한 목록에 모으는 쪽).
    buffer.drainTo( listEvent );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listEvent.size() );
    SW_EXPECT_EQUAL( 4, listEvent[2] );
    SW_EXPECT_TRUE( buffer.isEmpty() );

    buffer.push( 5 );
    buffer.clear();
    vector<int32> listOther;
    buffer.drainTo( listOther );
    SW_EXPECT_TRUE( listOther.empty() );
}

/**
 * @brief [GameFrameworkUtilTest] 격자 모양은 칸 번호 · 경계 · 이웃 순서(직교 넷 → 대각선 넷)가 하나이고, 탐색 스크래치는 세대만 올려 다시 쓴다
 * @details 너비 우선 결과(거리 · 부모 · 방문 순서)는 이웃 순서에 매인다. 두 번째 탐색은 첫 탐색의 표시를 보지 않고, 저장소를 다시 잡지 않는다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, GridTopologyAndSearchScratchReuseTheirStorage )
{
    const GridTopology topology{ 4, 3 };
    SW_EXPECT_EQUAL( 12, topology.getCellCount() );
    SW_EXPECT_EQUAL( 6, topology.toIndex( int2{ 2, 1 } ) );
    SW_EXPECT_TRUE( topology.toCell( 6 ) == int2( 2, 1 ) );
    SW_EXPECT_TRUE( topology.isInside( int2{ 3, 2 } ) );
    SW_EXPECT_FALSE( topology.isInside( int2{ 4, 0 } ) );
    SW_EXPECT_FALSE( topology.isInside( -1, 0 ) );
    SW_EXPECT_TRUE( GridTopology::getNeighbor( int2{ 1, 1 }, 0 ) == int2( 2, 1 ) );
    SW_EXPECT_TRUE( GridTopology::getNeighbor( int2{ 1, 1 }, 3 ) == int2( 1, 0 ) );
    SW_EXPECT_TRUE( GridTopology::getNeighbor( int2{ 1, 1 }, 7 ) == int2( 0, 0 ) );

    // 가운데 줄의 (1,1) 을 막은 4 × 3 — (0,0) 에서 (3,2) 까지 너비 우선.
    GridSearchScratch search;
    runGridSearch( search, topology, int2{ 0, 0 }, int2{ 1, 1 } );
    SW_EXPECT_EQUAL( static_cast<size_t>( 11 ), search.getVisitOrder().size() );
    SW_EXPECT_FALSE( search.isVisited( topology.toIndex( int2{ 1, 1 } ) ) );
    int32 pathLength = 0;
    for ( int32 index = topology.toIndex( int2{ 3, 2 } ); index >= 0; index = search.getParent( index ) )
    {
        ++pathLength;
    }
    SW_EXPECT_EQUAL( 6, pathLength );                // 맨해튼 5 걸음 + 시작 칸
    SW_EXPECT_EQUAL( 1, search.getVisitOrder()[1] ); // 첫 이웃은 +x 쪽

    // 다시 — 지난 표시는 사라지고 저장소는 그대로다.
    const int32* pOrder = search.getVisitOrder().data();
    runGridSearch( search, topology, int2{ 3, 2 }, int2{ 2, 2 } );
    SW_EXPECT_TRUE( search.isVisited( topology.toIndex( int2{ 1, 1 } ) ) );
    SW_EXPECT_FALSE( search.isVisited( topology.toIndex( int2{ 2, 2 } ) ) );
    SW_EXPECT_EQUAL( -1, search.getParent( topology.toIndex( int2{ 3, 2 } ) ) );
    SW_EXPECT_TRUE( search.getVisitOrder().data() == pOrder );
}

/**
 * @brief [GameFrameworkUtilTest] (x, y) 칸 번호와 사각형(발자국) 경계
 * @details 키트의 칸 저장소(CreatureTown · FarmField · CitySimulation …)가 `y × 너비 + x` · 발자국 경계를 손으로 적지 않고 이 둘을 쓴다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, GridTopologyXyIndexAndRectBounds )
{
    const GridTopology topology{ 4, 3 };
    SW_EXPECT_EQUAL( topology.toIndex( int2{ 2, 1 } ), topology.toIndex( 2, 1 ) );
    SW_EXPECT_EQUAL( 11, topology.toIndex( 3, 2 ) );

    SW_EXPECT_TRUE( topology.isRectInside( int2{ 0, 0 }, int2{ 4, 3 } ) );  // 격자 전체
    SW_EXPECT_TRUE( topology.isRectInside( int2{ 2, 1 }, int2{ 2, 2 } ) );  // 오른쪽 위 끝에 붙은 2 × 2
    SW_EXPECT_FALSE( topology.isRectInside( int2{ 3, 1 }, int2{ 2, 1 } ) ); // x 4 칸이 밖
    SW_EXPECT_FALSE( topology.isRectInside( int2{ 0, 2 }, int2{ 1, 2 } ) ); // y 3 칸이 밖
    SW_EXPECT_FALSE( topology.isRectInside( int2{ -1, 0 }, int2{ 1, 1 } ) );
    SW_EXPECT_FALSE( topology.isRectInside( int2{ 0, -1 }, int2{ 1, 1 } ) );
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
SW_TEST_CASE( GameFrameworkUtilTest, DataXmlReadsRootsIDsNumbersAndTokens )
{
    XmlDocument doc;
    XmlNode     root;
    SW_EXPECT_FALSE( GameDataXml::parseRoot( doc, "<Other/>", "GameFrameworkUtilTest", "Catalog", root ) );
    SW_ASSERT_TRUE( GameDataXml::parseRoot( doc, R"(<Catalog><Item id="a"/><Item name="NoId"/></Catalog>)", "GameFrameworkUtilTest", "Catalog", root ) );
    int32 idCount = 0;
    for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
    {
        idCount += GameDataXml::findRequiredID( node, "GameFrameworkUtilTest" ) != nullptr ? 1 : 0;
    }
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
 * @brief [GameFrameworkUtilTest] 카탈로그 로더 템플릿(`GameDataXml::loadText` · `loadFile`)은 루트를 카탈로그의 비공개 루트 읽기에 넘기고, 루트가 없거나
 *        읽은 수가 0(또는 false)이면 실패다 — 다른 카탈로그를 함께 넘기는 판도 같다
 * @details 카탈로그 마흔여섯이 같던 문서 · 루트 · 경고 몸통을 이 한 곳이 맡는다. 0 을 성공으로 넘기면 원소 이름을 틀린 파일이 텅 빈 카탈로그가 된다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, CatalogLoaderTemplateHandsTheRootToThePrivateReader )
{
    class CountingCatalog
    {
    public:
        bool loadFromXmlText( string_view xmlText ) { return GameDataXml::loadText( *this, &CountingCatalog::loadRoot, xmlText, "CountingCatalog", "Catalog" ); }
        bool loadWithBonus( string_view xmlText, const int32& bonus )
        {
            return GameDataXml::loadText( *this, &CountingCatalog::loadRootWithBonus, bonus, xmlText, "CountingCatalog", "Catalog" );
        }
        bool loadFromResource( string_view path ) { return GameDataXml::loadFile( *this, &CountingCatalog::loadRoot, path, "Catalog" ); }

        int32 _itemCount{ 0 };

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName )
        {
            (void)sourceName;
            _itemCount = 0;
            for ( XmlNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )
            {
                ++_itemCount;
            }
            return static_cast<uint32>( _itemCount );
        }
        bool loadRootWithBonus( const XmlNode& root, const int32& bonus, string_view sourceName )
        {
            const bool bLoaded = loadRoot( root, sourceName ) > 0;
            _itemCount += bonus;
            return bLoaded;
        }
    };

    CountingCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXmlText( "<Catalog><Item/><Item/></Catalog>" ) );
    SW_EXPECT_EQUAL( 2, catalog._itemCount );
    {
        test::ScopedDefensiveTestLog expected( "catalog root missing, empty catalog and missing file" );
        SW_EXPECT_FALSE( catalog.loadFromXmlText( "<Catalog><Thing/></Catalog>" ) ); // 읽은 것이 0
        SW_EXPECT_FALSE( catalog.loadFromXmlText( "<Other><Item/></Other>" ) );      // 루트가 없다
        SW_EXPECT_FALSE( catalog.loadFromResource( "game/none/no_such_catalog.xml" ) );
    }
    SW_EXPECT_TRUE( catalog.loadWithBonus( "<Catalog><Item/></Catalog>", 10 ) );
    SW_EXPECT_EQUAL( 11, catalog._itemCount );
}

/**
 * @brief [GameFrameworkUtilTest] `XmlCatalog` 를 물려받은 카탈로그는 루트 이름 · 비공개 루트 읽기만으로 두 공개 창구를 얻고, 실패 규칙은 로더 템플릿과 같다
 */
SW_TEST_CASE( GameFrameworkUtilTest, XmlCatalogBaseGivesBothLoadEntryPoints )
{
    GameFrameworkUtilTestCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXmlText( "<Catalog><Item/><Item/><Item/></Catalog>", "GameFrameworkUtilTest" ) );
    SW_EXPECT_EQUAL( 3, catalog.getItemCount() );
    {
        test::ScopedDefensiveTestLog expected( "catalog base: empty catalog, wrong root and missing file" );
        SW_EXPECT_FALSE( catalog.loadFromXmlText( "<Catalog><Thing/></Catalog>" ) );
        SW_EXPECT_FALSE( catalog.loadFromXmlText( "<Other><Item/></Other>" ) );
        SW_EXPECT_FALSE( catalog.loadFromResource( "game/none/no_such_catalog.xml" ) );
    }
}

/**
 * @brief [GameFrameworkUtilTest] 카탈로그는 읽은 순서를 지키고 같은 id 는 그 자리에서 바꾸며 빈 id 는 받지 않는다 · 아이템 값 목록은 0 이 되면 지우고 모자라면 옮기지 않는다
 */
SW_TEST_CASE( GameFrameworkUtilTest, CatalogKeepsOrderAndItemStackListMovesItems )
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

    ItemStackList items;
    ItemStackList box;
    items.addItem( "apple", 3 );
    items.addItem( "apple", 0 );
    items.addItem( hashed_string{}, 5 );
    SW_EXPECT_EQUAL( 3, items.getTotalCount() );
    SW_EXPECT_FALSE( items.moveItemTo( box, "apple", 4 ) );
    SW_EXPECT_TRUE( items.moveItemTo( box, "apple", 3 ) );
    SW_EXPECT_TRUE( items.isEmpty() );
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

/**
 * @brief [GameFrameworkUtilTest] 정수 가중치 뽑기는 `nextInt( 0, 합 − 1 )` 한 번 — 손으로 걷던 조우표와 같은 씨앗에서 같은 결과다
 * @details 조우표 · 드롭표가 손으로 쓰던 걷기(정수 합 → `nextInt` → 가중치를 빼며 걷기)를 공용으로 옮겼다. 옮긴 뒤에도 같은 씨앗의
 *          리플레이 · 결정성 시험이 그대로이도록 난수 흐름이 손 걷기와 정확히 같은지를 본다. 0 · 음수는 뽑히지 않고 합이 0 이면 −1 이다.
 */
SW_TEST_CASE( GameFrameworkUtilTest, IntegerWeightedPickMatchesTheHandWalkStream )
{
    const vector<int32> listWeight{ 3, 0, 5, -4, 2 };
    GameRandom          shared( 11u );
    GameRandom          reference( 11u );
    for ( int32 roll = 0; roll < 500; ++roll )
    {
        const int32 index = shared.pickWeightedIndexInt( listWeight, []( int32 weight )
        { return weight; } );
        // 손 걷기 — 음수는 0 으로 친다.
        int32 pick     = reference.nextInt( 0, 3 + 5 + 2 - 1 );
        int32 expected = -1;
        for ( int32 slot = 0; slot < static_cast<int32>( listWeight.size() ) && expected < 0; ++slot )
        {
            const int32 weight = listWeight.data()[slot] > 0 ? listWeight.data()[slot] : 0;
            if ( pick < weight )
                expected = slot;
            pick -= weight;
        }
        SW_ASSERT_EQUAL( expected, index );
    }
    const vector<int32> listNone{ 0, -1 };
    SW_EXPECT_EQUAL( -1, shared.pickWeightedIndexInt( listNone, []( int32 weight )
    { return weight; } ) );
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

/**
 * @brief [GameFrameworkUtilTest] 상태 구간 — 표 · 판 · 길이가 붙어 모르는 구간을 길이로 건너뛰고, 길이가 남은 바이트를 넘으면 오류다
 */
SW_TEST_CASE( GameFrameworkUtilTest, StateSectionsCarryTagVersionAndSkipByLength )
{
    Archive first;
    first << int32{ 7 };
    Archive unknown;
    unknown << uint64{ 0x1122334455667788ull };
    unknown << int32{ 9 };
    Archive empty;

    Archive archive;
    StateArchiveUtil::writeSection( archive, 0x41414141u, 3, first );
    StateArchiveUtil::writeSection( archive, 0x42424242u, 1, unknown );
    StateArchiveUtil::writeSection( archive, 0x43434343u, 2, empty );
    vector<uint8> bytes;
    archive.writeData( bytes );

    Archive reader( bytes.data(), bytes.size() );
    uint32  tag     = 0;
    uint32  version = 0;
    Archive body;
    SW_ASSERT_TRUE( StateArchiveUtil::readSection( reader, tag, version, body ) );
    SW_EXPECT_EQUAL( 0x41414141u, tag );
    SW_EXPECT_EQUAL( 3u, version );
    int32 value = 0;
    body >> value;
    SW_EXPECT_EQUAL( 7, value );
    SW_EXPECT_EQUAL( uint64{ 0 }, body.getRemainingBytes() );

    // 모르는 구간 — 본문을 읽지 않고 넘어가도 다음 구간이 맞는다
    SW_ASSERT_TRUE( StateArchiveUtil::readSection( reader, tag, version, body ) );
    SW_EXPECT_EQUAL( 0x42424242u, tag );
    SW_ASSERT_TRUE( StateArchiveUtil::readSection( reader, tag, version, body ) );
    SW_EXPECT_EQUAL( 0x43434343u, tag );
    SW_EXPECT_EQUAL( 2u, version );
    SW_EXPECT_EQUAL( uint64{ 0 }, body.getRemainingBytes() );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );

    // 마지막 구간의 머리가 잘린 세이브 — 앞 둘은 읽히고 셋째에서 오류다
    bytes.resize( bytes.size() - 1 );
    Archive truncated( bytes.data(), bytes.size() );
    SW_EXPECT_TRUE( StateArchiveUtil::readSection( truncated, tag, version, body ) );
    SW_EXPECT_TRUE( StateArchiveUtil::readSection( truncated, tag, version, body ) );
    SW_EXPECT_FALSE( StateArchiveUtil::readSection( truncated, tag, version, body ) );
    SW_EXPECT_TRUE( truncated.isError() );
}
