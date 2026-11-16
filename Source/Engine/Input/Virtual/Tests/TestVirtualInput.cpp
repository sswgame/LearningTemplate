// VirtualInputScriptTest · InputReplayTest — 가상 입력 원천: 프레임 순서 · 탭 · 마우스 위치 · 밀기, 녹화 · 시작 프레임 · 파일 왕복과 망가진 파일 거절(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Virtual/InputReplay.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

using namespace sw;

namespace
{
    vector<RawInputEvent> emit( IVirtualInputSource& source, uint32 frameIndex )
    {
        vector<RawInputEvent> listEvent;
        source.emitFrame( frameIndex, listEvent );
        return listEvent;
    }

    /** @brief 파일 머리말(매직 4 + 버전 4 + 이름 길이 4 + 프레임 수 4)의 프레임 수를 고칩니다. */
    void writeFrameCount( vector<uint8>& inoutBytes, uint32 frameCount )
    {
        constexpr size_t kFrameCountOffset = 12;
        for ( size_t index = 0; index < sizeof( uint32 ); ++index )
        {
            inoutBytes[kFrameCountOffset + index] = static_cast<uint8>( frameCount >> ( index * 8 ) );
        }
    }
} // namespace

/** @brief [VirtualInputScriptTest] 이벤트는 그 프레임에만 나오고, 같은 프레임은 더한 순서를 지킨다. 뒤늦게 앞 프레임에 더해도 순서가 맞다 */
SW_TEST_CASE( VirtualInputScriptTest, EmitsInFrameOrder )
{
    VirtualInputScript script;
    script.addEvent( 5, RawInputEvent::makeKeyDown( Key::A ) );
    script.addEvent( 5, RawInputEvent::makeKeyDown( Key::B ) );
    script.addEvent( 2, RawInputEvent::makeKeyDown( Key::C ) );
    SW_EXPECT_EQUAL( uint32( 3 ), script.getEntryCount() );

    SW_EXPECT_TRUE( emit( script, 0 ).empty() );
    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 2 ).size() );
    SW_EXPECT_TRUE( emit( script, 3 ).empty() );
    const vector<RawInputEvent> listEvent = emit( script, 5 );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._payload._keyData._key == Key::A );
    SW_EXPECT_TRUE( listEvent[1]._payload._keyData._key == Key::B );

    SW_EXPECT_FALSE( script.isFinished( 5 ) );
    SW_EXPECT_TRUE( script.isFinished( 6 ) );

    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 2 ).size() ); // 되감아도 다시 나온다
    script.clear();
    SW_EXPECT_TRUE( script.isFinished( 0 ) );
}

/** @brief [VirtualInputScriptTest] 탭은 누르고 그 프레임 수 뒤에 뗀다. 만들 수 없는 슬롯은 거절하고 아무것도 더하지 않는다 */
SW_TEST_CASE( VirtualInputScriptTest, TapsAndBadSlots )
{
    VirtualInputScript script;
    SW_REQUIRE_TRUE( script.addTap( 1, InputSlot::fromKey( Key::A ), 2 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 1 ).size() );
    SW_EXPECT_TRUE( emit( script, 2 ).empty() );
    const vector<RawInputEvent> listUp = emit( script, 3 );
    SW_REQUIRE_EQUAL( size_t( 1 ), listUp.size() );
    SW_EXPECT_TRUE( listUp[0]._type == RawInputEventType::KeyUp );

    VirtualInputScript sameFrame;
    SW_REQUIRE_TRUE( sameFrame.addTap( 1, InputSlot::fromKey( Key::A ), 0 ) );
    SW_EXPECT_EQUAL( size_t( 2 ), emit( sameFrame, 1 ).size() );

    VirtualInputScript bad;
    SW_EXPECT_FALSE( bad.addSlot( 1, InputSlot{ InputDeviceKind::Keyboard, 0, static_cast<uint16>( Key::Count ) }, true ) );
    SW_EXPECT_FALSE( bad.addTap( 1, InputSlot{ InputDeviceKind::Mouse, 0, static_cast<uint16>( MouseButton::Count ) } ) );
    SW_EXPECT_FALSE( bad.addSlot( 1, InputSlot{ InputDeviceKind::Gamepad, 0, static_cast<uint16>( GamepadButton::Count ) }, true ) );
    SW_EXPECT_EQUAL( uint32( 0 ), bad.getEntryCount() );
}

