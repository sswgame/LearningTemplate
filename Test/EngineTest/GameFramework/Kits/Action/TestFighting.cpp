// 철권 류 격투 키트 — 커맨드 우선도 · 스트링 창 · 자세 · 조건, 가드 높이(하단 · 상단 회피), 프레임 이득 확정 반격, 띄우기 저글 감쇠 · 스크류 1회 · 다운 추가타 · 벽꽝,
// 잡기 풀기 창, 횡이동 대 직선 · 추적, 라운드(시간 초과 · K.O. · 레이지 · 무승부), 롤백 저장 → 복원 → 재진행 결정성, 롤백(고정 배치형) 옮기기 · 거절, 세이브(자기 기술형) 왕복, 라운드 시간 · 대기 프레임 수.
#include "pch.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr uint16 kFightingButton1 = 1;
    constexpr uint16 kFightingButton2 = 2;
    constexpr uint16 kButton3         = 4;
    constexpr uint16 kButton4         = 8;

    // 히트박스 높이: 상단 [1.3, 1.7] · 중단 [0.7, 1.3] · 하단 [0, 0.6] · 저글용 넓은 중단 [0.1, 1.7] · 잡기 [1.15, 1.55].
    constexpr const utf8* kFightingMoveXML = R"(<MoveCatalog>
        <Move id="jab" startup="10" active="2" recovery="10" damage="5" hitstun="19" blockstun="12" hitstop="6" height="High">
          <Hitbox x="0.8" y="1.5" w="0.8" h="0.4"/><Cancel from="12" to="18" moves="jab2"/></Move>
        <Move id="jab2" startup="12" active="2" recovery="14" damage="8" hitstun="20" blockstun="10" hitstop="6" height="Mid">
          <Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="two" startup="12" active="2" recovery="12" damage="7" hitstun="18" blockstun="10" height="High"><Hitbox x="0.8" y="1.5" w="0.8" h="0.4"/></Move>
        <Move id="crouchJab" startup="10" active="2" recovery="10" damage="4" hitstun="15" blockstun="10" height="Low"><Hitbox x="0.8" y="0.4" w="0.8" h="0.6"/></Move>
        <Move id="df1" startup="13" active="2" recovery="12" damage="9" hitstun="18" blockstun="12" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="ff2" startup="16" active="3" recovery="20" damage="14" hitstun="20" blockstun="14" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="sweep" startup="16" active="2" recovery="20" damage="10" hitstun="20" blockstun="14" height="Low"><Hitbox x="0.9" y="0.3" w="0.8" h="0.6"/></Move>
        <Move id="unsafe" startup="15" active="2" recovery="25" damage="12" hitstun="20" blockstun="15" hitstop="4" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="safe" startup="15" active="2" recovery="25" damage="12" hitstun="20" blockstun="18" hitstop="4" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="launcher" startup="15" active="2" recovery="18" damage="15" hitstop="4" height="Mid" launcher="true"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="filler" startup="8" active="3" recovery="8" damage="10" hitstun="15" blockstun="10" hitstop="3" height="Mid"><Hitbox x="0.9" y="0.9" w="1.2" h="1.6"/></Move>
        <Move id="screw" startup="10" active="3" recovery="10" damage="10" hitstun="15" blockstun="10" hitstop="3" height="Mid"><Hitbox x="0.9" y="0.9" w="1.2" h="1.6"/></Move>
        <Move id="groundHit" startup="12" active="3" recovery="12" damage="6" height="Low"><Hitbox x="0.9" y="0.2" w="1.2" h="0.4"/></Move>
        <Move id="throw" startup="12" active="2" recovery="20" damage="30" height="Throw"><Hitbox x="0.7" y="1.35" w="0.6" h="0.4"/></Move>
        <Move id="linear" startup="15" active="2" recovery="20" damage="10" hitstun="18" blockstun="12" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="tracking" startup="15" active="2" recovery="20" damage="10" hitstun="18" blockstun="12" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="splat" startup="14" active="2" recovery="16" damage="12" hitstun="20" blockstun="12" height="Mid" wallSplat="true"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="wallEnder" startup="14" active="2" recovery="16" damage="20" height="Mid" knockdown="true"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="toFlamingo" startup="12" active="2" recovery="10" damage="6" hitstun="15" blockstun="10" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
        <Move id="flamingoKick" startup="11" active="2" recovery="12" damage="11" hitstun="16" blockstun="10" height="High"><Hitbox x="0.8" y="1.5" w="0.8" h="0.4"/></Move>
        <Move id="rageArt" startup="20" active="2" recovery="40" damage="40" height="Mid"><Hitbox x="0.9" y="1.0" w="0.8" h="0.6"/></Move>
      </MoveCatalog>)";

    // 방향이 붙은 한 단계 커맨드는 우선도 1 — 기반 규칙은 단계 수만 보므로 "d/f+1" 과 "1" 이 같은 길이다.
    constexpr const utf8* kFightingFighterXML = R"(<FighterCatalog>
        <Fighter id="tester" name="Tester" health="100" sidestepFrames="20" sidestepAngle="0.6981317" sidestep="u" sidestepDown="d,n" jump="u/f">
          <Move id="jab" command="1"/>
          <Move id="jab2" command="2" stringOnly="true"/>
          <Move id="two" command="2"/>
          <Move id="crouchJab" command="1" from="Crouching" priority="1"/>
          <Move id="df1" command="d/f+1" priority="1"/>
          <Move id="ff2" command="f,f+2"/>
          <Move id="sweep" command="d+4" from="Standing,Crouching" priority="1"/>
          <Move id="unsafe" command="b+1" priority="1"/>
          <Move id="safe" command="b+2" priority="1"/>
          <Move id="launcher" command="d/f+2" priority="1"/>
          <Move id="filler" command="3"/>
          <Move id="screw" command="4" screw="true"/>
          <Move id="groundHit" command="d+3" groundHit="true" priority="1"/>
          <Move id="throw" command="1+3" priority="2" breakButtons="1"/>
          <Move id="linear" command="b+3" priority="1"/>
          <Move id="tracking" command="f+4" tracking="true" priority="1"/>
          <Move id="splat" command="b+4" priority="1"/>
          <Move id="wallEnder" command="u/b+3" nearWall="true" priority="1"/>
          <Move id="toFlamingo" command="f+3" enterStance="flamingo" priority="1"/>
          <Move id="flamingoKick" command="4" stance="flamingo" priority="1"/>
          <Move id="rageArt" command="f+1+2" rageArt="true" priority="3"/>
        </Fighter>
      </FighterCatalog>)";

    /** @brief 테스트용 카탈로그 둘입니다(대전이 빌려 쓰므로 테스트 동안 산다). */
    struct FightingFixture
    {
        MoveCatalog    _moveCatalog;
        FighterCatalog _fighterCatalog;
        bool           _bLoaded{ false };

        FightingFixture()
            : _moveCatalog{}
            , _fighterCatalog{}
            , _bLoaded{ false }
        {
            _bLoaded = _moveCatalog.loadFromXMLText( kFightingMoveXML, "FightingTest" ) &&
                       _fighterCatalog.loadFromXMLText( kFightingFighterXML, _moveCatalog, "FightingTest" );
        }

        const FighterDef* findTester() const { return _fighterCatalog.find( "tester" ); }
    };

    /** @brief 앞 기준 넘패드(6 = 상대 쪽)를 그 플레이어의 화면 방향으로 바꿉니다. 플레이어 1 은 오른쪽에 있어 뒤집힌다. */
    InputFrame makeInput( int32 player, uint8 relativeDirection, uint16 buttons = 0 )
    {
        InputFrame frame;
        frame._direction = InputCommandBuffer::mirrorDirection( relativeDirection, player == 0 ? 1 : -1 );
        frame._buttons   = buttons;
        return frame;
    }

    /** @brief @p count 프레임을 같은 입력으로 돌리고 이벤트를 뒤에 붙입니다. */
    void runFrames( FightingMatch& match, int32 count, const InputFrame& input0, const InputFrame& input1, vector<FightingEvent>& inoutListEvent )
    {
        vector<FightingEvent> listEvent;
        for ( int32 index = 0; index < count; ++index )
        {
            match.advanceFrame( input0, input1 );
            listEvent.clear();
            match.drainEvents( listEvent );
            inoutListEvent.insert( inoutListEvent.end(), listEvent.begin(), listEvent.end() );
        }
    }

    int32 countEvents( const vector<FightingEvent>& listEvent, FightingEvent::Kind kind, int32 player )
    {
        int32 count = 0;
        for ( const FightingEvent& event : listEvent )
        {
            if ( event._kind == kind && event._player == player )
                ++count;
        }
        return count;
    }

    /** @brief 그 플레이어가 마지막으로 시작한 기술 id 입니다. 없으면 빈 이름입니다. */
    hashed_string findLastStartedMove( const vector<FightingEvent>& listEvent, int32 player )
    {
        hashed_string moveID;
        for ( const FightingEvent& event : listEvent )
        {
            if ( event._kind == FightingEvent::Kind::MoveStarted && event._player == player )
                moveID = event._moveID;
        }
        return moveID;
    }

    /** @brief 그 플레이어가 그 기술로 낸 첫 Hit 의 피해입니다. 없으면 −1 입니다. */
    int32 findHitDamage( const vector<FightingEvent>& listEvent, int32 player, const hashed_string& moveID )
    {
        for ( const FightingEvent& event : listEvent )
        {
            const bool bHitEventMatches = event._kind == FightingEvent::Kind::Hit && event._player == player && event._moveID == moveID;
            if ( bHitEventMatches )
                return event._value;
        }
        return -1;
    }

    /** @brief 플레이어 0 이 한 프레임 누르고 놓은 뒤 @p waitFrames 동안 중립으로 둡니다. 플레이어 1 은 @p input1 을 계속 넣는다. */
    void pressAndWait( FightingMatch& match, const InputFrame& press0, const InputFrame& input1, int32 waitFrames, vector<FightingEvent>& inoutListEvent )
    {
        runFrames( match, 1, press0, input1, inoutListEvent );
        runFrames( match, waitFrames, makeInput( 0, 5 ), input1, inoutListEvent );
    }

    /** @brief @p source 의 세이브를 같은 캐릭터로 새로 연 대전이 읽는지 반환합니다. 읽지 못하면 그 대전의 롤백 바이트가 그대로인지도 봅니다. */
    bool readsSaveOf( const FightingMatch& source, const FighterDef& fighterDef )
    {
        Archive written;
        source.writeState( written );
        vector<uint8> bytes;
        written.writeData( bytes );
        FightingMatch target;
        SW_EXPECT_TRUE( target.initialize( fighterDef, fighterDef ) );
        vector<uint8> bytesBefore;
        target.saveState( bytesBefore );
        Archive    reader( bytes.data(), bytes.size() );
        const bool bRead = target.readState( reader );
        if ( bRead == false )
        {
            vector<uint8> bytesAfter;
            target.saveState( bytesAfter );
            SW_EXPECT_TRUE( bytesAfter == bytesBefore );
        }
        return bRead;
    }

    /**
     * @brief @p match 의 롤백 바이트를 같은 캐릭터로 새로 연 대전에 읽혀, 같은 상태(같은 필드, 같은 바이트, 같은 라운드 시계)가 되는지 봅니다.
     * @details 필드 순서와 인코딩은 생성 코덱이 정하므로 바이트 배치를 테스트가 따로 적지 않습니다.
     */
    void expectRollbackTransfer( const FightingMatch& match, const FighterDef& fighterDef, const FightingSettings& settings )
    {
        vector<uint8> bytes;
        match.saveState( bytes );
        SW_EXPECT_TRUE( bytes.empty() == false );
        FightingMatch copy;
        SW_EXPECT_TRUE( copy.initialize( fighterDef, fighterDef, settings ) );
        SW_EXPECT_TRUE( copy.loadState( bytes ) );
        SW_EXPECT_TRUE( FixedLayout::equals( match, copy ) );
        vector<uint8> copyBytes;
        copy.saveState( copyBytes );
        SW_EXPECT_TRUE( copyBytes == bytes );
        SW_EXPECT_TRUE( copy.getPhase() == match.getPhase() );
        SW_EXPECT_EQUAL( match.getFrame(), copy.getFrame() );
        SW_EXPECT_EQUAL( match.getRound(), copy.getRound() );
        SW_EXPECT_EQUAL( match.getRoundFramesRemaining(), copy.getRoundFramesRemaining() );
        SW_EXPECT_EQUAL( match.getSeries().getIntermissionTicksRemaining(), copy.getSeries().getIntermissionTicksRemaining() );
        SW_EXPECT_EQUAL( match.getLastRoundWinner(), copy.getLastRoundWinner() );
    }

    /** @brief 240 프레임 라운드 · 2 선승 · 대기 10 프레임 — 아래 두 테스트가 같은 수치를 씁니다. */
    FightingSettings makeRoundPinSettings()
    {
        FightingSettings settings;
        settings._roundFrames     = 240;
        settings._roundsToWin     = 2;
        settings._roundOverFrames = 10;
        return settings;
    }
} // namespace

