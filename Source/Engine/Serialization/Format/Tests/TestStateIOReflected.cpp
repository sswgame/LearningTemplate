#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateIOReflectedTestTypes.h"

#include <cstring>

// StateIOReflectedTest — 리플렉션 상태 칸(`PROPERTY( RuntimeState )`)을 선언에서 싣는다: 모든 상태 칸이 왕복하고 상태가 아닌 칸은 실리지 않으며,
// 판은 타입(`REFLECT( Version )`)에 있고 옛 판은 그 타입의 패치 사슬이 올린다. 칸을 바꾸고 판을 안 올린 바이트는 틀리게 읽지 않고 거절한다(nogpu).

namespace sw
{
    namespace
    {
        struct StateIOReflectedTestPatches
        {
            /** @brief 판 1 → 2: 체력을 int16 에서 int32 로 넓히고 새 칸 속도를 데이터 규칙(1.5)으로 채운다. */
            static bool convertHealthAndAddSpeed( Archive& inOldBody, Archive& outNewBody )
            {
                int16 health = 0;
                inOldBody >> health;
                outNewBody << static_cast<int32>( health ) << 1.5f;
                return inOldBody.isOk();
            }
        };

        /** @brief 판 @p version · 배치 해시 @p layoutHash 머리 뒤에 @p body 를 붙인 바이트를 @p inoutState 로 읽습니다. */
        template <typename TState, typename TBody>
        bool readCrafted( uint32 version, uint64 layoutHash, TBody&& body, TState& inoutState )
        {
            Archive crafted;
            crafted << version << layoutHash;
            body( crafted );
            Archive reading( crafted.getData(), crafted.getSize() );
            return inoutState.readState( reading );
        }

        template <typename T>
        uint64 computeLayoutHash()
        {
            const TypeInfo* pType = TypeLookupCache{}.find( typeFqn<T>() );
            SW_ASSERT( pType != nullptr );
            return StateIO::computeReflectedLayoutHash( *pType );
        }
    } // namespace

    SW_TYPE_VERSION_PATCH( stateIOReflectedTest1To2, "sw::StateIOReflectedTestVersioned", 1, 2, &StateIOReflectedTestPatches::convertHealthAndAddSpeed );
} // namespace sw

/**
 * @brief [StateIOReflectedTest] 상태 칸은 형식마다 모두 왕복하고(구조체 · 구조체 시퀀스 · 비트필드 포함), 상태가 아닌 칸은 실리지 않아 읽는 쪽의 값 그대로다
 */
SW_TEST_CASE( StateIOReflectedTest, EveryStateFieldRoundTripsAndOthersStay )
{
    sw::StateIOReflectedTestState state;
    state._steps        = 7;
    state._target       = sw::float3{ 4.0f, 5.0f, 6.0f };
    state._cell         = sw::int2{ -3, 12 };
    state._note         = "running";
    state._label        = sw::hashed_string( "Gate" );
    state._phase        = sw::StateIOReflectedTestPhase::Done;
    state._part._count  = 3;
    state._part._weight = 2.5f;
    state._listPart.resize( 2 );
    state._listPart[1]._count = 9;
    state._listVisited        = { 1, 4, 9 };
    state._tuning             = 0.9f;
    state._bOpen              = SW_TRUE;

    sw::Archive written;
    state.writeState( written );
    sw::Archive                   reading( written.getData(), written.getSize() );
    sw::StateIOReflectedTestState restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, reading.getRemainingBytes() );
    SW_EXPECT_EQUAL( 7, restored._steps );
    SW_EXPECT_NEAR( 5.0f, restored._target._y, 1.0e-6f );
    SW_EXPECT_EQUAL( -3, restored._cell._x );
    SW_EXPECT_EQUAL( 12, restored._cell._y );
    SW_EXPECT_TRUE( restored._note == "running" );
    SW_EXPECT_TRUE( restored._label == sw::hashed_string( "Gate" ) );
    SW_EXPECT_TRUE( restored._phase == sw::StateIOReflectedTestPhase::Done );
    SW_EXPECT_EQUAL( 3, restored._part._count );
    SW_EXPECT_NEAR( 2.5f, restored._part._weight, 1.0e-6f );
    SW_REQUIRE_EQUAL( size_t{ 2 }, restored._listPart.size() );
    SW_EXPECT_EQUAL( 9, restored._listPart[1]._count );
    SW_REQUIRE_EQUAL( size_t{ 3 }, restored._listVisited.size() );
    SW_EXPECT_EQUAL( 9, restored._listVisited[2] );
    SW_EXPECT_EQUAL( uint32{ SW_TRUE }, uint32{ restored._bOpen } );
    SW_EXPECT_NEAR( 0.5f, restored._tuning, 1.0e-6f ); // 상태가 아닌 칸은 실리지 않는다
}

