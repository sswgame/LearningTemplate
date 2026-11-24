// RhythmGameModeTest — 리듬 디렉터: 열기 전 명령 거절, 음악 시계(넘긴 시각까지만 흐르고 되감기지 않는다), 레인 명령 판정과 상태 왕복, 자동 플레이로 깬 곡의 결과(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Component/RhythmGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/GameModeStateVersionTest.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

namespace
{
    constexpr float32 kRhythmFrame = 1.0f / 60.0f;

    // 레인 둘. 레인 0 의 롱노트(2 ~ 4 초), 레인 1 의 보통 노트(3 초). 판정은 셋이다.
    constexpr const utf8* kRhythmChartXML = R"(<Chart title="Song" artist="Band" level="1" lanes="2"><Bpm beat="0" bpm="120"/><Note lane="0" beat="4" endBeat="8"/><Note lane="1" beat="6"/></Chart>)";

    /** @brief 채보 파일을 가리키는 디렉터 하나입니다. */
    struct RhythmGameModeTest
    {
        const test::ScopedScratchDomain   _domain;
        RecordingSoundPlayer              _soundPlayer{};
        SubsystemScope                    _scope{};
        unique_ptr<game::ScopedGameScope> _pScopeBinding{};
        GameObjectManager                 _manager{};
        RhythmGameModeComponent*          _pGameMode{ nullptr };

        RhythmGameModeTest()
        {
            SW_REQUIRE_TRUE( _scope.bind<ISoundPlayer>( &_soundPlayer ) );
            _pScopeBinding = make_unique<game::ScopedGameScope>( _scope );
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( _domain.makeDiskPath( "song.chart.xml" ), kRhythmChartXML ) );
            _pGameMode = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<RhythmGameModeComponent>();
            SW_REQUIRE_NOT_NULL( _pGameMode );
            set( "_chart", test::ScopedScratchDomain::makeID( "song.chart.xml" ).str() );
            set( "_resultHoldSeconds", "0.5" );
            set( "_clearReward", "25" );
        }

        void set( const utf8* pProperty, string_view text )
        {
            const PropertyInfo* const pInfo = RhythmGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            SW_REQUIRE_NOT_NULL( pInfo );
            SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pInfo, MutableObjectAddress{ _pGameMode }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
        }

        void advance( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kRhythmFrame );
            }
        }

        vector<uint8> saveState() const
        {
            Archive saved;
            _pGameMode->writeState( saved );
            vector<uint8> bytes;
            saved.writeData( bytes );
            return bytes;
        }
    };
} // namespace

/** @brief [RhythmGameModeTest] 곡이 열리기 전의 시각 · 레인 명령은 거절한다 */
SW_TEST_CASE( RhythmGameModeTest, CommandsBeforeTheSongOpensAreRefused )
{
    GameObjectManager        manager;
    RhythmGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<RhythmGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->submitMusicTime( 1.0f ) );
    SW_EXPECT_FALSE( pGameMode->pressLane( 0 ) );
    SW_EXPECT_FALSE( pGameMode->releaseLane( 0 ) );
    SW_EXPECT_NULL( pGameMode->findChart() );
}

/** @brief [RhythmGameModeTest] 음악 시계의 곡은 넘긴 시각까지만 흐른다 — 프레임이 흘러도 놓치지 않고, 이른 시각을 넘겨도 되감기지 않는다 */
SW_TEST_CASE_F( RhythmGameModeTest, MusicClockAdvancesOnlyByTheSubmittedTime )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    advance( 600 ); // 10 초 — 곡(4 초)보다 길다
    SW_EXPECT_EQUAL( 0, _pGameMode->getJudged() );
    SW_EXPECT_NEAR( 0.0f, _pGameMode->getMusicTime(), 1.0e-6f );
    SW_EXPECT_TRUE( _pGameMode->submitMusicTime( 2.5f ) );
    advance( 1 );
    SW_EXPECT_EQUAL( 2, _pGameMode->getMissed() ); // 머리를 놓친 롱노트는 끝도 놓친다
    SW_EXPECT_EQUAL( 2, _soundPlayer.countPosted( "RhythmMissed" ) );
    SW_EXPECT_EQUAL( 0, _soundPlayer.countPosted( "RhythmJudged" ) );
    SW_EXPECT_TRUE( _pGameMode->submitMusicTime( 1.0f ) );
    SW_EXPECT_NEAR( 2.5f, _pGameMode->getMusicTime(), 1.0e-6f );
    _manager.endPlay();
}