SW_TEST_CASE( FightingTest, CatalogSkipsBrokenMovesAndRejectsFightersWithoutMovementCommands )
{
    // 프레임 데이터가 없는 기술, 모르는 자세, 모르는 속성은 그 기술만 버린다. 횡이동 · 점프 커맨드를 적지 않은 캐릭터는 넣지 않는다(코드가 대신 정한 커맨드를 쓰지 않는다).
    constexpr const utf8* kBrokenXML = R"(<FighterCatalog>
        <Fighter id="broken" name="Broken" sidestep="u" sidestepDown="d,n" jump="u/f">
          <Move id="jab" command="1"/>
          <Move id="missing" command="1"/>
          <Move id="two" command="2" from="Sitting"/>
          <Move id="df1" command="d/f+1" priorty="1"/>
        </Fighter>
        <Fighter id="still" name="Still"><Move id="jab" command="1"/></Fighter>
      </FighterCatalog>)";
    MoveCatalog           moveCatalog;
    FighterCatalog        fighterCatalog;
    SW_ASSERT_TRUE( moveCatalog.loadFromXMLText( kFightingMoveXML, "FightingTest" ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "moves and fighters that break the schema are skipped with an error" );
        SW_ASSERT_TRUE( fighterCatalog.loadFromXMLText( kBrokenXML, moveCatalog, "FightingTest" ) );
    }
    const FighterDef* pBroken = fighterCatalog.find( "broken" );
    SW_ASSERT_NOT_NULL( pBroken );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pBroken->_listMove.size() ) );
    SW_EXPECT_NULL( fighterCatalog.find( "still" ) );
}