/**
 * @brief [StateIOReflectedTest] 판이 같은데 칸 배치 해시가 다른 바이트(칸을 바꾸고 판을 안 올림)는 틀리게 읽지 않고 거절하며 상태는 그대로다
 */
SW_TEST_CASE( StateIOReflectedTest, SameVersionWithAnotherLayoutIsRefused )
{
    sw::StateIOReflectedTestVersioned state;
    state._health    = 11;
    const uint64 now = sw::computeLayoutHash<sw::StateIOReflectedTestVersioned>();
    SW_EXPECT_TRUE( now != sw::computeLayoutHash<sw::StateIOReflectedTestMixed>() );
    SW_EXPECT_EQUAL( now, sw::computeLayoutHash<sw::StateIOReflectedTestVersioned>() ); // 같은 선언이면 같은 해시다

    const auto body = []( sw::Archive& out )
    { out << int32{ 3 } << 2.0f; };
    SW_EXPECT_LOG( "StateIO.ReflectedLayoutChanged" );
    SW_EXPECT_FALSE( sw::readCrafted( 2, now + 1, body, state ) );
    SW_EXPECT_EQUAL( 11, state._health );
    SW_EXPECT_TRUE( sw::readCrafted( 2, now, body, state ) );
    SW_EXPECT_EQUAL( 3, state._health );
}

/**
 * @brief [StateIOReflectedTest] 옛 판(1) 본문은 그 타입의 패치 사슬(`SW_TYPE_VERSION_PATCH`)로 올라 읽히고, 새 판(3)은 거절한다 — 판은 그릇이 아니라 타입에 있다
 */
SW_TEST_CASE( StateIOReflectedTest, OldVersionIsPatchedThroughTheTypeChain )
{
    sw::StateIOReflectedTestVersioned state;
    SW_REQUIRE_TRUE( sw::readCrafted( 1, 0, []( sw::Archive& out )
    { out << int16{ 7 }; }, state ) );
    SW_EXPECT_EQUAL( 7, state._health );
    SW_EXPECT_NEAR( 1.5f, state._speed, 1.0e-6f );

    // 올려 읽은 상태는 지금 판으로 다시 쓴다
    sw::Archive rewritten;
    state.writeState( rewritten );
    sw::Archive                       again( rewritten.getData(), rewritten.getSize() );
    sw::StateIOReflectedTestVersioned reread;
    SW_REQUIRE_TRUE( reread.readState( again ) );
    SW_EXPECT_EQUAL( 7, reread._health );

    SW_EXPECT_LOG( "FormatUpgrade.NewerThanSupported" );
    SW_EXPECT_FALSE( sw::readCrafted( 3, 0, []( sw::Archive& out )
    { out << int32{ 1 } << 1.0f; }, state ) );
    SW_EXPECT_EQUAL( 7, state._health );
}

/**
 * @brief [StateIOReflectedTest] 열거자 밖 값과 `PROPERTY( Min )` 밖 값은 거절하고 상태는 그대로다(사본에 읽는다)
 */
SW_TEST_CASE( StateIOReflectedTest, OutOfRangeValuesAreRefused )
{
    sw::StateIOReflectedTestState source;
    source._steps = 4;
    sw::Archive written;
    source.writeState( written );

    // 머리(12) 뒤 첫 칸 `_steps` 를 -1 로 바꾼다
    sw::vector<uint8> bytes( written.getData(), written.getData() + written.getSize() );
    const int32       negative = -1;
    std::memcpy( bytes.data() + sizeof( uint32 ) + sizeof( uint64 ), &negative, sizeof( negative ) );
    sw::Archive                   reading( bytes.data(), bytes.size() );
    sw::StateIOReflectedTestState restored;
    restored._steps = 2;
    SW_EXPECT_FALSE( restored.readState( reading ) ); // 범위 밖은 손 `io.check` 처럼 로그 없이 읽기만 실패한다
    SW_EXPECT_EQUAL( 2, restored._steps );
}

