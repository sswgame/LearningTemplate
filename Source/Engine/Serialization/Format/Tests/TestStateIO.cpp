#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <cstring>

// StateIOTest — 상태 구간: 기대한 표의 구간만 읽히고(옛 판은 판 올림 사슬로 올리고, 단계가 없거나 새 판이면 거절), 표로 갈라 읽는 쪽은 아무 구간이나 받는다.
// 머리(`header`)의 판 올림: 옛 판 바이트 → 단계를 차례로 → 새 판 값, 바깥 형식의 남은 칸은 그대로(감사 A-6).
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

    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    sw::Archive readAsOld( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( sw::StateIO::readExpectedSection( readAsOld, kTag, 1, body ) ); // 새 판을 옛 판으로 읽지 않는다
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
        SW_EXPECT_LOG( "FormatUpgrade.StepMissing" );
        sw::Archive reading( outer.getData(), outer.getSize() );
        sw::StateIO io( reading );
        io.section( kTag, 2, []( sw::StateIO& ) {} );
        SW_EXPECT_FALSE( io.isOk() ); // 다른 판 — 올릴 단계가 없다
    }
    {
        sw::Archive reading( outer.getData(), outer.getSize() );
        sw::StateIO io( reading );
        io.section( kTag, 1, []( sw::StateIO& ) {} );
        SW_EXPECT_FALSE( io.isOk() ); // 읽지 않은 본문이 남았다
    }
}

namespace sw
{
    namespace
    {
        /**
         * @brief 판 올림을 시험하는 상태입니다. 판 1: 체력 int16, 판 2: 체력 int32, 판 3: 체력 int32 + 속도 float32.
         * @details 판 0 에서 올리는 단계는 일부러 없다(빠진 단계는 거절).
         */
        struct StateUpgradeTestState
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "TUPG" );
            static constexpr uint32 kStateVersion = 3;

            SW_DECLARE_STATE_IO();

            int32   _health{ 0 };
            float32 _speed{ 0.0f };
        };

        void StateUpgradeTestState::serializeState( StateIO& io )
        {
            io.header( kStateTag, kStateVersion );
            io.value( _health );
            io.value( _speed );
        }

        /** @brief 머리 없는 바깥 형식입니다 — 안쪽 상태가 올라가도 뒤 칸(`_tail`)이 그대로 읽혀야 한다. */
        struct StateUpgradeTestOuterState
        {
            SW_DECLARE_STATE_IO();

            StateUpgradeTestState _child{};
            int32                 _tail{ 0 };
        };

        void StateUpgradeTestOuterState::serializeState( StateIO& io )
        {
            io.value( _child );
            io.value( _tail );
        }

        struct StateUpgradeTestSteps
        {
            /** @brief 판 1 → 2: 체력을 int16 에서 int32 로 넓힌다. */
            static bool convertHealthToInt32( Archive& inOldBody, Archive& outNewBody )
            {
                int16 health = 0;
                inOldBody >> health;
                outNewBody << static_cast<int32>( health );
                return inOldBody.isOk();
            }
            /** @brief 판 2 → 3: 새 칸 속도를 데이터 규칙(1.5)으로 채운다. */
            static bool addSpeed( Archive& inOldBody, Archive& outNewBody )
            {
                int32 health = 0;
                inOldBody >> health;
                outNewBody << health << 1.5f;
                return inOldBody.isOk();
            }
        };
    } // namespace

    SW_STATE_FORMAT_UPGRADE( stateUpgradeTest1, "TUPG", 1, 2, &StateUpgradeTestSteps::convertHealthToInt32 );
    SW_STATE_FORMAT_UPGRADE( stateUpgradeTest2, "TUPG", 2, 3, &StateUpgradeTestSteps::addSpeed );
} // namespace sw

/**
 * @brief [StateIOTest] 옛 판(1) 바이트를 머리가 단계 1 → 2 → 3 으로 올려 읽는다 — 값이 맞고, 머리 없는 바깥 형식의 뒤 칸도 그대로 읽히며 아카이브는 끝까지 소비된다(감사 A-6)
 */