SW_TEST_CASE( FightingTest, CommandPriorityStringWindowPostureAndConditions )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef* pTester = fixture.findTester();
    SW_ASSERT_NOT_NULL( pTester );
    SW_EXPECT_EQUAL( 21, static_cast<int32>( pTester->_listMove.size() ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pTester->_listStance.size() ) );

    FightingMatch         match;
    vector<FightingEvent> listEvent;
    const InputFrame      idle1 = makeInput( 1, 5 );

    // 우선도 — "d/f+1"(우선도 1)이 같은 틱에 완성된 "1" 을 이긴다. 같은 우선도면 단계가 많은 "f,f+2" 가 "2" 를 이긴다.
    SW_EXPECT_TRUE( match.initialize( *pTester, *pTester ) );
    match.setFighterPosition( 1, 3.0f, 0.0f ); // 닿지 않게 멀리
    pressAndWait( match, makeInput( 0, 3, kFightingButton1 ), idle1, 40, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "df1" ) );
    runFrames( match, 1, makeInput( 0, 6 ), idle1, listEvent );
    runFrames( match, 1, makeInput( 0, 5 ), idle1, listEvent );
    pressAndWait( match, makeInput( 0, 6, kFightingButton2 ), idle1, 50, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "ff2" ) );
    pressAndWait( match, makeInput( 0, 5, kFightingButton2 ), idle1, 40, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "two" ) );

    // 스트링 — 잽의 캔슬 창(12~18 프레임) 안에 2 를 누르면 jab2, 창이 지난 뒤에 누르면 선입력된 단독 "2" 가 잽이 끝나고 나간다.
    SW_EXPECT_TRUE( match.initialize( *pTester, *pTester ) );
    match.setFighterPosition( 1, 3.0f, 0.0f );
    listEvent.clear();
    runFrames( match, 1, makeInput( 0, 5, kFightingButton1 ), idle1, listEvent );
    while ( match.getFighter( 0 )._timeline.getFrame() < 13 )
    {
        runFrames( match, 1, makeInput( 0, 5 ), idle1, listEvent );
    }
    pressAndWait( match, makeInput( 0, 5, kFightingButton2 ), idle1, 40, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "jab2" ) );

    listEvent.clear();
    runFrames( match, 1, makeInput( 0, 5, kFightingButton1 ), idle1, listEvent );
    while ( match.getFighter( 0 )._timeline.getFrame() <= 18 )
    {
        runFrames( match, 1, makeInput( 0, 5 ), idle1, listEvent );
    }
    SW_EXPECT_TRUE( match.getFighter( 0 )._state == FighterState::Attacking );
    pressAndWait( match, makeInput( 0, 5, kFightingButton2 ), idle1, 40, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "two" ) );
    SW_EXPECT_EQUAL( 2, countEvents( listEvent, FightingEvent::Kind::MoveStarted, 0 ) );

    // 시작 자세 — 앉은 채(전 프레임이 앉기) 1 은 crouchJab, 선 채 1 은 jab.
    listEvent.clear();
    runFrames( match, 3, makeInput( 0, 2 ), idle1, listEvent );
    pressAndWait( match, makeInput( 0, 2, kFightingButton1 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "crouchJab" ) );
    pressAndWait( match, makeInput( 0, 5, kFightingButton1 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "jab" ) );

    // 고유 자세 — 서기에서 4 는 스크류 기술, f+3 으로 플라밍고에 들어간 뒤의 4 는 flamingoKick.
    pressAndWait( match, makeInput( 0, 5, kButton4 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "screw" ) );
    runFrames( match, 1, makeInput( 0, 6, kButton3 ), idle1, listEvent );
    while ( match.getFighter( 0 )._state == FighterState::Attacking )
    {
        runFrames( match, 1, makeInput( 0, 5 ), idle1, listEvent );
    }
    SW_EXPECT_TRUE( match.getFighter( 0 )._posture == FighterPosture::Stance );
    pressAndWait( match, makeInput( 0, 5, kButton4 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "flamingoKick" ) );

    // 조건 — 상대가 다운이 아니면 d+3 은 groundHit 이 아니라 3(filler), 벽 근처가 아니면 u/b+3 도 filler.
    pressAndWait( match, makeInput( 0, 2, kButton3 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "filler" ) );
    pressAndWait( match, makeInput( 0, 7, kButton3 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "filler" ) );
    // 레이지가 아니면 레이지 아츠 커맨드도 나가지 않는다(f+1 → 아무것도 아님, 1+2 의 1 → jab).
    pressAndWait( match, makeInput( 0, 6, kFightingButton1 | kFightingButton2 ), idle1, 30, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "jab" ) );
}

SW_TEST_CASE( FightingTest, GuardHeightLowHitsStandingHighWhiffsCrouching )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef&     tester = *fixture.findTester();
    FightingMatch         match;
    vector<FightingEvent> listEvent;

    // 하단 — 서서(중립) 가드하면 맞는다.
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    pressAndWait( match, makeInput( 0, 2, kButton4 ), makeInput( 1, 5 ), 30, listEvent );
    SW_EXPECT_EQUAL( 10, findHitDamage( listEvent, 0, "sweep" ) );
    SW_EXPECT_EQUAL( 90, match.getFighter( 1 )._health );

    // 하단 — 앉아 가드(뒤아래)하면 막는다.
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 2, kButton4 ), makeInput( 1, 1 ), 30, listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Blocked, 0 ) );
    SW_EXPECT_EQUAL( 100, match.getFighter( 1 )._health );

    // 상단 — 서서 가드하면 막고, 앉으면 머리 위로 헛친다(맞지도 막히지도 않는다).
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 5, kFightingButton1 ), makeInput( 1, 5 ), 30, listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Blocked, 0 ) );
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 5, kFightingButton1 ), makeInput( 1, 1 ), 30, listEvent );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, FightingEvent::Kind::Blocked, 0 ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, FightingEvent::Kind::Hit, 0 ) );
    SW_EXPECT_EQUAL( 100, match.getFighter( 1 )._health );

    // 중단 — 앉아 가드를 뚫고, 서서 가드에는 막힌다.
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 5, kButton3 ), makeInput( 1, 1 ), 30, listEvent );
    SW_EXPECT_EQUAL( 10, findHitDamage( listEvent, 0, "filler" ) );
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 5, kButton3 ), makeInput( 1, 4 ), 30, listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Blocked, 0 ) );
    SW_EXPECT_TRUE( MoveTimeline::computeGuardOutcome( AttackHeight::Low, GuardStance::Standing ) == GuardOutcome::Hit );
}

