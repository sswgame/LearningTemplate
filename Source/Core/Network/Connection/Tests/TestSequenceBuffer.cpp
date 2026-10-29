#include "pch.h"

#include "Core/Network/Connection/SequenceBuffer.h"
#include "Core/Testing/TestFramework.h"

// SequenceBuffer — 칸을 다시 쓰는 방법은 원소가 `kSlotReset` 으로 밝힌다. 이름으로 찾아 고르지 않으므로
// `resetForReuse` 이름을 틀리거나 `kSlotReset` 을 빠뜨린 원소는 `SequenceBuffer` 가 컴파일 오류로 멈춘다(아래 static_assert 가 그 판정이다).

using namespace sw;

namespace
{
    struct AssignEntry
    {
        static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::Assign;

        int32 _value{ 0 };
    };

    struct ReuseEntry
    {
        static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::ReuseBuffer;

        void resetForReuse()
        {
            vector<uint8> keptBuffer = std::move( _buffer );
            keptBuffer.clear();
            *this   = ReuseEntry{};
            _buffer = std::move( keptBuffer );
        }

        vector<uint8> _buffer{};
        int32         _value{ 0 };
    };

    /** @brief `ReuseBuffer` 를 밝혔으나 함수 이름을 틀렸다 — 옛 감지 방식이면 조용히 `T{}` 대입으로 돌아갔다. */
    struct MisnamedReuseEntry
    {
        static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::ReuseBuffer;

        [[maybe_unused]] void resetForReused(); // 정의하지 않는다 — 감지(decltype)만 본다
    };

    /** @brief 방법을 밝히지 않았다. */
    struct UndeclaredEntry
    {
        [[maybe_unused]] void resetForReuse(); // 정의하지 않는다 — 감지(decltype)만 본다
    };

    /** @brief 방법을 다른 형식으로 밝혔다. */
    struct WrongTypeEntry
    {
        static constexpr bool kSlotReset = true;
    };
} // namespace

static_assert( IsSequenceBufferElement<int32>::value, "a scalar can only be assigned" );
static_assert( IsSequenceBufferElement<AssignEntry>::value );
static_assert( IsSequenceBufferElement<ReuseEntry>::value );
static_assert( IsSequenceBufferElement<MisnamedReuseEntry>::value == false, "misnamed resetForReuse must stop the build" );
static_assert( IsSequenceBufferElement<UndeclaredEntry>::value == false, "an element without kSlotReset must stop the build" );
static_assert( IsSequenceBufferElement<WrongTypeEntry>::value == false, "kSlotReset must be a SequenceSlotReset" );

SW_TEST_CASE( SequenceBufferTest, ReuseBufferKeepsCapacityAndClearsFields )
{
    SequenceBuffer<ReuseEntry, 4> buffer;
    ReuseEntry*                   pFirst = buffer.insert( 0 );
    SW_REQUIRE_TRUE( pFirst != nullptr );
    pFirst->_buffer.assign( 64, 7 );
    pFirst->_value        = 5;
    const size_t capacity = pFirst->_buffer.capacity();

    // 크기 4 의 고리라 시퀀스 4 는 0 과 같은 칸을 다시 쓴다.
    ReuseEntry* pReused = buffer.insert( 4 );
    SW_REQUIRE_TRUE( pReused == pFirst );
    SW_EXPECT_TRUE( pReused->_buffer.empty() );
    SW_EXPECT_EQUAL( capacity, pReused->_buffer.capacity() );
    SW_EXPECT_EQUAL( 0, pReused->_value );
}

SW_TEST_CASE( SequenceBufferTest, AssignResetsEntry )
{
    SequenceBuffer<AssignEntry, 4> buffer;
    buffer.insert( 1 )->_value = 9;
    SW_EXPECT_EQUAL( 0, buffer.insert( 5 )->_value );
    SW_EXPECT_FALSE( buffer.exists( 1 ) );
}
