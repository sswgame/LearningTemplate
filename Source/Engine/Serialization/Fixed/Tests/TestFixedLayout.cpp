// FixedLayoutTest — 고정 배치형 해석기(서술자 표 `FixedFieldDesc`): 예전 생성 본문(타입마다 직선 코드)과 같은 바이트를 내고, 같은 경계 · 개수 · 열거자 · 검증 판정으로 거절한다(nogpu).
// 손 인코딩은 예전 생성기가 `FixedLayoutSample` 에 냈을 호출 차례를 그대로 적은 것이다 — 해석기가 이것과 한 비트라도 다르면 와이어 형식이 바뀐 것이다.
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Fixed/Tests/FixedLayoutTestTypes.h"

using namespace sw;

namespace
{
    /** @brief 손 인코딩에 넣는 결함입니다. 읽기 쪽 거절을 하나씩 본다. */
    enum class FixedSampleFault : uint8
    {
        None,
        UsedAboveLimit,  ///< `_used` 가 앞선 필드 `_limit` 보다 크다
        ModeOutOfSet,    ///< 열거자 범위 비트에 범위 밖 번호
        UnknownFlagBit,  ///< 등록되지 않은 플래그 비트
        NameTooLong,     ///< 글 길이가 `MaxCount` 를 넘는다
        ListOverMax,     ///< vector 개수가 `MaxCount` 를 넘는다
        OffsetOutOfRange ///< 펼친 중첩 타입의 칸이 바깥 필드 범위를 넘는다
    };

    struct FixedSampleUtil
    {
        static FixedLayoutSample makeSample()
        {
            FixedLayoutSample sample;
            sample._id            = 4321;
            sample._bEnabled      = true;
            sample._bFirst        = 1;
            sample._bSecond       = 0;
            sample._percent       = 73;
            sample._score         = 1234567890123;
            sample._limit         = 9;
            sample._used          = 7;
            sample._raw           = -3.25f;
            sample._bounded       = 0.5f;
            sample._quantized     = 3.7f;
            sample._time          = 12.5;
            sample._position      = float3( 1.0f, -2.5f, 9.75f );
            sample._mode          = FixedLayoutSampleMode::Crouch;
            sample._flags         = static_cast<FixedLayoutSampleFlags>( SW_BIT( 0 ) | SW_BIT( 2 ) );
            sample._name          = "pilot";
            sample._listValue     = { -5, 0, 300 };
            sample._arrByte       = { 1, 2, 255 };
            sample._point._offset = -9;
            sample._point._weight = 0.125f;
            sample._listStat.resize( 2 );
            sample._listStat[0]._health = 0;
            sample._listStat[1]._health = 1000;
            return sample;
        }