SW_TEST_CASE( FightingTest, FrameAdvantageDecidesGuaranteedPunish )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();
    SW_EXPECT_EQUAL( -11, fixture._moveCatalog.find( "unsafe" )->computeFrameAdvantage( true ) );
    SW_EXPECT_EQUAL( -8, fixture._moveCatalog.find( "safe" )->computeFrameAdvantage( true ) );
    SW_EXPECT_EQUAL( 10, fixture._moveCatalog.find( "jab" )->_startup );

    // 막은 쪽은 가드 경직 중에 1 을 미리 넣는다(선입력) — 움직일 수 있는 첫 프레임에 10 프레임 잽이 나간다.
    const hashed_string arrMove[]   = { "unsafe", "safe" };
    const uint16        arrButton[] = { kFightingButton1, kFightingButton2 };
    for ( int32 index = 0; index < 2; ++index )
    {
        FightingMatch         match;
        vector<FightingEvent> listEvent;
        SW_EXPECT_TRUE( match.initialize( tester, tester ) );
        runFrames( match, 1, makeInput( 0, 4, arrButton[index] ), makeInput( 1, 5 ), listEvent );
        bool bPressed = false;
        for ( int32 frame = 0; frame < 90; ++frame )
        {
            const FighterRuntime& defender  = match.getFighter( 1 );
            const bool            bPressNow = bPressed == false && defender._state == FighterState::Blockstun && defender._hitstop == 0 && defender._stateFrames <= 4;
            bPressed                        = bPressed || bPressNow;
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5, bPressNow ? kFightingButton1 : 0 ), listEvent );
        }
        SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Blocked, 0 ) );
        SW_EXPECT_TRUE( findLastStartedMove( listEvent, 1 ) == hashed_string( "jab" ) );
        if ( arrMove[index] == hashed_string( "unsafe" ) )
        {
            // −11 — 후딜이 잽 발생보다 길어 확정으로 맞는다.
            SW_EXPECT_EQUAL( 5, findHitDamage( listEvent, 1, "jab" ) );
            SW_EXPECT_EQUAL( 95, match.getFighter( 0 )._health );
        }
        else
        {
            // −8 — 공격 쪽이 먼저 중립으로 돌아와 서서 막는다.
            SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Blocked, 1 ) );
            SW_EXPECT_EQUAL( 100, match.getFighter( 0 )._health );
        }
    }
}

SW_TEST_CASE( FightingTest, LauncherJuggleScalesDamageAndScrewOncePerCombo )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef&     tester = *fixture.findTester();
    FightingMatch         match;
    vector<FightingEvent> listEvent;
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );

    // 띄우기 — 상대는 앞으로 걷는 중(가드 없음).
    const InputFrame walk1 = makeInput( 1, 6 );
    pressAndWait( match, makeInput( 0, 3, kFightingButton2 ), walk1, 0, listEvent );
    while ( match.getFighter( 0 )._state == FighterState::Attacking )
    {
        runFrames( match, 1, makeInput( 0, 6 ), walk1, listEvent );
    }
    SW_EXPECT_EQUAL( 15, findHitDamage( listEvent, 0, "launcher" ) );
    SW_ASSERT_TRUE( match.getFighter( 1 )._state == FighterState::Juggle );

    // 공중 추가타 — filler, filler, screw, screw. 움직일 수 있게 되면 바로 다음 기술(그 사이는 앞으로 걸어 따라간다).
    const uint16  arrButton[] = { kButton3, kButton3, kButton4, kButton4 };
    vector<int32> listDamage;
    int32         screwVelocityIndex   = 0;
    float32       arrLiftAfterScrew[2] = { 0.0f, 0.0f };
    for ( const uint16 button : arrButton )
    {
        vector<FightingEvent> listStepEvent;
        runFrames( match, 1, makeInput( 0, 5, button ), walk1, listStepEvent );
        int32 guard = 0;
        while ( match.getFighter( 0 )._state == FighterState::Attacking && guard++ < 60 )
        {
            const int32 hitsBefore = countEvents( listStepEvent, FightingEvent::Kind::Hit, 0 );
            runFrames( match, 1, makeInput( 0, 6 ), walk1, listStepEvent );
            const bool bScrewLiftSample = button == kButton4 && countEvents( listStepEvent, FightingEvent::Kind::Hit, 0 ) > hitsBefore && screwVelocityIndex < 2;
            if ( bScrewLiftSample )
                arrLiftAfterScrew[screwVelocityIndex++] = match.getFighter( 1 )._velocityY;
        }
        for ( const FightingEvent& event : listStepEvent )
        {
            if ( event._kind == FightingEvent::Kind::Hit && event._player == 0 )
                listDamage.push_back( event._value );
        }
        listEvent.insert( listEvent.end(), listStepEvent.begin(), listStepEvent.end() );
    }
    SW_ASSERT_TRUE( listDamage.size() == 4 );
    // 공중 히트마다 15 % 감쇠 — 10 × 0.85 → 9, × 0.70 → 7, × 0.55 → 6, × 0.40 → 4.
    SW_EXPECT_EQUAL( 9, listDamage[0] );
    SW_EXPECT_EQUAL( 7, listDamage[1] );
    SW_EXPECT_EQUAL( 6, listDamage[2] );
    SW_EXPECT_EQUAL( 4, listDamage[3] );
    // 스크류는 한 번 — 두 번째는 보통 공중 히트(작게 띄움).
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Screw, 0 ) );
    SW_EXPECT_TRUE( match.getFighter( 1 )._bScrewUsed == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 0.11f, arrLiftAfterScrew[0], 1.0e-5f );
    SW_EXPECT_TRUE( arrLiftAfterScrew[1] < 0.05f );

    // 떨어지면 다운 — 다운된 상대에게만 나가는 d+3 이 groundHit 으로 맞는다.
    int32 guard = 0;
    while ( match.getFighter( 1 )._state == FighterState::Juggle && guard++ < 120 )
    {
        runFrames( match, 1, makeInput( 0, 6 ), walk1, listEvent );
    }
    SW_ASSERT_TRUE( match.getFighter( 1 )._state == FighterState::Down );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 2, kButton3 ), walk1, 20, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "groundHit" ) );
    SW_EXPECT_TRUE( findHitDamage( listEvent, 0, "groundHit" ) > 0 );

    // 벽꽝 — 벽 근처의 상대를 벽꽝 기술로 붙이고, 벽 조건 기술(u/b+3)이 그제야 나간다.
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    match.setFighterPosition( 1, 4.6f, 0.0f );
    match.setFighterPosition( 0, 3.4f, 0.0f );
    SW_EXPECT_TRUE( match.isNearWall( 1 ) );
    listEvent.clear();
    pressAndWait( match, makeInput( 0, 4, kButton4 ), walk1, 0, listEvent );
    while ( match.getFighter( 0 )._state == FighterState::Attacking )
    {
        runFrames( match, 1, makeInput( 0, 5 ), walk1, listEvent );
    }
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::WallSplat, 0 ) );
    SW_ASSERT_TRUE( match.getFighter( 1 )._state == FighterState::WallSplat );
    pressAndWait( match, makeInput( 0, 7, kButton3 ), walk1, 20, listEvent );
    SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "wallEnder" ) );
    SW_EXPECT_EQUAL( 17, findHitDamage( listEvent, 0, "wallEnder" ) ); // 벽꽝 추가타도 감쇠(20 × 0.85)
}

