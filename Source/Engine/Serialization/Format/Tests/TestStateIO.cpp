#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <cstring>

// StateIOTest — 상태 구간: 기대한 표 · 버전의 구간만 읽히고(다른 버전은 거절), 표로 갈라 읽는 쪽은 아무 구간이나 받는다.
// 누적기: 음수와 터무니없는 값을 거절한다(1e30 은 빼도 줄지 않아 소비 고리가 끝나지 않는다)(nogpu).

/**
 * @brief [StateIOTest] 결함: 걸음 타이머가 음수만 보아, 터무니없는 누적 하나로 `FixedStepTimer::consume` 이 끝나지 않았다. 한 프레임 상한까지만 받고 값을 둔다
 */
SW_TEST_CASE( StateIOTest, StepTimerRefusesAccumulatorPastOneFrame )
{
    const auto readForged = []( float32 accumulator, sw::FixedStepTimer& inoutTimer )
    {
        sw::Archive written;
        written << accumulator;
        sw::Archive reading( written.getData(), written.getSize() );
        return sw::StateIO::readStepTimer( reading, inoutTimer );
    };
    sw::FixedStepTimer timer( 0.05f, 0.25f );
    SW_REQUIRE_TRUE( readForged( 0.03f, timer ) );
    SW_EXPECT_NEAR( 0.03f, timer._accumulator, 1.0e-6f );
    SW_EXPECT_TRUE( readForged( 0.2f, timer ) ); // 걸음을 줄인 핫 리로드는 그대로 읽힌다
    SW_EXPECT_TRUE( readForged( 0.03f, timer ) );

    SW_EXPECT_FALSE( readForged( -0.1f, timer ) );
    SW_EXPECT_FALSE( readForged( 0.26f, timer ) );
    SW_EXPECT_FALSE( readForged( 1.0e30f, timer ) );
    SW_EXPECT_NEAR( 0.03f, timer._accumulator, 1.0e-6f );

    sw::FixedStepTimer longStep( 2.0f, 0.25f ); // 걸음이 한 프레임 상한보다 길면 걸음까지 찬다
    SW_EXPECT_TRUE( readForged( 1.5f, longStep ) );
    SW_EXPECT_FALSE( readForged( 2.5f, longStep ) );
}

/**
 * @brief [StateIOTest] 결함: 비율 누적이 음수만 보아, 터무니없는 값 하나로 `takeOne` 고리가 끝나지 않았다. 0..1 밖을 거절하고 값을 둔다
 */
SW_TEST_CASE( StateIOTest, RateAccumulatorRefusesWholeAmounts )
{
    const auto readForged = []( float32 fraction, sw::RateAccumulator& inoutAccumulator )
    {
        sw::Archive written;
        written << fraction;
        sw::Archive reading( written.getData(), written.getSize() );
        return sw::StateIO::readRateAccumulator( reading, inoutAccumulator );
    };
    sw::RateAccumulator accumulator;
    SW_REQUIRE_TRUE( readForged( 0.5f, accumulator ) );
    SW_EXPECT_NEAR( 0.5f, accumulator.getFraction(), 1.0e-6f );

    SW_EXPECT_FALSE( readForged( -0.1f, accumulator ) );
    SW_EXPECT_FALSE( readForged( 1.0f, accumulator ) );
    SW_EXPECT_FALSE( readForged( 1.0e30f, accumulator ) );
    SW_EXPECT_NEAR( 0.5f, accumulator.getFraction(), 1.0e-6f );
}

namespace
{
    constexpr uint32 kTag = 0x54534554u; // "TEST"

    sw::Archive makeSection( uint32 version, int32 value )
    {
        sw::Archive body;
        body << value;
        sw::Archive outer;
        sw::StateIO::writeSection( outer, kTag, version, body );
        return outer;
    }
} // namespace

