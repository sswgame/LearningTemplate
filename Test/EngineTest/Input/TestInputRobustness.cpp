#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputReplay.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/VirtualInputScript.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 입력 경로의 튼튼함 — 동시성 스트레스 · 리플레이 라운드트립 · 경계값.
namespace
{
    struct TestInputRobustnessInternal
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        /** @brief 4 프레임에 Space 를 누르고 6 프레임에 떼는 입력을 가상 키보드로 넣어 8 프레임을 녹화한다(녹화는 실제로 적용된 사건). */
        static void recordSpaceTap( sw::InputManager& input, sw::InputReplay& outReplay )
        {
            sw::VirtualInputScript source;
            SW_EXPECT_TRUE( source.addTap( 4, sw::InputSlot::fromKey( sw::Key::Space ), 2 ) );
            input.attachVirtualInput( &source );
            outReplay.startRecording( "jump" );
            for ( uint32 frame = 0; frame < 8; ++frame )
            {
                input.beginFrame( kFrameSeconds );
                outReplay.recordFrame( kFrameSeconds, input.getLastFrameEvents() );
                input.endFrame();
            }
            outReplay.stopRecording();
            input.detachVirtualInput();
        }
    };
} // namespace

/**
 * @brief [InputReplayTest] 녹화는 프레임마다 장치에 적용된 사건을 그 자리(프레임 번호)에 적는다
 */
SW_TEST_CASE( InputReplayTest, RecordingKeepsAppliedEventsPerFrame )
{
    using Internal = TestInputRobustnessInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputReplay replay;
    SW_EXPECT_FALSE( replay.isRecording() );
    Internal::recordSpaceTap( input, replay );

    SW_EXPECT_FALSE( replay.isRecording() );
    SW_EXPECT_EQUAL( "jump", replay.getReplayName() );
    SW_ASSERT_EQUAL( 8u, replay.getFrameCount() );
    SW_EXPECT_NEAR_EQUAL( 8.0f * Internal::kFrameSeconds, replay.getTotalDuration(), 0.001f );
    for ( uint32 frame = 0; frame < 8; ++frame )
    {
        const uint32 expectedCount = ( frame == 4 || frame == 6 ) ? 1u : 0u;
        SW_EXPECT_EQUAL( expectedCount, static_cast<uint32>( replay.getFrames()[frame]._listRawEvent.size() ) );
    }
    SW_EXPECT_TRUE( replay.getFrames()[4]._listRawEvent[0]._type == sw::RawInputEventType::KeyDown );
    SW_EXPECT_TRUE( replay.getFrames()[6]._listRawEvent[0]._type == sw::RawInputEventType::KeyUp );
    // 녹화에는 출처 표시를 남기지 않는다 — 재생 때 다시 가상 사건으로 표시된다.
    SW_EXPECT_TRUE( replay.getFrames()[4]._listRawEvent[0]._bSynthetic == SW_FALSE );
    input.shutdown();
}

/**
 * @brief [InputReplayTest] 녹화한 사건을 붙여 재생하면 프레임 시간과 상관없이 같은 프레임에 같은 액션이 난다
 * @details 재생 프레임은 붙인 뒤 `beginFrame` 횟수다 — 벽시계 누산으로 고르면 0.1 초 프레임에서 첫 프레임에 여러 녹화 프레임이 한꺼번에 나온다.
 */
SW_TEST_CASE( InputReplayTest, ReplayEmitsRecordedFramesByIndex )
{
    using Internal = TestInputRobustnessInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed );
    sw::InputReplay replay;
    Internal::recordSpaceTap( input, replay );

    input.attachVirtualInput( &replay );
    for ( uint32 frame = 0; frame < 8; ++frame )
    {
        input.beginFrame( 0.1f );
        SW_EXPECT_EQUAL( frame == 4, input.getInputMap().wasActionTriggered( "Jump" ) );
        input.endFrame();
    }
    SW_EXPECT_TRUE( replay.isFinished( input.getVirtualFrameIndex() ) );
    input.detachVirtualInput();
    input.shutdown();
}

/**
 * @brief [InputReplayTest] seekTo 는 그 프레임 직전 장치 상태를 만들고, 상태를 지우지 않고 붙이면 그 프레임부터 이어 낸다
 */