SW_TEST_CASE( FightingTest, ThrowBreaksOnlyWithMatchingButtonInWindow )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();

    // 0: 맞는 버튼(1) → 풀림, 1: 틀린 버튼(2)을 먼저 → 잠겨 못 풂, 2: 아무것도 안 누름, 3: 창이 지난 뒤 누름.
    for ( int32 scenario = 0; scenario < 4; ++scenario )
    {
        FightingMatch         match;
        vector<FightingEvent> listEvent;
        SW_EXPECT_TRUE( match.initialize( tester, tester ) );
        runFrames( match, 1, makeInput( 0, 5, kFightingButton1 | kButton3 ), makeInput( 1, 5 ), listEvent );
        while ( match.getFighter( 1 )._state != FighterState::ThrowBreak && match.getFrame() < 30 )
        {
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
        }
        SW_ASSERT_TRUE( match.getFighter( 1 )._state == FighterState::ThrowBreak );
        SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::ThrowGrab, 0 ) );
        SW_EXPECT_TRUE( findLastStartedMove( listEvent, 0 ) == hashed_string( "throw" ) ); // 1+3 이 1 을 이긴다(우선도 2)

        runFrames( match, 5, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
        if ( scenario == 0 )
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5, kFightingButton1 ), listEvent );
        if ( scenario == 1 )
        {
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5, kFightingButton2 ), listEvent );
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5, kFightingButton1 ), listEvent );
        }
        if ( scenario == 3 )
        {
            runFrames( match, match.getSettings()._throwBreakFrames, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
            runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5, kFightingButton1 ), listEvent );
        }
        runFrames( match, 40, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );

        const bool bBroken = scenario == 0;
        SW_EXPECT_EQUAL( bBroken ? 1 : 0, countEvents( listEvent, FightingEvent::Kind::ThrowBroken, 1 ) );
        SW_EXPECT_EQUAL( bBroken ? 0 : 1, countEvents( listEvent, FightingEvent::Kind::ThrowLanded, 0 ) );
        SW_EXPECT_EQUAL( bBroken ? 100 : 70, match.getFighter( 1 )._health );
    }

    // 앉으면 잡기는 머리 위로 헛친다.
    FightingMatch         match;
    vector<FightingEvent> listEvent;
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    runFrames( match, 2, makeInput( 0, 5 ), makeInput( 1, 2 ), listEvent );
    runFrames( match, 1, makeInput( 0, 5, kFightingButton1 | kButton3 ), makeInput( 1, 2 ), listEvent );
    runFrames( match, 40, makeInput( 0, 5 ), makeInput( 1, 2 ), listEvent );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, FightingEvent::Kind::ThrowGrab, 0 ) );
    SW_EXPECT_EQUAL( 100, match.getFighter( 1 )._health );
}

SW_TEST_CASE( FightingTest, SidestepDodgesLinearButNotTracking )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();

    // 0: 직선 · 횡이동 없음 → 맞음, 1: 직선 · 횡이동 → 빗나감, 2: 추적 · 횡이동 → 맞음.
    for ( int32 scenario = 0; scenario < 3; ++scenario )
    {
        FightingMatch         match;
        vector<FightingEvent> listEvent;
        SW_EXPECT_TRUE( match.initialize( tester, tester ) );
        const bool       bSidestep = scenario != 0;
        const InputFrame attack    = scenario == 2 ? makeInput( 0, 6, kButton4 ) : makeInput( 0, 4, kButton3 );
        // 상대는 앞으로 걷기(가드 없음) 대신 제자리 — 횡이동이 아니면 중립에서 앞을 누른 채 맞는다.
        runFrames( match, 1, attack, bSidestep ? makeInput( 1, 8 ) : makeInput( 1, 6 ), listEvent );
        runFrames( match, 40, makeInput( 0, 5 ), bSidestep ? makeInput( 1, 5 ) : makeInput( 1, 6 ), listEvent );
        SW_EXPECT_EQUAL( bSidestep ? 1 : 0, countEvents( listEvent, FightingEvent::Kind::Sidestep, 1 ) );
        const bool bExpectHit = scenario != 1;
        SW_EXPECT_EQUAL( bExpectHit ? 1 : 0, countEvents( listEvent, FightingEvent::Kind::Hit, 0 ) );
        SW_EXPECT_EQUAL( bExpectHit ? 90 : 100, match.getFighter( 1 )._health );
    }
}

SW_TEST_CASE( FightingTest, RoundsTimeOutKnockoutRageAndDraw )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();
    FightingSettings  settings;
    settings._roundFrames     = 240;
    settings._roundsToWin     = 2;
    settings._roundOverFrames = 10;
    FightingMatch         match;
    vector<FightingEvent> listEvent;
    SW_EXPECT_TRUE( match.initialize( tester, tester, settings ) );

    // 1 라운드 — 잽 하나 맞히고 시간 초과: 체력 비율이 높은 0 이 이긴다.
    pressAndWait( match, makeInput( 0, 5, kFightingButton1 ), makeInput( 1, 6 ), 250, listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::TimeOut, 0 ) );
    SW_EXPECT_EQUAL( 0, match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 2, match.getRound() );
    SW_EXPECT_EQUAL( 100, match.getFighter( 1 )._health ); // 새 라운드는 체력을 채운다

    // 2 라운드 — 체력이 25 % 이하면 레이지: 주는 피해 ×1.1(잽 5 → 6), 레이지 아츠는 한 번 쓰면 레이지가 끝난다.
    listEvent.clear();
    match.setFighterHealth( 1, 20 );
    runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::RageEntered, 1 ) );
    runFrames( match, 1, makeInput( 0, 6 ), makeInput( 1, 5, kFightingButton1 ), listEvent );
    runFrames( match, 30, makeInput( 0, 6 ), makeInput( 1, 5 ), listEvent );
    SW_EXPECT_EQUAL( 6, findHitDamage( listEvent, 1, "jab" ) );
    runFrames( match, 1, makeInput( 0, 5 ), makeInput( 1, 6, kFightingButton1 | kFightingButton2 ), listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::RageArt, 1 ) );
    SW_EXPECT_TRUE( match.getFighter( 1 )._bRage == SW_FALSE );
    SW_EXPECT_TRUE( match.getFighter( 1 )._bRageUsed == SW_TRUE );
    runFrames( match, 80, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::RageArt, 1 ) );

    // K.O. — 체력 3 의 0 을 잽(레이지가 끝나 5)으로(시간 초과 전에).
    match.setFighterHealth( 0, 3 );
    runFrames( match, 1, makeInput( 0, 6 ), makeInput( 1, 5, kFightingButton1 ), listEvent );
    runFrames( match, 20, makeInput( 0, 6 ), makeInput( 1, 5 ), listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::Knockout, 1 ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, FightingEvent::Kind::TimeOut, 1 ) );
    SW_EXPECT_EQUAL( 1, match.getRoundWins( 1 ) );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Intermission || match.getRound() == 3 );

    // 3 라운드 — 아무도 안 때리고 시간 초과: 같은 체력이면 무승부, 둘 다 2 승 → 대전 무승부.
    listEvent.clear();
    runFrames( match, 300, makeInput( 0, 5 ), makeInput( 1, 5 ), listEvent );
    SW_EXPECT_EQUAL( 3, match.getRound() );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, match.getLastRoundWinner() );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Finished );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, match.getMatchWinner() );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::MatchEnd, FightingMatch::kDraw ) );
}