/** @brief [VirtualInputScriptTest] 마우스 버튼은 그때까지의 마지막 위치에서 눌린다. 같은 프레임이면 먼저 더한 이동까지 본다 */
SW_TEST_CASE( VirtualInputScriptTest, MouseButtonUsesLastPosition )
{
    VirtualInputScript script;
    SW_REQUIRE_TRUE( script.addSlot( 0, InputSlot::fromMouseButton( MouseButton::Left ), true ) );
    script.addMousePosition( 3, 10, 20 );
    SW_REQUIRE_TRUE( script.addSlot( 3, InputSlot::fromMouseButton( MouseButton::Left ), false ) );
    script.addMousePosition( 9, 70, 80 );
    SW_REQUIRE_TRUE( script.addSlot( 5, InputSlot::fromMouseButton( MouseButton::Right ), true ) ); // 앞 프레임에 끼운다

    const vector<RawInputEvent> listFirst = emit( script, 0 );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFirst.size() );
    SW_EXPECT_EQUAL( 0, listFirst[0]._payload._mouseData._x );

    const vector<RawInputEvent> listThird = emit( script, 3 );
    SW_REQUIRE_EQUAL( size_t( 2 ), listThird.size() );
    SW_EXPECT_EQUAL( 10, listThird[1]._payload._mouseData._x );
    SW_EXPECT_EQUAL( 20, listThird[1]._payload._mouseData._y );

    const vector<RawInputEvent> listFifth = emit( script, 5 );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFifth.size() );
    SW_EXPECT_EQUAL( 10, listFifth[0]._payload._mouseData._x ); // 프레임 9 의 이동은 아직이다
}

/** @brief [VirtualInputScriptTest] 밀기: 그 프레임 이후만 뒤로 간다. 이미 낸 이벤트는 다시 나오지 않는다 */
SW_TEST_CASE( VirtualInputScriptTest, DelayMovesOnlyLaterEvents )
{
    VirtualInputScript script;
    script.addEvent( 1, RawInputEvent::makeKeyDown( Key::A ) );
    script.addEvent( 4, RawInputEvent::makeKeyDown( Key::B ) );
    script.addEvent( 6, RawInputEvent::makeKeyDown( Key::C ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 1 ).size() );

    script.delayEventsFrom( 2, 10 );
    SW_EXPECT_TRUE( emit( script, 4 ).empty() );
    SW_EXPECT_TRUE( emit( script, 6 ).empty() );
    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 14 ).size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), emit( script, 16 ).size() );
    SW_EXPECT_TRUE( script.isFinished( 17 ) );
}

/** @brief [InputReplayTest] 녹화 중에만 프레임이 쌓이고, 재생은 프레임 번호로 그 이벤트를 낸다. 길이를 더하고 끝을 안다 */
SW_TEST_CASE( InputReplayTest, RecordsAndEmits )
{
    InputReplay                 replay;
    const vector<RawInputEvent> listEvent{ RawInputEvent::makeKeyDown( Key::A ) };
    replay.recordFrame( 0.5f, listEvent ); // 아직 녹화 전이다
    SW_EXPECT_EQUAL( uint32( 0 ), replay.getFrameCount() );

    replay.startRecording();
    SW_EXPECT_EQUAL( string( "NewReplay" ), replay.getReplayName() );
    SW_EXPECT_TRUE( replay.isRecording() );
    replay.recordFrame( 0.25f, listEvent );
    replay.recordFrame( 0.5f, {} );
    replay.stopRecording();
    replay.recordFrame( 0.5f, listEvent ); // 멈춘 뒤다

    SW_EXPECT_EQUAL( uint32( 2 ), replay.getFrameCount() );
    SW_EXPECT_NEAR( 0.75f, replay.getTotalDuration(), 1.0e-5f );
    SW_EXPECT_EQUAL( size_t( 1 ), emit( replay, 0 ).size() );
    SW_EXPECT_TRUE( emit( replay, 1 ).empty() );
    SW_EXPECT_TRUE( emit( replay, 2 ).empty() ); // 끝 뒤다
    SW_EXPECT_FALSE( replay.isFinished( 1 ) );
    SW_EXPECT_TRUE( replay.isFinished( 2 ) );

    replay.startRecording( "again" ); // 있던 프레임을 지운다
    SW_EXPECT_EQUAL( uint32( 0 ), replay.getFrameCount() );
}