SW_TEST_CASE( StateIOTest, HeaderUpgradesOldVersionThroughTheChain )
{
    sw::Archive written;
    sw::StateIO::writeHeader( written, sw::StateUpgradeTestState::kStateTag, 1 );
    written << int16{ 7 };
    written << int32{ 99 }; // 바깥 형식의 뒤 칸

    sw::Archive                    reading( written.getData(), written.getSize() );
    sw::StateUpgradeTestOuterState restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 7, restored._child._health );
    SW_EXPECT_NEAR( 1.5f, restored._child._speed, 1.0e-6f );
    SW_EXPECT_EQUAL( 99, restored._tail );
    SW_EXPECT_EQUAL( size_t{ 0 }, reading.getRemainingBytes() );
    SW_EXPECT_TRUE( reading.isOk() );

    // 올려 읽은 상태는 새 판으로 쓴다
    sw::Archive rewritten;
    restored.writeState( rewritten );
    sw::Archive                    again( rewritten.getData(), rewritten.getSize() );
    sw::StateUpgradeTestOuterState reread;
    SW_REQUIRE_TRUE( reread.readState( again ) );
    SW_EXPECT_EQUAL( 7, reread._child._health );
    SW_EXPECT_EQUAL( 99, reread._tail );
}

/**
 * @brief [StateIOTest] 단계가 없는 판(0)과 새 판(4)은 거절하고 상태는 그대로다 — 옛 판을 그대로 읽는 폴백은 없다
 */
SW_TEST_CASE( StateIOTest, HeaderRefusesVersionWithoutChain )
{
    const auto readVersion = []( uint32 version, sw::StateUpgradeTestState& inoutState )
    {
        sw::Archive forged;
        sw::StateIO::writeHeader( forged, sw::StateUpgradeTestState::kStateTag, version );
        forged << int32{ 3 } << 2.0f;
        sw::Archive reading( forged.getData(), forged.getSize() );
        return inoutState.readState( reading );
    };
    sw::StateUpgradeTestState state;
    state._health = 11;
    SW_EXPECT_LOG( "FormatUpgrade.StepMissing" );
    SW_EXPECT_FALSE( readVersion( 0, state ) );
    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_FALSE( readVersion( 4, state ) );
    SW_EXPECT_EQUAL( 11, state._health );
    SW_EXPECT_TRUE( readVersion( 3, state ) );
    SW_EXPECT_EQUAL( 3, state._health );
}

/**
 * @brief [StateIOTest] 자리가 정해진 옛 판 구간(`section` · `readExpectedSection`)도 같은 사슬로 올려 본문을 읽는다
 */
SW_TEST_CASE( StateIOTest, SectionUpgradesOldVersionThroughTheChain )
{
    sw::Archive body;
    body << int16{ 21 };
    sw::Archive outer;
    sw::StateIO::writeSection( outer, sw::StateUpgradeTestState::kStateTag, 1, body );

    sw::Archive reading( outer.getData(), outer.getSize() );
    sw::StateIO io( reading );
    int32       health = 0;
    float32     speed  = 0.0f;
    io.section( sw::StateUpgradeTestState::kStateTag, sw::StateUpgradeTestState::kStateVersion, [&health, &speed]( sw::StateIO& bodyIO )
    {
        bodyIO.value( health );
        bodyIO.value( speed );
    } );
    SW_EXPECT_TRUE( io.isOk() );
    SW_EXPECT_EQUAL( 21, health );
    SW_EXPECT_NEAR( 1.5f, speed, 1.0e-6f );
    SW_EXPECT_EQUAL( size_t{ 0 }, reading.getRemainingBytes() );
}

/**
 * @brief [StateIOTest] 머리표가 다른 바이트(다른 형식, 또는 머리표가 생기기 전의 판)는 판을 알 수 없다는 경고와 함께 거절하고 상태는 그대로다
 */
SW_TEST_CASE( StateIOTest, HeaderTagMismatchSaysTheVersionIsUnknown )
{
    SW_EXPECT_LOG( "StateIO.HeaderTagMismatch" );
    sw::Archive forged;
    forged << int16{ 7 } << int16{ 0 } << int32{ 3 } << 1.5f; // 머리표 없는 칸 — 표 자리에 체력이 놓였다
    sw::Archive               reading( forged.getData(), forged.getSize() );
    sw::StateUpgradeTestState state;
    state._health = 11;
    SW_EXPECT_FALSE( state.readState( reading ) );
    SW_EXPECT_EQUAL( 11, state._health );
}