SW_TEST_CASE( FightingTest, RollbackSaveLoadResimulatesIdentically )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();

    // 입력 1 바이트 왕복.
    InputFrame sample;
    sample._direction        = 3;
    sample._buttons          = kFightingButton1 | kButton4;
    const InputFrame decoded = FightingMatch::decodeInput( FightingMatch::encodeInput( sample ) );
    SW_EXPECT_EQUAL( 3, decoded._direction );
    SW_EXPECT_EQUAL( kFightingButton1 | kButton4, decoded._buttons );

    // 씨앗 입력열 — 버튼을 자주 눌러 공격 · 가드 · 콤보가 섞이게.
    constexpr int32 kFrameCount = 400;
    constexpr int32 kSaveFrame  = 150;
    GameRandom      random( 1234u );
    vector<uint8>   listScript;
    for ( int32 frame = 0; frame < kFrameCount * 2; ++frame )
    {
        InputFrame input;
        input._direction = static_cast<uint8>( random.nextInt( 1, 9 ) );
        input._buttons   = random.nextChance( 0.3f ) ? static_cast<uint16>( 1u << random.nextInt( 0, 3 ) ) : 0;
        listScript.push_back( FightingMatch::encodeInput( input ) );
    }
    vector<uint8> listInput( 2 );

    FightingMatch         match;
    vector<FightingEvent> listEvent;
    vector<uint8>         savedBuffer;
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    int32 eventCount = 0;
    for ( int32 frame = 0; frame < kFrameCount; ++frame )
    {
        if ( frame == kSaveFrame )
            match.saveState( savedBuffer );
        listInput[0] = listScript[static_cast<size_t>( frame * 2 )];
        listInput[1] = listScript[static_cast<size_t>( frame * 2 + 1 )];
        match.advanceFrame( listInput );
        listEvent.clear();
        match.drainEvents( listEvent );
        eventCount += static_cast<int32>( listEvent.size() );
    }
    vector<uint8> firstFinal;
    match.saveState( firstFinal );
    SW_EXPECT_TRUE( eventCount > 20 ); // 실제로 싸웠다
    SW_EXPECT_TRUE( match.getFighter( 0 )._health < 100 || match.getFighter( 1 )._health < 100 || match.getRound() > 1 );

    // 되돌려 같은 입력으로 다시 — 끝 상태 바이트가 같다.
    SW_ASSERT_TRUE( match.loadState( savedBuffer ) );
    SW_EXPECT_EQUAL( kSaveFrame, match.getFrame() );
    vector<uint8> reloaded;
    match.saveState( reloaded );
    SW_EXPECT_TRUE( reloaded == savedBuffer );
    for ( int32 frame = kSaveFrame; frame < kFrameCount; ++frame )
    {
        listInput[0] = listScript[static_cast<size_t>( frame * 2 )];
        listInput[1] = listScript[static_cast<size_t>( frame * 2 + 1 )];
        match.advanceFrame( listInput );
    }
    vector<uint8> secondFinal;
    match.saveState( secondFinal );
    SW_EXPECT_TRUE( secondFinal == firstFinal );

    // 처음부터 따로 돌린 대전도 같다.
    FightingMatch fresh;
    SW_EXPECT_TRUE( fresh.initialize( tester, tester ) );
    for ( int32 frame = 0; frame < kFrameCount; ++frame )
    {
        listInput[0] = listScript[static_cast<size_t>( frame * 2 )];
        listInput[1] = listScript[static_cast<size_t>( frame * 2 + 1 )];
        fresh.advanceFrame( listInput );
    }
    vector<uint8> freshFinal;
    fresh.saveState( freshFinal );
    SW_EXPECT_TRUE( freshFinal == firstFinal );

    // 깨진 바이트는 거절하고 상태를 바꾸지 않는다.
    vector<uint8> broken( savedBuffer.begin(), savedBuffer.begin() + static_cast<ptrdiff_t>( savedBuffer.size() / 2 ) );
    SW_EXPECT_FALSE( match.loadState( broken ) );
    vector<uint8> afterBroken;
    match.saveState( afterBroken );
    SW_EXPECT_TRUE( afterBroken == firstFinal );
}

/**
 * @brief [FightingTest] 롤백 상태(고정 배치형)가 라운드 중 · 대기 중 · 다음 라운드 · 대전 끝에서 새로 연 대전으로 그대로 옮겨 간다.
 *        다른 캐릭터의 대전과 잘린 바이트는 거절하고 상태를 바꾸지 않는다
 */