/** @brief [RhythmGameModeTest] 레인 명령은 지금 시각에 판정하고, 쓴 상태를 되살리면 누르고 있는 롱노트까지 이어진다 */
SW_TEST_CASE_F( RhythmGameModeTest, LaneCommandsAreJudgedAndTheStateRoundTrips )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_FALSE( _pGameMode->pressLane( 2 ) ); // 없는 레인
    SW_EXPECT_TRUE( _pGameMode->submitMusicTime( 2.01f ) );
    SW_EXPECT_TRUE( _pGameMode->pressLane( 0 ) );
    advance( 1 );
    SW_EXPECT_EQUAL( 1, _pGameMode->getJudged() );
    SW_EXPECT_EQUAL( 0, _pGameMode->getMissed() );
    SW_EXPECT_EQUAL( 1, _soundPlayer.countPosted( "RhythmJudged" ) );
    SW_EXPECT_TRUE( _pGameMode->getSession().isHolding( 0 ) );

    vector<uint8> bytes = saveState();
    SW_EXPECT_TRUE( _pGameMode->submitMusicTime( 3.9f ) );
    advance( 1 );
    SW_EXPECT_EQUAL( 2, _pGameMode->getJudged() ); // 레인 1 을 놓쳤다
    _pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_EQUAL( 1, _pGameMode->getJudged() );
    SW_EXPECT_TRUE( _pGameMode->getSession().isHolding( 0 ) );
    SW_EXPECT_NEAR( 2.01f, _pGameMode->getMusicTime(), 1.0e-4f );
    _manager.endPlay();
}

/** @brief [RhythmGameModeTest] 시뮬레이션 시계 · 자동 플레이의 곡은 모두 맞혀 깨고, 결과를 보인 뒤 미니게임을 한 번 끝낸다(흐름이 없으면 오류) */
SW_TEST_CASE_F( RhythmGameModeTest, AutoPlayedSongIsClearedWithTheReward )
{
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    set( "_clock", "Simulation" );
    set( "_bAutoPlay", "true" );
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_FALSE( _pGameMode->submitMusicTime( 3.0f ) ); // 이 판의 시계가 아니다
    SW_EXPECT_FALSE( _pGameMode->pressLane( 0 ) );
    advance( 60 * 5 );
    SW_EXPECT_EQUAL( static_cast<int32>( RhythmPlayState::Cleared ), _pGameMode->getState() );
    SW_EXPECT_EQUAL( 3, _pGameMode->getMaxCombo() );
    SW_EXPECT_EQUAL( 900, _pGameMode->getScore() ); // 기본 창의 가장 좁은 등급 300 점 셋
    vector<MinigameResultValue> listValue;
    _pGameMode->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 25 }, listValue[0]._amount );
    advance( 60 );
    _manager.endPlay();
}

/** @brief [RhythmGameModeTest] 결과 유지 깃발이 있던 판 1 상태는 패치로 올라 읽히고 나머지 값이 그대로다(판 1 은 `_musicTime` 뒤에 깃발 둘이 있었다) */
SW_TEST_CASE_F( RhythmGameModeTest, Version1StateIsPatchedAndReadsTheSame )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    advance( 30 );
    constexpr uint8 kArrRemovedField[] = { 0, 0 };
    test::GameModeStateVersionTest::expectVersion1ReadsTheSame( *_pGameMode, sizeof( float32 ), kArrRemovedField );
    _manager.endPlay();
}
