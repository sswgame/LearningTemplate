// KartGameModeTest — 카트 경기 디렉터: 열기 전 명령 거절, 사람 차의 운전 명령, 상태 왕복, 자동 플레이로 끝까지 달린 경기의 순위 · 상금 · 끝내기(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Component/KartGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/GameModeStateVersionTest.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Presentation/Tests/RecordingSoundPlayer.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

using namespace sw;

namespace
{
    constexpr float32 kKartFrame     = 1.0f / 60.0f;
    constexpr int32   kMaxRaceFrames = 60 * 120; ///< 한 바퀴 경기가 끝나고도 남는 시뮬레이션 시간(프레임) — 벽시계와 무관하다

    constexpr const utf8* kKartTrackXML   = R"(<KartTrackCatalog><Track id="ring" name="Ring" width="12" laps="1" samples="16">
        <Point x="40" z="0"/><Point x="0" z="40"/><Point x="-40" z="0"/><Point x="0" z="-40"/></Track></KartTrackCatalog>)";
    constexpr const utf8* kKartVehicleXML = R"(<KinematicVehicleSettings _maxSpeed="30" _maxDriveForce="3600"/>)";

    /** @brief 고리 트랙 카탈로그를 건 게임 범위와 디렉터 하나입니다. */
    struct KartGameModeTest
    {
        const test::ScopedScratchDomain   _domain;
        KartTrackCatalog                  _tracks{};
        RecordingSoundPlayer              _soundPlayer{};
        SubsystemScope                    _scope{};
        unique_ptr<game::ScopedGameScope> _pScopeBinding{};
        GameObjectManager                 _manager{};
        KartGameModeComponent*            _pGameMode{ nullptr };

        KartGameModeTest()
        {
            SW_REQUIRE_TRUE( _tracks.loadFromXMLText( kKartTrackXML, "KartGameModeTest" ) );
            SW_REQUIRE_TRUE( _scope.bind<ISoundPlayer>( &_soundPlayer ) && _scope.bind<const KartTrackCatalog>( &_tracks ) );
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( _domain.makeDiskPath( "kart.vehicle.xml" ), kKartVehicleXML ) );
            _pScopeBinding = make_unique<game::ScopedGameScope>( _scope );
            _pGameMode     = _manager.createGameObject( hashed_string( "Director" ) )->addComponent<KartGameModeComponent>();
            SW_REQUIRE_NOT_NULL( _pGameMode );
            set( "_trackID", "ring" );
            set( "_vehicle", test::ScopedScratchDomain::makeID( "kart.vehicle.xml" ).str() );
            set( "_bItems", "false" );
            set( "_countdownSeconds", "0" );
            set( "_finishTimeoutSeconds", "5" );
            set( "_resultHoldSeconds", "0.5" );
        }

        void set( const utf8* pProperty, string_view text )
        {
            const PropertyInfo* const pInfo = KartGameModeComponent::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            SW_REQUIRE_NOT_NULL( pInfo );
            SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( *pInfo, MutableObjectAddress{ _pGameMode }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
        }

        void advance( int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kKartFrame );
            }
        }
    };
} // namespace

/** @brief [KartGameModeTest] 경기가 열리기 전의 운전 명령은 거절한다 — 클라이언트가 먼저 와도 경기를 바꾸지 않는다 */
SW_TEST_CASE( KartGameModeTest, CommandsBeforeTheRaceOpensAreRefused )
{
    GameObjectManager      manager;
    KartGameModeComponent* pGameMode = manager.createGameObject( hashed_string( "Director" ) )->addComponent<KartGameModeComponent>();
    SW_REQUIRE_NOT_NULL( pGameMode );
    SW_EXPECT_FALSE( pGameMode->isAcceptingInput() );
    SW_EXPECT_FALSE( pGameMode->submitDriverInput( KartRacerInput{} ) );
    SW_EXPECT_EQUAL( 0, pGameMode->getPlayerPlace() );
    SW_EXPECT_EQUAL( 0, pGameMode->getFinishedCount() );
}

/** @brief [KartGameModeTest] 카탈로그에 없는 트랙이면 오류를 남기고 경기를 열지 않는다 */
SW_TEST_CASE_F( KartGameModeTest, UnknownTrackDoesNotOpen )
{
    SW_EXPECT_LOG( "KartRacing.TrackUnusable" );
    set( "_trackID", "nowhere" );
    _manager.beginPlay();
    SW_EXPECT_FALSE( _pGameMode->isStarted() );
    _manager.endPlay();
}