SW_TEST_CASE( FightingTest, RollbackStateMovesToAFreshMatchAndRejectsForeignBytes )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef&      tester   = *fixture.findTester();
    const InputFrame       neutral0 = makeInput( 0, 5 );
    const InputFrame       neutral1 = makeInput( 1, 5 );
    const FightingSettings settings = makeRoundPinSettings();
    FightingMatch          match;
    vector<FightingEvent>  listEvent;
    SW_EXPECT_TRUE( match.initialize( tester, tester, settings ) );
    expectRollbackTransfer( match, tester, settings );

    // 1 라운드 시간 초과(체력이 많은 0 승) 바로 뒤 — 대기 10.
    match.setFighterHealth( 1, 90 );
    runFrames( match, 240, neutral0, neutral1, listEvent );
    SW_ASSERT_TRUE( match.getPhase() == RoundSeriesPhase::Intermission );
    SW_EXPECT_EQUAL( 10, match.getSeries().getIntermissionTicksRemaining() );
    expectRollbackTransfer( match, tester, settings );

    // 대기 6 프레임 뒤 — 4 남음.
    runFrames( match, 6, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( 4, match.getSeries().getIntermissionTicksRemaining() );
    expectRollbackTransfer( match, tester, settings );

    // 2 라운드 시작.
    runFrames( match, 4, neutral0, neutral1, listEvent );
    SW_ASSERT_TRUE( match.getPhase() == RoundSeriesPhase::RoundActive );
    expectRollbackTransfer( match, tester, settings );

    // 2 라운드도 0 승 → 대전 끝(끝날 때는 대기를 걸지 않는다).
    match.setFighterHealth( 1, 90 );
    runFrames( match, 240, neutral0, neutral1, listEvent );
    SW_ASSERT_TRUE( match.getPhase() == RoundSeriesPhase::Finished );
    expectRollbackTransfer( match, tester, settings );

    // 다른 캐릭터(id 가 다른 정의)로 연 대전은 같은 바이트를 거절하고 그대로다.
    vector<uint8> bytes;
    match.saveState( bytes );
    FighterDef impostorDef = tester;
    impostorDef._id        = hashed_string( "impostor" );
    FightingMatch impostor;
    SW_EXPECT_TRUE( impostor.initialize( impostorDef, impostorDef, settings ) );
    vector<uint8> impostorBefore;
    impostor.saveState( impostorBefore );
    SW_EXPECT_FALSE( impostor.loadState( bytes ) );
    vector<uint8> impostorAfter;
    impostor.saveState( impostorAfter );
    SW_EXPECT_TRUE( impostorAfter == impostorBefore );

    // 잘린 바이트(빈 것, 한 바이트, 절반, 한 바이트 모자란 것)는 거절하고 그대로다.
    FightingMatch target;
    SW_EXPECT_TRUE( target.initialize( tester, tester, settings ) );
    vector<uint8> targetBefore;
    target.saveState( targetBefore );
    const size_t arrLength[] = { 0, 1, bytes.size() / 2, bytes.size() - 1 };
    for ( const size_t length : arrLength )
    {
        const vector<uint8> prefix( bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>( length ) );
        SW_EXPECT_FALSE( target.loadState( prefix ) );
    }
    vector<uint8> targetAfter;
    target.saveState( targetAfter );
    SW_EXPECT_TRUE( targetAfter == targetBefore );
}

/**
 * @brief [FightingTest] 라운드 시간 · 라운드 사이 대기 · 대전 끝을 프레임 단위로 센다 — 240 번째 프레임에 시간 초과, 대기 10 프레임 뒤 2 라운드, 2 선승에 끝, 끝난 뒤의 프레임은 세지 않는다
 * @details 대기 0 은 1 프레임으로 센다(끝난 다음 프레임에 새 라운드).
 */
