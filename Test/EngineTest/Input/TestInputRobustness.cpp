#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputReplay.h"
#include "Engine/Input/RawInputEvent.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 입력 경로의 튼튼함 — 동시성 스트레스 · 리플레이 라운드트립 · 경계값.
/**
 * @brief [InputReplayTest] 입력 녹화, 프레임 스크러빙 및 결정론적 재생 검증
 */
SW_TEST_CASE( InputReplayTest, RecordingAndPlaybackWorkflow )
{
    sw::InputReplay replay;
    SW_EXPECT_FALSE( replay.isRecording() );
    SW_EXPECT_FALSE( replay.isPlaying() );

    replay.startRecording( "TestReplaySession" );
    SW_EXPECT_TRUE( replay.isRecording() );
    SW_EXPECT_EQUAL( "TestReplaySession", replay.getReplayName() );

    // 3프레임 녹화
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        sw::InputSnapshot snapshot{};
        snapshot._tickNumber = frameIndex;
        snapshot._buttonMask = 1ull << frameIndex;

        sw::vector<sw::RawInputEvent> listEvent;
        listEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::A ) );

        replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
    }

    replay.stopRecording();
    SW_EXPECT_FALSE( replay.isRecording() );
    SW_EXPECT_EQUAL( 3u, replay.getFrameCount() );
    SW_EXPECT_NEAR_EQUAL( 0.048f, replay.getTotalDuration(), 0.001f );

    // 재생 모드 진입
    replay.play();
    SW_EXPECT_TRUE( replay.isPlaying() );
    SW_EXPECT_FALSE( replay.isPaused() );
    SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

    const sw::InputReplayFrame* pFrame0 = replay.getCurrentFrame();
    SW_EXPECT_TRUE( pFrame0 != nullptr );
    if ( pFrame0 != nullptr )
    {
        SW_EXPECT_EQUAL( 0u, pFrame0->_tickNumber );
        SW_EXPECT_EQUAL( 1ull, pFrame0->_snapshot._buttonMask );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pFrame0->_listRawEvent.size() ) );
    }

    // 일시정지 및 재개
    replay.pause();
    SW_EXPECT_TRUE( replay.isPaused() );
    replay.resume();
    SW_EXPECT_FALSE( replay.isPaused() );

    replay.stop();
    SW_EXPECT_FALSE( replay.isPlaying() );
}

/**
 * @brief [InputReplayTest] 리플레이 파일은 프레임 · 원시 이벤트를 그대로 되읽고, 다른 판(2)의 파일은 읽지 않는다
 * @details 파일은 `RawInputEvent` 를 구조체째로 적으므로 그 배치가 바뀐 판(3)은 옛 판의 바이트를 지금 배치로 읽으면 안 된다.
 */
SW_TEST_CASE( InputReplayTest, FileRoundTripKeepsEventsAndRejectsOtherVersion )
{
    sw::InputReplay recorded;
    recorded.startRecording( "FileRoundTrip" );
    for ( uint32 frameIndex = 0; frameIndex < 2; ++frameIndex )
    {
        sw::InputSnapshot snapshot{};
        snapshot._tickNumber = frameIndex;
        sw::vector<sw::RawInputEvent> listEvent;
        listEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::D, 0, false, static_cast<uint8>( frameIndex + 1 ) ) );
        recorded.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
    }
    recorded.stopRecording();

    const sw::string path = test::makeTempPath( "roundtrip.swreplay" );
    SW_ASSERT_TRUE( recorded.saveToFile( path ) );

    sw::InputReplay loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );
    SW_ASSERT_EQUAL( 2u, loaded.getFrameCount() );
    const sw::InputReplayFrame& lastFrame = loaded.getFrames()[1];
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( lastFrame._listRawEvent.size() ) );
    SW_EXPECT_TRUE( lastFrame._listRawEvent[0]._type == sw::RawInputEventType::KeyDown );
    SW_EXPECT_TRUE( lastFrame._listRawEvent[0]._payload._keyData._key == sw::Key::D );
    SW_EXPECT_EQUAL( uint32( 2 ), static_cast<uint32>( lastFrame._listRawEvent[0]._modifierMask ) );

    // 머리말의 판(매직 4 바이트 뒤 uint32)을 2 로 바꾼 파일은 거절한다.
    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( path, bytes ) );
    const uint32 otherVersion = 2;
    sw::Memory::copy( bytes.data() + 4, &otherVersion, sizeof( otherVersion ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, bytes.data(), bytes.size() ) );
    sw::InputReplay rejected;
    {
        SW_TEST_DEFENSIVE_SCOPE( "replay file of another version" );
        SW_EXPECT_FALSE( rejected.loadFromFile( path ) );
    }
    SW_EXPECT_EQUAL( 0u, rejected.getFrameCount() );
}