/**
 * @brief [StateIOReflectedTest] 손 본문은 리플렉션 칸 뒤에 리플렉션 밖 값과 칸 사이 검사를 더한다 — 둘 다 왕복하고 검사를 어긴 바이트는 거절한다
 */
SW_TEST_CASE( StateIOReflectedTest, HandBodyAddsValuesOutsideReflection )
{
    sw::StateIOReflectedTestMixed state;
    state._limit    = 20;
    state._handOnly = 15;
    sw::Archive written;
    state.writeState( written );
    sw::Archive                   reading( written.getData(), written.getSize() );
    sw::StateIOReflectedTestMixed restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 20, restored._limit );
    SW_EXPECT_EQUAL( 15, restored._handOnly );

    const uint64 hash = sw::computeLayoutHash<sw::StateIOReflectedTestMixed>();
    SW_EXPECT_FALSE( sw::readCrafted( 1, hash, []( sw::Archive& out )
    { out << int32{ 5 } << int32{ 6 }; }, restored ) ); // 손 값이 한도를 넘는다
    SW_EXPECT_EQUAL( 15, restored._handOnly );
}

/**
 * @brief [StateIOReflectedTest] 내장 표의 State 역할 · Layout 으로 실리는 칸(실수 · 큰 정수 · 쿼터니언 · int2 시퀀스 · 오브젝트 핸들 · 이름 시퀀스)이 FATAL 없이 왕복한다
 */
SW_TEST_CASE( StateIOReflectedTest, BuiltinLayoutPropertiesRoundTrip )
{
    sw::StateIOReflectedTestBuiltins state;
    state._clock    = 12.25;
    state._ticket   = 0x1122334455667788ull;
    state._facing   = sw::quaternion{ 0.0f, 0.6f, 0.0f, 0.8f };
    state._listCell = {
        sw::int2{-1,   2},
        sw::int2{30, -40}
    };
    state._target  = sw::GameObjectHandle::make( 77 );
    state._listTag = { sw::hashed_string( "Gate" ), sw::hashed_string( "Lever" ) };

    sw::Archive written;
    state.writeState( written );
    sw::Archive                      reading( written.getData(), written.getSize() );
    sw::StateIOReflectedTestBuiltins restored;
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, reading.getRemainingBytes() );
    SW_EXPECT_EQUAL( 12.25, restored._clock );
    SW_EXPECT_EQUAL( state._ticket, restored._ticket );
    SW_EXPECT_EQUAL( 0.6f, restored._facing._y );
    SW_EXPECT_EQUAL( 0.8f, restored._facing._w );
    SW_REQUIRE_EQUAL( size_t{ 2 }, restored._listCell.size() );
    SW_EXPECT_EQUAL( 30, restored._listCell[1]._x );
    SW_EXPECT_EQUAL( -40, restored._listCell[1]._y );
    SW_EXPECT_TRUE( restored._target == sw::GameObjectHandle::make( 77 ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, restored._listTag.size() );
    SW_EXPECT_TRUE( restored._listTag[1] == sw::hashed_string( "Lever" ) );
}

/**
 * @brief [StateIOReflectedTest] int2 칸은 성분마다(int32 둘) 실려 손 함수 `StateIO::writeInt2` 와 같은 바이트다 — 이름 표에서 Layout 으로 옮겨도 옛 바이트를 그대로 읽는다
 */
SW_TEST_CASE( StateIOReflectedTest, Int2BytesMatchTheHandWriter )
{
    sw::StateIOReflectedTestCell state;
    state._cell = sw::int2{ -7, 123456 };
    sw::Archive written;
    state.writeState( written );

    sw::Archive expected;
    sw::StateIO::writeInt2( expected, state._cell );
    // 머리(판 · 배치 해시) 뒤의 본문이 손 함수의 바이트와 같다.
    SW_REQUIRE_TRUE( written.getSize() == sizeof( uint32 ) + sizeof( uint64 ) + expected.getSize() );
    const uint8* pBody = static_cast<const uint8*>( written.getData() ) + written.getSize() - expected.getSize();
    SW_EXPECT_TRUE( std::memcmp( expected.getData(), pBody, expected.getSize() ) == 0 );
}
