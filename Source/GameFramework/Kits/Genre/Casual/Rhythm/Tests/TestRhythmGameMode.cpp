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
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/Component/RhythmGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

using namespace sw;

namespace
{
    constexpr float32 kRhythmFrame = 1.0f / 60.0f;

    // 레인 둘. 레인 0 의 롱노트(2 ~ 4 초), 레인 1 의 보통 노트(3 초). 판정은 셋이다.
    constexpr const utf8* kRhythmChartXML = R"(<Chart title="Song" artist="Band" level="1" lanes="2"><Bpm beat="0" bpm="120"/><Note lane="0" beat="4" endBeat="8"/><Note lane="1" beat="6"/></Chart>)";

    /** @brief 채보 파일을 가리키는 디렉터 하나입니다. */
    /** @brief 디렉터의 판 규칙입니다(`stage.scene.xml` 과 같은 값). */
    constexpr const utf8* kJudgeDataXML = R"(<_playSettings _failedRank="F" _missLifeDelta="-4" _maxLife="100" _initialLife="100" _comboBonusPerCombo="0" _comboBonusCap="100" _globalOffset="0" _scrollMode="ConstantSpeed"><_listWindow>
<RhythmJudgeWindow _grade="Cool" _width="0.045" _score="300" _lifeDelta="1" _bBreaksCombo="false" />
<RhythmJudgeWindow _grade="Good" _width="0.09" _score="150" _lifeDelta="0" _bBreaksCombo="false" />
<RhythmJudgeWindow _grade="Bad" _width="0.14" _score="50" _lifeDelta="-2" _bBreaksCombo="true" />
</_listWindow>
<_listRank>
<RhythmRankThreshold _rank="S" _minAccuracy="95" />
<RhythmRankThreshold _rank="A" _minAccuracy="90" />
<RhythmRankThreshold _rank="B" _minAccuracy="80" />
<RhythmRankThreshold _rank="C" _minAccuracy="70" />
<RhythmRankThreshold _rank="D" _minAccuracy="0" />
</_listRank></_playSettings>)";
    /** @brief 디렉터 원소입니다 — 차트 id, 판정 데이터. */
    constexpr const utf8* kDirectorXML = R"(<RhythmGameModeComponent _chart="%#" _resultHoldSeconds="0.5" _clearReward="25">%#</RhythmGameModeComponent>)";

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
            // 판정 창 · 등급 표는 씬 데이터다(필수 칸) — 씬이 적는 것과 같은 모양으로 읽힌다.
            StringBuilder<constant::buffer::kMax2048> xml;
            xml.appendFormat( kDirectorXML, test::ScopedScratchDomain::makeID( "song.chart.xml" ).str(), kJudgeDataXML );
            SW_REQUIRE_TRUE( XMLSerializer::deserialize( _pGameMode, *RhythmGameModeComponent::StaticType(), xml.view() ) );
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

/** @brief [RhythmGameModeTest] 판정 창 · 등급 표를 적지 않은 디렉터 데이터는 읽지 못한다 — 코드 기본 표로 조용히 돌지 않는다 */
SW_TEST_CASE( RhythmGameModeTest, JudgeDataIsRequired )
{
    SW_EXPECT_LOG( "Serialization.RequiredMissing" );
    // 빠진 칸의 초기값(0)은 반사의 `Min` 밖이라 읽기 공통 끝도 칸마다 알린다 — 빠졌다는 오류와 함께 난다.
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    GameObjectManager        manager;
    RhythmGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<RhythmGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    StringBuilder<constant::buffer::kMax2048> xml;
    xml.appendFormat( kDirectorXML, "game/none.chart.xml", "" );
    SW_EXPECT_FALSE( XMLSerializer::deserialize( pGameMode, *RhythmGameModeComponent::StaticType(), xml.view() ) );
}