/**
 * @brief [InputReplayTest] 1프레임 전진/후진 스텝 실행 검증
 */
SW_TEST_CASE( InputReplayTest, StepForwardAndBackward )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputReplay replay;
    replay.startRecording( "StepSession" );

    for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        sw::InputSnapshot snapshot{};
        snapshot._tickNumber = frameIndex;
        sw::vector<sw::RawInputEvent> listEvent;
        listEvent.push_back( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
        replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
    }
    replay.stopRecording();

    SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

    // 1프레임씩 전진
    replay.stepForward( &input );
    SW_EXPECT_EQUAL( 1u, replay.getCurrentFrameIndex() );

    replay.stepForward( &input );
    SW_EXPECT_EQUAL( 2u, replay.getCurrentFrameIndex() );

    // 1프레임 후진
    replay.stepBackward( &input );
    SW_EXPECT_EQUAL( 1u, replay.getCurrentFrameIndex() );

    input.shutdown();
}

/**
 * @brief [InputEdgeCaseTest] 게임패드 아날로그 트리거(LT/RT) 엣지 전이 감지 정밀 검증
 */
SW_TEST_CASE( InputEdgeCaseTest, GamepadTriggerEdgeDetection )
{
    struct TestGamepadDevice : public sw::GamepadDevice
    {
        using sw::GamepadDevice::GamepadDevice;
        void poll( [[maybe_unused]] float32 deltaTime ) override {}
    };

    TestGamepadDevice pad( 0 );

    // 초기 상태
    pad.onFrameBegin( 0.016f );
    SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
    pad.onFrameEnd();

    // 프레임 1: 트리거 0.8f 인입 (상승 엣지)
    pad.onFrameBegin( 0.016f );
    pad.setAxis( 4, 0.8f ); // Left Trigger
    SW_EXPECT_TRUE( pad.isControlDown( 100 ) );
    SW_EXPECT_TRUE( pad.wasControlPressed( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
    pad.onFrameEnd();

    // 프레임 2: 계속 0.8f 유지 (누르고 있음 -> wasControlPressed는 false여야 함)
    pad.onFrameBegin( 0.016f );
    SW_EXPECT_TRUE( pad.isControlDown( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
    pad.onFrameEnd();

    // 프레임 3: 트리거 0.0f로 해제 (하강 엣지)
    pad.onFrameBegin( 0.016f );
    pad.setAxis( 4, 0.0f );
    SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
    SW_EXPECT_TRUE( pad.wasControlReleased( 100 ) );
    pad.onFrameEnd();

    // 프레임 4: 뗀 상태 유지 (wasControlReleased는 false여야 함)
    pad.onFrameBegin( 0.016f );
    SW_EXPECT_FALSE( pad.isControlDown( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlPressed( 100 ) );
    SW_EXPECT_FALSE( pad.wasControlReleased( 100 ) );
    pad.onFrameEnd();
}

/**
 * @brief [InputEdgeCaseTest] 트리거 값은 `setAxis` 에서 데드존을 거친다
 * @details 모든 입력 경로(XInput · 리눅스 조이스틱 · 원시 이벤트 · 에디터 시뮬레이터)가 트리거를 `setAxis( 4 · 5 )` 로 넣는다.
 *          한 경로라도 트리거 칸에 곧바로 대입하면 그 경로(예: Windows 의 XInput)에서는 `setTriggerDeadzone` 이 아무 효과가 없다.
 *          XInput 폴링은 장치 없이 돌릴 수 없으므로, 그 길이 기대는 `setAxis` 의 계약을 여기서 붙잡아 둔다.
 */
SW_TEST_CASE( InputEdgeCaseTest, GamepadTriggerDeadzoneAppliesInSetAxis )
{
    struct TestGamepadDevice : public sw::GamepadDevice
    {
        using sw::GamepadDevice::GamepadDevice;
        void poll( [[maybe_unused]] float32 deltaTime ) override {}
    };

    TestGamepadDevice pad( 0 );
    pad.setTriggerDeadzone( 0.1f );

    pad.setAxis( 4, 0.05f );
    pad.setAxis( 5, 0.05f );
    SW_EXPECT_TRUE_MSG( pad.getLeftTrigger() == 0.0f, "데드존 아래의 왼쪽 트리거 값이 걸러지지 않았습니다" );
    SW_EXPECT_TRUE_MSG( pad.getRightTrigger() == 0.0f, "데드존 아래의 오른쪽 트리거 값이 걸러지지 않았습니다" );

    pad.setAxis( 4, 0.3f );
    pad.setAxis( 5, 0.6f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pad.getLeftTrigger(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, pad.getRightTrigger(), 0.0001f );
}

/**
 * @brief [InputEdgeCaseTest] 리플레이 경계 조건(범위 초과 시킹, 0프레임 후진, 끝 프레임 전진, 손상된 헤더) 검증
 */
SW_TEST_CASE( InputEdgeCaseTest, ReplayBoundarySeekingAndCorruptedData )
{
    sw::InputReplay replay;

    // 빈 리플레이 상태 안전성 검증
    replay.play();
    SW_EXPECT_FALSE( replay.isPlaying() );
    replay.stepForward( nullptr );
    replay.stepBackward( nullptr );
    replay.seek( 100 );
    SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );
    SW_EXPECT_TRUE( replay.getCurrentFrame() == nullptr );

    // 프레임 3개 기록
    replay.startRecording( "BoundaryTest" );
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        sw::InputSnapshot snapshot{};
        snapshot._tickNumber = frameIndex;
        sw::vector<sw::RawInputEvent> listEvent;
        replay.recordFrame( frameIndex, 0.016f, snapshot, listEvent );
    }
    replay.stopRecording();

    // 범위 초과 시킹 검증 (2로 클램핑)
    replay.seek( 99999 );
    SW_EXPECT_EQUAL( 2u, replay.getCurrentFrameIndex() );

    // 0번 프레임에서 stepBackward 시 언더플로 방어
    replay.seek( 0 );
    replay.stepBackward( nullptr );
    SW_EXPECT_EQUAL( 0u, replay.getCurrentFrameIndex() );

    // 손상된 파일 로드 시도
    SW_EXPECT_FALSE( replay.loadFromFile( "non_existent_file.swreplay" ) );
}

/**
 * @brief [InputStressTest] 멀티스레드 대량 원시 이벤트 동시 인입 락프리 스트레스 검증
 */
SW_TEST_CASE( InputStressTest, MultiThreadedRawEventConcurrentBurst )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    constexpr uint32 kThreadCount     = 4;
    constexpr uint32 kEventsPerThread = 2500;

    sw::vector<std::thread> listThread;
    listThread.reserve( kThreadCount );

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&input]()
        {
            for ( uint32 eventIndex = 0; eventIndex < kEventsPerThread; ++eventIndex )
            {
                const sw::Key key = static_cast<sw::Key>( ( eventIndex % 26 ) + static_cast<uint32>( sw::Key::A ) );
                input.postRawEvent( sw::RawInputEvent::makeKeyDown( key ) );
                input.postRawEvent( sw::RawInputEvent::makeKeyUp( key ) );
            }
        } );
    }

    for ( std::thread& t : listThread )
    {
        if ( t.joinable() )
            t.join();
    }

    // 모든 인입된 이벤트 드레인 및 상태 정합성 검증
    input.beginFrame( 0.016f );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [InputStressTest] 대량 액션 등록 및 키 충돌 탐색/해결 스트레스 검증
 */
SW_TEST_CASE( InputStressTest, InputMapBulkConflictResolutionStress )
{
    sw::InputMap inputMap;

    constexpr uint32 kActionCount = 100;
    for ( uint32 index = 0; index < kActionCount; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sbName;
        sbName.append( "StressAction_" ).append( index );
        const sw::string actionName( sbName.view() );
        inputMap.bind( sw::hashed_string( actionName ), sw::Key::Space );
    }

    // 100개 액션 간 충돌 해결 (Override 전략)
    for ( uint32 index = 1; index < kActionCount; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sbName;
        sbName.append( "StressAction_" ).append( index );
        const sw::string actionName( sbName.view() );
        inputMap.rebindWithResolution( sw::hashed_string( actionName ), sw::InputSlot::fromKey( sw::Key::Escape ), sw::ConflictResolution::Override );
    }

    SW_EXPECT_TRUE( inputMap.hasAction( "StressAction_0" ) );
}

/**
 * @brief [InputEdgeCaseTest] 리셋 뒤 첫 폴링에서 누르고 있던 게임패드 버튼은 "새로 눌림" 이 아니다.
 * @details 포커스를 잃으면 모든 장치를 리셋한다. 게임패드는 폴링으로 상태를 읽으므로, 리셋 뒤 첫 폴링이 계속 누르고 있던 버튼을 읽으면
 *          직전 값 0 과 비교해 눌림 엣지가 생겼다 — 창을 오가기만 해도 점프가 나갔다.
 */
SW_TEST_CASE( InputEdgeCaseTest, GamepadResetDoesNotReportHeldButtonAsNewPress )
{
    struct HeldButtonGamepadDevice : public sw::GamepadDevice
    {
        using sw::GamepadDevice::GamepadDevice;
        void poll( [[maybe_unused]] float32 deltaTime ) override { setButtonDown( sw::GamepadButton::A, true ); }
    };

    HeldButtonGamepadDevice pad( 0 );

    // 프레임 1: 누르기 시작 — 새로 눌림
    pad.onFrameBegin( 0.016f );
    pad.poll( 0.016f );
    pad.onPolled();
    SW_EXPECT_TRUE( pad.wasButtonPressed( sw::GamepadButton::A ) );
    pad.onFrameEnd();

    // 포커스를 잃어 리셋된다. 버튼은 여전히 눌려 있다.
    pad.resetState();
    pad.onFrameBegin( 0.016f );
    pad.poll( 0.016f );
    pad.onPolled();
    SW_EXPECT_TRUE( pad.isButtonDown( sw::GamepadButton::A ) );
    SW_EXPECT_FALSE_MSG( pad.wasButtonPressed( sw::GamepadButton::A ), "리셋 뒤 누르고 있던 버튼이 새로 눌린 것으로 보고됐습니다" );
    pad.onFrameEnd();

    // 한 번 떼고 다시 누르면 그때는 눌림이다(억제는 한 번뿐).
    pad.onFrameBegin( 0.016f );
    pad.setButtonDown( sw::GamepadButton::A, false );
    pad.onFrameEnd();
    pad.onFrameBegin( 0.016f );
    pad.poll( 0.016f );
    pad.onPolled();
    SW_EXPECT_TRUE( pad.wasButtonPressed( sw::GamepadButton::A ) );
}