/**
 * @brief [StateIOTest] readExpectedSection 은 표와 버전이 모두 같을 때만 본문을 주고, 버전 · 표가 다르면 거절한다(옛 판을 새 판으로 읽지 않는다)
 */
SW_TEST_CASE( StateIOTest, ExpectedSectionRejectsOtherVersionOrTag )
{
    const sw::Archive written = makeSection( 2, 42 );

    sw::Archive same( written.getData(), written.getSize() );
    sw::Archive body;
    SW_REQUIRE_TRUE( sw::StateIO::readExpectedSection( same, kTag, 2, body ) );
    int32 value = 0;
    body >> value;
    SW_EXPECT_EQUAL( 42, value );
    SW_EXPECT_EQUAL( size_t{ 0 }, same.getRemainingBytes() );

    sw::Archive oldVersion( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateIO::readExpectedSection( oldVersion, kTag, 1, body ) );
    sw::Archive otherTag( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateIO::readExpectedSection( otherTag, kTag + 1, 2, body ) );

    sw::Archive any( written.getData(), written.getSize() );
    uint32      tag     = 0;
    uint32      version = 0;
    SW_REQUIRE_TRUE( sw::StateIO::readSection( any, tag, version, body ) );
    SW_EXPECT_EQUAL( kTag, tag );
    SW_EXPECT_EQUAL( 2u, version );
}

/**
 * @brief [StateIOTest] 결함: 빈 이름의 c_str() 가 "None" 이라 빈 이름이 "None" 이라는 이름으로 돌아왔다. 빈 이름과 이름이 그대로 왕복하는지 검증
 */
SW_TEST_CASE( StateIOTest, EmptyNameRoundTripsEmpty )
{
    sw::Archive written;
    sw::StateIO::writeName( written, sw::hashed_string{} );
    sw::StateIO::writeName( written, sw::hashed_string( "gunner" ) );
    SW_EXPECT_EQUAL( int64{ 4 + 4 + 6 }, static_cast<int64>( written.getSize() ) ); // 빈 이름은 길이 칸만

    sw::Archive       reading( written.getData(), written.getSize() );
    sw::hashed_string first( "x" );
    sw::hashed_string second;
    SW_REQUIRE_TRUE( sw::StateIO::readName( reading, first ) );
    SW_REQUIRE_TRUE( sw::StateIO::readName( reading, second ) );
    SW_EXPECT_TRUE( first.empty() );
    SW_EXPECT_TRUE( second == sw::hashed_string( "gunner" ) );
}

namespace
{
    /** @brief 입구를 시험하는 작은 상태입니다. 손으로 쓴 바이트와 견줘 칸 순서와 폭을 고정합니다. */
    struct StateIOTestState
    {
        SW_DECLARE_STATE_IO();

        sw::hashed_string  _name{};
        sw::vector<uint32> _listID{};
        int32              _level{ 1 };
        uint8              _bActive{ SW_FALSE };
        uint8              _kind{ 0 };
    };

    enum class StateIOTestKind : uint8
    {
        First,
        Last,
    };

    void StateIOTestState::serializeState( sw::StateIO& io )
    {
        io.value( _name );
        io.list( _listID );
        io.value( _level );
        io.flag( _bActive );
        StateIOTestKind kind = static_cast<StateIOTestKind>( _kind );
        io.enumValue( kind, StateIOTestKind::Last );
        _kind = static_cast<uint8>( kind );
        io.check( 1 <= _level );
    }
} // namespace

/**
 * @brief [StateIOTest] 한 함수가 쓴 바이트가 손으로 놓은 칸 순서 · 폭과 같고, 다시 읽어 같은 상태가 된다
 */