        /** @brief 예전 생성 본문의 호출 차례 그대로 씁니다. @p fault 자리는 범위 밖 값을 같은 비트 폭으로 씁니다. */
        static void writeHandEncoded( const FixedLayoutSample& sample, BitWriter& writer, const FixedSampleFault fault )
        {
            constexpr int64   kNoLower      = FixedCodecUtil::kNoLowerBound;
            constexpr int64   kNoUpper      = FixedCodecUtil::kNoUpperBound;
            constexpr float64 kNoUpperFloat = FixedCodecUtil::kNoUpperFloatBound;
            FixedCodecUtil::writeRawBits( writer, FixedLayout::getSchemaHash<FixedLayoutSample>(), 64 );
            // 기반 클래스
            SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, sample._id, kNoLower, kNoUpper ) );
            writer.writeBool( sample._bEnabled );
            writer.writeBool( sample._bFirst != 0 );
            writer.writeBool( sample._bSecond != 0 );
            SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, sample._percent, 0, 100 ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, sample._score, 0, kNoUpper ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, sample._limit, 1, 64 ) );
            if ( fault == FixedSampleFault::UsedAboveLimit )
                SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, static_cast<uint32>( sample._limit + 1 ), kNoLower, kNoUpper ) );
            else
                SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, sample._used, kNoLower, static_cast<int64>( sample._limit ) ) );
            writer.writeFloat( sample._raw );
            SW_EXPECT_TRUE( FixedCodecUtil::writeBoundedFloat32( writer, sample._bounded, -1.0, 1.0 ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeQuantizedFloat32( writer, sample._quantized, 0.0f, 10.0f, 0.5f ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeBoundedFloat64( writer, sample._time, 0.0, kNoUpperFloat ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeBoundedFloat32( writer, sample._position._x, -10.0, 10.0 ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeBoundedFloat32( writer, sample._position._y, -10.0, 10.0 ) );
            SW_EXPECT_TRUE( FixedCodecUtil::writeBoundedFloat32( writer, sample._position._z, -10.0, 10.0 ) );
            if ( fault == FixedSampleFault::ModeOutOfSet )
                SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, int64{ 3 }, 0, 3 ) ); // [0, 2] 와 같은 2 비트
            else
                SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, static_cast<int64>( sample._mode ), 0, 2 ) );
            const uint8 flags = fault == FixedSampleFault::UnknownFlagBit ? uint8{ 0x02 } : static_cast<uint8>( sample._flags );
            FixedCodecUtil::writeRawBits( writer, flags, 8 );
            if ( fault == FixedSampleFault::NameTooLong )
                SW_EXPECT_TRUE( FixedCodecUtil::writeString( writer, "seventeen-letters", 31 ) ); // [0, 16] 과 같은 5 비트
            else
                SW_EXPECT_TRUE( FixedCodecUtil::writeString( writer, sample._name, 16 ) );
            const size_t listCount = fault == FixedSampleFault::ListOverMax ? 5 : sample._listValue.size();
            SW_EXPECT_TRUE( FixedCodecUtil::writeCount( writer, listCount, 7 ) ); // [0, 4] 와 같은 3 비트
            for ( size_t index = 0; index < listCount; ++index )
            {
                const int16 value = index < sample._listValue.size() ? sample._listValue[index] : int16{ 0 };
                SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, value, kNoLower, kNoUpper ) );
            }
            for ( const uint8 byte : sample._arrByte )
            {
                SW_EXPECT_TRUE( FixedCodecUtil::writeVarInteger( writer, byte, kNoLower, kNoUpper ) );
            }
            // 펼친 중첩 타입 — 범위는 바깥 `_limit`
            const int64 limit = sample._limit;
            if ( fault == FixedSampleFault::OffsetOutOfRange )
                SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, static_cast<int64>( 2 * limit + 1 ), -limit, 2 * limit + 1 ) ); // [-limit, limit] 와 같은 5 비트에 범위 밖 칸 번호
            else
                SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, sample._point._offset, -limit, limit ) );
            writer.writeFloat( sample._point._weight );
            // 다른 코덱(검증 함수가 있는 타입)
            SW_EXPECT_TRUE( FixedCodecUtil::writeCount( writer, sample._listStat.size(), 2 ) );
            for ( const FixedLayoutSampleStat& stat : sample._listStat )
            {
                SW_EXPECT_TRUE( FixedCodecUtil::writeRangedInteger( writer, stat._health, 0, 1000 ) );
            }
        }

        static FixedReadResult readHandEncoded( const FixedLayoutSample& source, const FixedSampleFault fault, FixedLayoutSample& inoutTarget )
        {
            BitWriter writer;
            writeHandEncoded( source, writer, fault );
            const vector<uint8> bytes = writer.getBytes();
            return FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ), inoutTarget );
        }
    };
} // namespace

/** @brief [FixedLayoutTest] 해석기가 쓴 바이트가 예전 생성 본문의 호출 차례로 손 인코딩한 바이트와 한 비트도 다르지 않다 */
SW_TEST_CASE( FixedLayoutTest, DescriptorCodecMatchesHandEncodedBytes )
{
    const FixedLayoutSample sample = FixedSampleUtil::makeSample();

    BitWriter written;
    SW_REQUIRE_TRUE( FixedLayout::write( sample, written ) );
    BitWriter expected;
    FixedSampleUtil::writeHandEncoded( sample, expected, FixedSampleFault::None );
    SW_EXPECT_TRUE( written.getBytes() == expected.getBytes() );
}