/** @brief [InputReplayTest] 상한을 준 녹화는 그 프레임 수를 채운 순간 스스로 멈춘다(버그 리포트 세션 기록의 고정 용량). 다시 시작하면 상한도 새로 정한다 */
SW_TEST_CASE( InputReplayTest, FrameLimitStopsRecording )
{
    InputReplay replay;
    replay.startRecording( "capped", 2 );
    replay.recordFrame( 0.1f, {} );
    SW_EXPECT_TRUE( replay.isRecording() );
    replay.recordFrame( 0.1f, {} );
    SW_EXPECT_FALSE( replay.isRecording() );
    replay.recordFrame( 0.1f, {} ); // 상한 뒤다
    SW_EXPECT_EQUAL( uint32( 2 ), replay.getFrameCount() );

    replay.startRecording( "unlimited" );
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        replay.recordFrame( 0.1f, {} );
    }
    SW_EXPECT_TRUE( replay.isRecording() );
    SW_EXPECT_EQUAL( uint32( 5 ), replay.getFrameCount() );
}

/** @brief [InputReplayTest] 파일: 이름 · 시간 · 이벤트가 되돌아온다 */
SW_TEST_CASE( InputReplayTest, FileRoundTrips )
{
    const string path = test::makeScratchPath( "roundtrip.swreplay" );
    InputReplay  replay;
    replay.startRecording( "demo" );
    replay.recordFrame( 0.25f, { RawInputEvent::makeKeyDown( Key::A ), RawInputEvent::makeMouseMove( 3, 4 ) } );
    replay.recordFrame( 0.5f, {} );
    replay.stopRecording();
    SW_REQUIRE_TRUE( replay.saveToFile( *AbsolutePath::tryMake( path ) ) );

    InputReplay loaded;
    SW_REQUIRE_TRUE( loaded.loadFromFile( path ) );
    SW_EXPECT_EQUAL( string( "demo" ), loaded.getReplayName() );
    SW_REQUIRE_EQUAL( uint32( 2 ), loaded.getFrameCount() );
    SW_EXPECT_NEAR( 0.25f, loaded.getFrames()[0]._deltaTime, 1.0e-5f );
    SW_REQUIRE_EQUAL( size_t( 2 ), loaded.getFrames()[0]._listRawEvent.size() );
    SW_EXPECT_TRUE( loaded.getFrames()[0]._listRawEvent[0]._payload._keyData._key == Key::A );
    SW_EXPECT_EQUAL( 3, loaded.getFrames()[0]._listRawEvent[1]._payload._mouseData._x );
}

/**
 * @brief [InputReplayTest] 저장 → 읽기 → 저장이 같은 바이트다 - Input Map Editor 의 녹화 Save · Load 왕복(편집기 저장 왕복 시나리오의 녹화 몫)
 * @details 녹화 파일은 패널 상태가 없는 형식이라 시나리오가 아니라 여기서 본다. 읽기가 값을 잃거나 바꾸면 둘째 저장이 달라진다.
 */