SW_TEST_CASE( FightingTest, RoundClockIntermissionAndMatchEndCountExactFrames )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef&     tester   = *fixture.findTester();
    const InputFrame      neutral0 = makeInput( 0, 5 );
    const InputFrame      neutral1 = makeInput( 1, 5 );
    FightingSettings      settings = makeRoundPinSettings();
    FightingMatch         match;
    vector<FightingEvent> listEvent;
    SW_EXPECT_TRUE( match.initialize( tester, tester, settings ) );
    SW_EXPECT_EQUAL( 1, match.getRound() );
    SW_EXPECT_EQUAL( 240, match.getRoundFramesRemaining() );

    // 239 프레임 — 아직 라운드 중, 1 프레임 남음.
    match.setFighterHealth( 1, 90 );
    runFrames( match, 239, neutral0, neutral1, listEvent );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::RoundActive );
    SW_EXPECT_EQUAL( 1, match.getRoundFramesRemaining() );
    SW_EXPECT_EQUAL( -1, match.getLastRoundWinner() );

    // 240 번째 — 시간 초과, 체력이 많은 0 승. 알림은 TimeOut → RoundEnd 둘.
    listEvent.clear();
    runFrames( match, 1, neutral0, neutral1, listEvent );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._kind == FightingEvent::Kind::TimeOut && listEvent[0]._player == 0 );
    SW_EXPECT_TRUE( listEvent[1]._kind == FightingEvent::Kind::RoundEnd && listEvent[1]._value == 0 );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Intermission );
    SW_EXPECT_EQUAL( 0, match.getRoundFramesRemaining() );
    SW_EXPECT_EQUAL( 0, match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 0, match.getRoundWins( 1 ) );

    // 대기 10 프레임 — 9 프레임째까지 1 라운드, 10 프레임째 2 라운드(체력 · 시간을 채운다).
    runFrames( match, 9, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( 1, match.getRound() );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Intermission );
    runFrames( match, 1, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( 2, match.getRound() );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::RoundActive );
    SW_EXPECT_EQUAL( 240, match.getRoundFramesRemaining() );
    SW_EXPECT_EQUAL( 100, match.getFighter( 1 )._health );
    SW_EXPECT_EQUAL( 250, match.getFrame() );

    // 2 라운드도 0 승 → 2 선승으로 대전 끝. 끝난 뒤의 프레임은 세지 않는다.
    listEvent.clear();
    match.setFighterHealth( 1, 90 );
    runFrames( match, 240, neutral0, neutral1, listEvent );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Finished );
    SW_EXPECT_EQUAL( 0, match.getMatchWinner() );
    SW_EXPECT_EQUAL( 2, match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, FightingEvent::Kind::MatchEnd, 0 ) );
    SW_EXPECT_EQUAL( 490, match.getFrame() );
    runFrames( match, 5, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( 490, match.getFrame() );

    // 라운드 사이 대기와 라운드 시간 0 은 1 프레임으로 올려 받지 않고 거절한다(범위는 `FightingSettings` 의 선언).
    FightingSettings zeroOver = settings;
    zeroOver._roundOverFrames = 0;
    SW_EXPECT_FALSE( match.initialize( tester, tester, zeroOver ) );
    FightingSettings zeroRound = settings;
    zeroRound._roundFrames     = 0;
    SW_EXPECT_FALSE( match.initialize( tester, tester, zeroRound ) );

    // 대기 1 프레임 — 무승부 라운드(같은 체력) 뒤 다음 프레임에 2 라운드.
    settings._roundOverFrames = 1;
    SW_EXPECT_TRUE( match.initialize( tester, tester, settings ) );
    runFrames( match, 240, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( FightingMatch::kDraw, match.getLastRoundWinner() );
    SW_EXPECT_EQUAL( 1, match.getRoundWins( 0 ) );
    SW_EXPECT_EQUAL( 1, match.getRoundWins( 1 ) );
    SW_EXPECT_TRUE( match.getPhase() == RoundSeriesPhase::Intermission );
    runFrames( match, 1, neutral0, neutral1, listEvent );
    SW_EXPECT_EQUAL( 2, match.getRound() );
}

/**
 * @brief [FightingTest] 세이브(Archive) 왕복 — 버전 머리가 붙은 자기 기술형 본문으로 실어, 새로 연 대전이 같은 롤백 바이트로 돌아오고 같은 입력을 더 넣어도 같다.
 *        세이브 본문은 롤백 바이트가 아니다. 잘린 본문과 다른 캐릭터의 대전은 거절하고 그대로 둔다
 */
SW_TEST_CASE( FightingTest, StateRoundTripContinuesTheSameMatch )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();
    GameRandom        random( 4321u );
    vector<uint8>     listScript;
    for ( int32 frame = 0; frame < 300 * 2; ++frame )
    {
        InputFrame input;
        input._direction = static_cast<uint8>( random.nextInt( 1, 9 ) );
        input._buttons   = random.nextChance( 0.3f ) ? static_cast<uint16>( 1u << random.nextInt( 0, 3 ) ) : 0;
        listScript.push_back( FightingMatch::encodeInput( input ) );
    }
    vector<uint8> listInput( 2 );
    auto          runScript = [&]( FightingMatch& match, int32 firstFrame, int32 lastFrame )
    {
        for ( int32 frame = firstFrame; frame < lastFrame; ++frame )
        {
            listInput[0] = listScript[static_cast<size_t>( frame * 2 )];
            listInput[1] = listScript[static_cast<size_t>( frame * 2 + 1 )];
            match.advanceFrame( listInput );
        }
    };

    FightingMatch match;
    SW_EXPECT_TRUE( match.initialize( tester, tester ) );
    runScript( match, 0, 150 );
    vector<uint8> codecBytes;
    match.saveState( codecBytes );
    Archive written;
    match.writeState( written );
    vector<uint8> archiveBytes;
    written.writeData( archiveBytes );
    // 세이브 본문은 버전 머리(uint32 리틀 엔디언)로 시작하고, 롤백 스트림의 머리인 고정 배치형 스키마 해시도, 롤백 바이트도 싣지 않는다.
    SW_ASSERT_TRUE( archiveBytes.size() > sizeof( uint32 ) );
    uint32 headerVersion = 0;
    std::memcpy( &headerVersion, archiveBytes.data(), sizeof( uint32 ) );
    SW_EXPECT_EQUAL( FightingMatch::kStateVersion, headerVersion );
    const uint64 schemaHash = FixedLayout::getSchemaHash<FightingMatch>();
    uint8        arrHashByte[sizeof( uint64 )];
    std::memcpy( arrHashByte, &schemaHash, sizeof( uint64 ) );
    SW_ASSERT_TRUE( codecBytes.size() > sizeof( uint64 ) );
    SW_EXPECT_TRUE( std::equal( std::begin( arrHashByte ), std::end( arrHashByte ), codecBytes.begin() ) ); // 롤백 스트림은 해시로 시작한다
    SW_EXPECT_TRUE( std::search( archiveBytes.begin(), archiveBytes.end(), std::begin( arrHashByte ), std::end( arrHashByte ) ) == archiveBytes.end() );
    SW_EXPECT_TRUE( std::search( archiveBytes.begin(), archiveBytes.end(), codecBytes.begin(), codecBytes.end() ) == archiveBytes.end() );

    FightingMatch restored;
    SW_EXPECT_TRUE( restored.initialize( tester, tester ) );
    Archive reader( archiveBytes.data(), archiveBytes.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    SW_EXPECT_EQUAL( 150, restored.getFrame() );
    vector<uint8> restoredBytes;
    restored.saveState( restoredBytes );
    SW_EXPECT_TRUE( codecBytes == restoredBytes );

    runScript( match, 150, 300 );
    runScript( restored, 150, 300 );
    match.saveState( codecBytes );
    restored.saveState( restoredBytes );
    SW_EXPECT_TRUE( codecBytes == restoredBytes );

    FightingMatch truncated;
    SW_EXPECT_TRUE( truncated.initialize( tester, tester ) );
    Archive cut( archiveBytes.data(), archiveBytes.size() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_EQUAL( 0, truncated.getFrame() );

    FighterDef impostorDef = tester;
    impostorDef._id        = hashed_string( "impostor" );
    FightingMatch impostor;
    SW_EXPECT_TRUE( impostor.initialize( impostorDef, impostorDef ) );
    Archive foreignReader( archiveBytes.data(), archiveBytes.size() );
    SW_EXPECT_FALSE( impostor.readState( foreignReader ) );
    SW_EXPECT_EQUAL( 0, impostor.getFrame() );
}

/**
 * @brief [FightingTest] 범위 밖 값이 든 세이브는 거절하고 상태를 바꾸지 않는다.
 * @details 자기 기술형 읽기는 `Min` · `Max` 를 보지 않으므로 `readState` 가 직접 본다. 최상위 칸(지난 라운드 결과), 배열 원소의 칸(기술 인덱스),
 *          값으로 든 구조체의 칸(고리 버퍼 다음 자리), vector 원소의 칸(입력 방향)을 하나씩 망가뜨린다. 망가뜨리지 않은 세이브는 읽힌다.
 */
SW_TEST_CASE( FightingTest, SaveStateWithOutOfRangeValuesIsRejected )
{
    FightingFixture fixture;
    SW_ASSERT_TRUE( fixture._bLoaded );
    const FighterDef& tester = *fixture.findTester();
    FightingMatch     source;
    SW_EXPECT_TRUE( source.initialize( tester, tester ) );
    SW_EXPECT_TRUE( readsSaveOf( source, tester ) );

    const PropertyInfo* pLastWinner = FightingMatch::StaticType()->findProperty( "_lastRoundWinner" );
    const PropertyInfo* pHead       = InputCommandBuffer::StaticType()->findProperty( "_head" );
    const PropertyInfo* pFrameList  = InputCommandBuffer::StaticType()->findProperty( "_listFrame" );
    SW_ASSERT_TRUE( pLastWinner != nullptr );
    SW_ASSERT_TRUE( pHead != nullptr );
    SW_ASSERT_TRUE( pFrameList != nullptr );

    FightingMatch lastWinnerCase                        = source;
    *pLastWinner->getValuePtr<int32>( &lastWinnerCase ) = 3; // [−1, 2] 밖
    SW_EXPECT_FALSE( readsSaveOf( lastWinnerCase, tester ) );

    FightingMatch moveIndexCase                                             = source;
    const_cast<FighterRuntime&>( moveIndexCase.getFighter( 1 ) )._moveIndex = -2; // 배열 원소의 칸, 최소 −1 밖
    SW_EXPECT_FALSE( readsSaveOf( moveIndexCase, tester ) );

    FightingMatch headCase                                                                              = source;
    *pHead->getValuePtr<int32>( &const_cast<FighterRuntime&>( headCase.getFighter( 0 ) )._inputBuffer ) = -1;
    SW_EXPECT_FALSE( readsSaveOf( headCase, tester ) );

    FightingMatch       directionCase = source;
    vector<InputFrame>* pListFrame    = pFrameList->getValuePtr<vector<InputFrame>>( &const_cast<FighterRuntime&>( directionCase.getFighter( 0 ) )._inputBuffer );
    SW_ASSERT_TRUE( pListFrame != nullptr && pListFrame->empty() == false );
    ( *pListFrame )[0]._direction = 12; // vector 원소의 칸, 최대 9 밖
    SW_EXPECT_FALSE( readsSaveOf( directionCase, tester ) );
}