/** @brief [FixedLayoutTest] 쓴 바이트를 읽으면 같은 값이고(Step 칸은 칸 값), 비교가 같다고 본다. 한 칸만 달라도 다르다 */
SW_TEST_CASE( FixedLayoutTest, DescriptorCodecRoundTripsAndCompares )
{
    const FixedLayoutSample sample = FixedSampleUtil::makeSample();
    BitWriter               writer;
    SW_REQUIRE_TRUE( FixedLayout::write( sample, writer ) );
    const vector<uint8> bytes = writer.getBytes();

    FixedLayoutSample read;
    SW_REQUIRE_TRUE( FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ), read ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( 3.5f, read._quantized ); // 0.5 간격 칸으로 내려앉는다(3.7 → 7 번 칸)
    SW_EXPECT_TRUE( read._listValue == sample._listValue );
    SW_EXPECT_EQUAL( -9, read._point._offset );

    FixedLayoutSample expected = sample;
    expected._quantized        = 3.5f;
    SW_EXPECT_TRUE( FixedLayout::equals( expected, read ) );

    read._bSecond = 1;
    SW_EXPECT_FALSE( FixedLayout::equals( expected, read ) ); // 비트필드 한 칸
    read._bSecond             = 0;
    read._listStat[1]._health = 999;
    SW_EXPECT_FALSE( FixedLayout::equals( expected, read ) ); // 다른 코덱의 칸
    read._listStat[1]._health = 1000;
    read._id                  = 1;
    SW_EXPECT_FALSE( FixedLayout::equals( expected, read ) ); // 기반 클래스의 칸
}

/** @brief [FixedLayoutTest] 네트워크 · 세이브에서 온 범위 밖 바이트는 하나씩 거절되고, 거절된 읽기는 대상을 바꾸지 않는다 */
SW_TEST_CASE( FixedLayoutTest, DescriptorCodecRefusesOutOfBoundBytes )
{
    const FixedLayoutSample sample     = FixedSampleUtil::makeSample();
    const FixedSampleFault  arrFault[] = { FixedSampleFault::UsedAboveLimit, FixedSampleFault::ModeOutOfSet, FixedSampleFault::UnknownFlagBit,
                                           FixedSampleFault::NameTooLong, FixedSampleFault::ListOverMax, FixedSampleFault::OffsetOutOfRange };
    for ( const FixedSampleFault fault : arrFault )
    {
        FixedLayoutSample target;
        SW_EXPECT_TRUE( FixedSampleUtil::readHandEncoded( sample, fault, target ) == FixedReadResult::OutOfRange );
        SW_EXPECT_EQUAL( uint16{ 0 }, target._id ); // 기반 칸도 그대로다(원자적 읽기)
        SW_EXPECT_TRUE( target._name.empty() );
    }

    // 검증 함수는 다른 코덱 안에서도 읽기 끝에 불린다
    FixedLayoutSample invalidSample    = sample;
    invalidSample._listStat[0]._health = 13;
    FixedLayoutSample target;
    SW_EXPECT_TRUE( FixedSampleUtil::readHandEncoded( invalidSample, FixedSampleFault::None, target ) == FixedReadResult::ValidateFailed );

    // 잘린 스트림
    BitWriter writer;
    SW_REQUIRE_TRUE( FixedLayout::write( sample, writer ) );
    const vector<uint8> bytes = writer.getBytes();
    SW_EXPECT_TRUE( FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ) - 2, target ) == FixedReadResult::Truncated );
}

/** @brief [FixedLayoutTest] 쓰기도 경계 밖 값을 잘라 쓰지 않고 실패한다 — 앞선 필드 범위 · 개수 · 열거자 · 플래그 · 글 길이 */
SW_TEST_CASE( FixedLayoutTest, DescriptorCodecRefusesOutOfBoundValuesOnWrite )
{
    const FixedLayoutSample sample = FixedSampleUtil::makeSample();
    for ( int32 caseIndex = 0; caseIndex < 6; ++caseIndex )
    {
        FixedLayoutSample bad = sample;
        switch ( caseIndex )
        {
            case 0:
                bad._used = static_cast<uint32>( bad._limit + 1 );
                break;
            case 1:
                bad._listValue.resize( 5 );
                break;
            case 2:
                bad._mode = static_cast<FixedLayoutSampleMode>( 7 );
                break;
            case 3:
                bad._flags = static_cast<FixedLayoutSampleFlags>( SW_BIT( 1 ) );
                break;
            case 4:
                bad._name = "seventeen-letters";
                break;
            default:
                bad._point._offset = bad._limit + 1;
                break;
        }
        BitWriter writer;
        SW_EXPECT_FALSE( FixedLayout::write( bad, writer ) );
    }
}