/** @brief [KartGameModeTest] 아이템을 켠 채 아이템 파일을 적지 않으면 오류를 남기고 경기를 열지 않는다 — 빈 칸이 아이템 없는 경기를 뜻하지 않는다 */
SW_TEST_CASE_F( KartGameModeTest, ItemsOnWithoutAFileDoesNotOpen )
{
    SW_EXPECT_LOG( "GameMode.RequiredAssetMissing" );
    set( "_bItems", "true" );
    _manager.beginPlay();
    SW_EXPECT_FALSE( _pGameMode->isStarted() );
    _manager.endPlay();
}

/** @brief [KartGameModeTest] 사람 차는 명령으로 달리고, 쓴 상태를 되살리면 같은 자리 · 같은 순위로 이어진다 */
SW_TEST_CASE_F( KartGameModeTest, DriverCommandsMoveTheKartAndTheStateRoundTrips )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_REQUIRE_TRUE( _pGameMode->isAcceptingInput() );
    const float3   start = _pGameMode->getRacerPosition( KartGameModeComponent::kPlayerRacer )._value;
    KartRacerInput input;
    input._vehicle._throttle = 1.0f;
    for ( int32 frame = 0; frame < 60; ++frame )
    {
        SW_EXPECT_TRUE( _pGameMode->submitDriverInput( input ) );
        advance( 1 );
    }
    const float3 driven = _pGameMode->getRacerPosition( KartGameModeComponent::kPlayerRacer )._value;
    SW_EXPECT_TRUE( float3::getDistance( start, driven ) > 2.0f );
    SW_EXPECT_TRUE( _pGameMode->getPlayerSpeed() > 2.0f );

    Archive saved;
    _pGameMode->writeState( saved );
    vector<uint8> bytes;
    saved.writeData( bytes );
    const int32 place = _pGameMode->getPlayerPlace();
    advance( 30 );
    SW_EXPECT_TRUE( float3::getDistance( driven, _pGameMode->getRacerPosition( KartGameModeComponent::kPlayerRacer )._value ) > 0.1f );
    _pGameMode->restoreState( std::move( bytes ) );
    SW_EXPECT_NEAR( 0.0f, float3::getDistance( driven, _pGameMode->getRacerPosition( KartGameModeComponent::kPlayerRacer )._value ), 1.0e-4f );
    SW_EXPECT_EQUAL( place, _pGameMode->getPlayerPlace() );
    _manager.endPlay();
}

/** @brief [KartGameModeTest] 자동 플레이 경기는 모두 순위를 받고 끝나며, 결과를 보인 뒤 미니게임을 한 번 끝낸다(흐름이 없으면 오류) — 상금은 순위대로다 */
SW_TEST_CASE_F( KartGameModeTest, AutoPlayedRaceEndsWithPlacesAndReward )
{
    SW_EXPECT_LOG( "GameMode.FinishWithoutMinigame" );
    set( "_bAutoPlay", "true" );
    set( "_firstReward", "40" );
    set( "_rewardStep", "10" );
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    SW_EXPECT_FALSE( _pGameMode->isAcceptingInput() ); // AI 가 몬다
    for ( int32 frame = 0; frame < kMaxRaceFrames && _pGameMode->getPhase() != static_cast<int32>( MatchPhase::Ended ); ++frame )
    {
        advance( 1 );
    }
    SW_REQUIRE_EQUAL( static_cast<int32>( MatchPhase::Ended ), _pGameMode->getPhase() );
    SW_EXPECT_EQUAL( _pGameMode->getRace().getRacerCount(), _pGameMode->getFinishedCount() );
    const int32 place = _pGameMode->getPlayerFinishPlace();
    SW_REQUIRE_TRUE( 1 <= place && place <= 4 );
    vector<MinigameResultValue> listValue;
    _pGameMode->collectMinigameResult( listValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listValue.size() );
    SW_EXPECT_EQUAL( int64{ 50 - place * 10 }, listValue[0]._amount );
    advance( 60 ); // 결과 0.5 초를 넘긴다
    _manager.endPlay();
}

/** @brief [KartGameModeTest] 결과 유지 깃발이 있던 판 1 상태는 패치로 올라 읽히고 나머지 값이 그대로다(판 1 의 상태 칸은 `_bResultShown` · `_bFinishRequested` 둘이었다) */
SW_TEST_CASE_F( KartGameModeTest, Version1StateIsPatchedAndReadsTheSame )
{
    _manager.beginPlay();
    SW_REQUIRE_TRUE( _pGameMode->isStarted() );
    advance( 30 );
    constexpr uint8 kArrRemovedField[] = { 0, 0 };
    test::GameModeStateVersionTest::expectVersion1ReadsTheSame( *_pGameMode, 0, kArrRemovedField );
    _manager.endPlay();
}