SW_TEST_CASE( StateIOTest, WritesPropertiesInOrderAndRoundTrips )
{
    StateIOTestState state;
    state._name    = sw::hashed_string( "hero" );
    state._listID  = { 7u, 9u };
    state._level   = 3;
    state._bActive = SW_TRUE;
    state._kind    = 1;
    sw::Archive written;
    state.writeState( written );

    sw::Archive byHand;
    sw::StateIO::writeName( byHand, state._name );
    byHand << static_cast<uint32>( 2 );
    byHand << static_cast<uint32>( 7 );
    byHand << static_cast<uint32>( 9 );
    byHand << int32{ 3 };
    byHand << uint8{ SW_TRUE };
    byHand << uint8{ 1 };
    SW_REQUIRE_TRUE( written.getSize() == byHand.getSize() );
    SW_EXPECT_TRUE( std::memcmp( written.getData(), byHand.getData(), written.getSize() ) == 0 );

    sw::Archive      reading( written.getData(), written.getSize() );
    StateIOTestState restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored._name == state._name );
    SW_EXPECT_EQUAL( size_t{ 2 }, restored._listID.size() );
    SW_EXPECT_EQUAL( 3, restored._level );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( restored._kind ) );
}

/**
 * @brief [StateIOTest] 어느 칸에서 거절되든 읽기는 실패하고 상태는 그대로다(입구가 사본에 읽는다)
 */
SW_TEST_CASE( StateIOTest, RejectedReadLeavesStateUntouched )
{
    const auto readForged = []( int32 level, uint8 bActive, uint8 kind, StateIOTestState& inoutState )
    {
        sw::Archive forged;
        sw::StateIO::writeName( forged, sw::hashed_string( "forged" ) );
        forged << static_cast<uint32>( 0 );
        forged << level;
        forged << bActive;
        forged << kind;
        sw::Archive reading( forged.getData(), forged.getSize() );
        return inoutState.readState( reading );
    };
    StateIOTestState state;
    state._name  = sw::hashed_string( "kept" );
    state._level = 5;
    SW_EXPECT_FALSE( readForged( 0, SW_TRUE, 1, state ) ); // 범위 검사
    SW_EXPECT_FALSE( readForged( 2, 2, 1, state ) );       // 깃발
    SW_EXPECT_FALSE( readForged( 2, SW_TRUE, 2, state ) ); // 열거
    SW_EXPECT_TRUE( state._name == sw::hashed_string( "kept" ) );
    SW_EXPECT_EQUAL( 5, state._level );
    SW_EXPECT_TRUE( readForged( 2, SW_TRUE, 1, state ) );
    SW_EXPECT_EQUAL( 2, state._level );
}

/**
 * @brief [StateIOTest] 구간은 표 · 판이 같을 때만 본문을 읽고, 본문이 남으면 거절한다
 */
SW_TEST_CASE( StateIOTest, SectionChecksTagVersionAndConsumption )
{
    constexpr uint32 kTag = 0x54534554u;
    sw::Archive      outer;
    {
        sw::StateIO io( outer );
        int32       value = 42;
        io.section( kTag, 1, [&value]( sw::StateIO& bodyIO )
        { bodyIO.value( value ); } );
    }
    {
        sw::Archive reading( outer.getData(), outer.getSize() );
        sw::StateIO io( reading );
        int32       value = 0;
        io.section( kTag, 1, [&value]( sw::StateIO& bodyIO )
        { bodyIO.value( value ); } );
        SW_EXPECT_TRUE( io.isOk() );
        SW_EXPECT_EQUAL( 42, value );
    }
    {
        sw::Archive reading( outer.getData(), outer.getSize() );
        sw::StateIO io( reading );
        io.section( kTag, 2, []( sw::StateIO& ) {} );
        SW_EXPECT_FALSE( io.isOk() ); // 다른 판
    }
    {
        sw::Archive reading( outer.getData(), outer.getSize() );
        sw::StateIO io( reading );
        io.section( kTag, 1, []( sw::StateIO& ) {} );
        SW_EXPECT_FALSE( io.isOk() ); // 읽지 않은 본문이 남았다
    }
}