SW_TEST_CASE( InputReplayTest, ReplaySeekRebuildsDeviceState )
{
    using Internal = TestInputRobustnessInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    input.getInputMap().bind( "Jump", sw::Key::Space, sw::ActionTrigger::Released );
    sw::InputReplay replay;
    Internal::recordSpaceTap( input, replay );

    replay.seekTo( input, 5 ); // 4 에 누름 — 5 직전에는 눌린 채
    SW_EXPECT_TRUE( input.getKeyboard()->isKeyDown( sw::Key::Space ) );
    SW_EXPECT_EQUAL( 5u, replay.getStartFrameIndex() );

    // 이어 붙이면 원천 프레임 1 이 녹화 프레임 6(뗌)이다.
    input.attachVirtualInput( &replay, sw::VirtualInputMode::Exclusive, false );
    input.beginFrame( Internal::kFrameSeconds ); // 녹화 5
    SW_EXPECT_TRUE( input.getKeyboard()->isKeyDown( sw::Key::Space ) );
    input.endFrame();
    input.beginFrame( Internal::kFrameSeconds ); // 녹화 6 — 뗌
    SW_EXPECT_FALSE( input.getKeyboard()->isKeyDown( sw::Key::Space ) );
    SW_EXPECT_TRUE( input.getInputMap().wasActionTriggered( "Jump" ) );
    input.endFrame();
    input.detachVirtualInput();

    replay.seekTo( input, 7 ); // 6 에 뗌 — 7 직전에는 떼어짐
    SW_EXPECT_FALSE( input.getKeyboard()->isKeyDown( sw::Key::Space ) );
    replay.seekTo( input, 99 ); // 끝을 넘으면 끝으로
    SW_EXPECT_EQUAL( replay.getFrameCount(), replay.getStartFrameIndex() );
    input.shutdown();
}

/**
 * @brief [InputReplayTest] 리플레이 파일은 프레임 · 원시 이벤트를 그대로 되읽고, 다른 판(3)의 파일과 잘린 파일은 읽지 않는다
 * @details 파일은 `RawInputEvent` 를 구조체째로 적으므로 배치가 바뀐 판은 옛 판의 바이트를 지금 배치로 읽으면 안 된다.
 */
SW_TEST_CASE( InputReplayTest, FileRoundTripKeepsEventsAndRejectsOtherVersion )
{
    using Internal = TestInputRobustnessInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputReplay recorded;
    Internal::recordSpaceTap( input, recorded );

    const sw::string path = test::makeTempPath( "roundtrip.swreplay" );
    SW_ASSERT_TRUE( recorded.saveToFile( path ) );

    sw::InputReplay loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );
    SW_EXPECT_EQUAL( "jump", loaded.getReplayName() );
    SW_ASSERT_EQUAL( 8u, loaded.getFrameCount() );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( loaded.getFrames()[4]._listRawEvent.size() ) );
    SW_EXPECT_TRUE( loaded.getFrames()[4]._listRawEvent[0]._type == sw::RawInputEventType::KeyDown );
    SW_EXPECT_TRUE( loaded.getFrames()[4]._listRawEvent[0]._payload._keyData._key == sw::Key::Space );

    // 읽은 파일로 seekTo 해도 같은 상태다.
    loaded.seekTo( input, 5 );
    SW_EXPECT_TRUE( input.getKeyboard()->isKeyDown( sw::Key::Space ) );
    input.shutdown();

    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( path, bytes ) );

    // 잘린 파일(마지막 사건 반쪽)은 거절하고 읽던 리플레이를 그대로 둔다.
    {
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, bytes.data(), bytes.size() - sizeof( sw::RawInputEvent ) / 2 ) );
        SW_TEST_DEFENSIVE_SCOPE( "truncated replay file" );
        SW_EXPECT_FALSE( loaded.loadFromFile( path ) );
        SW_EXPECT_EQUAL( 8u, loaded.getFrameCount() );
    }

    // 머리말의 판(매직 4 바이트 뒤 uint32)을 3 으로 바꾼 파일은 거절한다.
    const uint32 otherVersion = 3;
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
 * @brief [InputEdgeCaseTest] 리플레이 경계 — 빈 리플레이의 탐색 · 재생, 없는 파일
 */
SW_TEST_CASE( InputEdgeCaseTest, ReplayBoundarySeekingAndMissingFile )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputReplay replay;

    // 빈 리플레이: 탐색은 0 에 머물고, 붙여도 아무것도 내지 않고 바로 끝이다.
    replay.seekTo( input, 100 );
    SW_EXPECT_EQUAL( 0u, replay.getStartFrameIndex() );
    SW_EXPECT_TRUE( replay.isFinished( 0 ) );
    input.attachVirtualInput( &replay );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( input.getLastFrameEvents().empty() );
    input.endFrame();
    input.detachVirtualInput();

    // 녹화 중이 아니면 프레임을 적지 않는다.
    replay.recordFrame( 0.016f, {} );
    SW_EXPECT_EQUAL( 0u, replay.getFrameCount() );

    SW_EXPECT_FALSE( replay.loadFromFile( "non_existent_file.swreplay" ) );
    input.shutdown();
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