SW_TEST_CASE( InputReplayTest, SaveLoadSaveKeepsBytes )
{
    const string firstPath  = test::makeScratchPath( "first.swreplay" );
    const string secondPath = test::makeScratchPath( "second.swreplay" );
    InputReplay  replay;
    replay.startRecording( "roundtrip" );
    replay.recordFrame( 0.016f, { RawInputEvent::makeKeyDown( Key::Space ), RawInputEvent::makeMouseMove( -7, 12 ) } );
    replay.recordFrame( 0.033f, {} );
    replay.recordFrame( 0.5f, { RawInputEvent::makeKeyDown( Key::A ) } );
    replay.stopRecording();
    SW_REQUIRE_TRUE( replay.saveToFile( *AbsolutePath::tryMake( firstPath ) ) );

    InputReplay loaded;
    SW_REQUIRE_TRUE( loaded.loadFromFile( firstPath ) );
    SW_REQUIRE_TRUE( loaded.saveToFile( *AbsolutePath::tryMake( secondPath ) ) );

    vector<uint8> firstBytes;
    vector<uint8> secondBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( firstPath, firstBytes ) );
    SW_REQUIRE_TRUE( FileUtil::readFile( secondPath, secondBytes ) );
    SW_EXPECT_TRUE( firstBytes.empty() == false );
    SW_EXPECT_TRUE( firstBytes == secondBytes );
}

/** @brief [InputReplayTest] 망가진 파일은 거절하고 있던 녹화를 지킨다 - 없는 파일 · 매직 · 버전 · 잘린 이름과 프레임 · 터무니없는 프레임 수(큰 할당을 하지 않는다) */
SW_TEST_CASE( InputReplayTest, BrokenFilesAreRefused )
{
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "InputReplay.InputReplayVersionNot" );
    SW_EXPECT_LOG( "InputReplay.InputReplayTruncatedName" );
    SW_EXPECT_LOG( "InputReplay.InputReplayTruncatedFrame" );
    const string path = test::makeScratchPath( "broken.swreplay" );
    InputReplay  replay;
    replay.startRecording( "kept" );
    replay.recordFrame( 0.25f, { RawInputEvent::makeKeyDown( Key::A ) } );
    replay.stopRecording();
    SW_REQUIRE_TRUE( replay.saveToFile( *AbsolutePath::tryMake( path ) ) );
    vector<uint8> good;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, good ) );

    const auto refuses = [&]( const vector<uint8>& bytes )
    {
        SW_REQUIRE_TRUE( FileUtil::writeFile( path, bytes.data(), bytes.size() ) );
        SW_EXPECT_FALSE( replay.loadFromFile( path ) );
        SW_EXPECT_EQUAL( string( "kept" ), replay.getReplayName() );
        SW_EXPECT_EQUAL( uint32( 1 ), replay.getFrameCount() );
    };

    SW_EXPECT_FALSE( replay.loadFromFile( test::makeScratchPath( "nothing.swreplay" ) ) );
    refuses( vector<uint8>( good.begin(), good.begin() + 3 ) ); // 머리말이 없다

    vector<uint8> badMagic = good;
    badMagic[0]            = 'X';
    refuses( badMagic );

    vector<uint8> badVersion = good;
    badVersion[4]            = 0xEE;
    refuses( badVersion );

    refuses( vector<uint8>( good.begin(), good.begin() + 17 ) ); // 이름 중간에서 끝난다
    refuses( vector<uint8>( good.begin(), good.end() - 1 ) );    // 이벤트 중간에서 끝난다

    vector<uint8> hugeCount = good;
    writeFrameCount( hugeCount, 0xFFFFFFFFu );
    refuses( hugeCount );

    // 어느 길이로 잘려도 거절한다. 머리말 뒤의 이름 · 프레임 시간 · 이벤트 수는 형식이 정한 차례다.
    for ( size_t length = 1; length < good.size(); ++length )
    {
        refuses( vector<uint8>( good.begin(), good.begin() + static_cast<ptrdiff_t>( length ) ) );
    }

    // 머리말 16 바이트 · 이름 "kept" 뒤가 프레임 시간이고 그 뒤가 이벤트 수다.
    const uint32  nanBits   = 0x7FC00000u;
    const uint32  hugeEvent = 0xFFFFFFFFu;
    vector<uint8> nanDelta  = good;
    Memory::copy( nanDelta.data() + 20, &nanBits, sizeof( nanBits ) );
    refuses( nanDelta );
    vector<uint8> craftedEvents = good;
    Memory::copy( craftedEvents.data() + 24, &hugeEvent, sizeof( hugeEvent ) );
    refuses( craftedEvents );
}
