// LayoutSerializerTest — 스키마 결합형 해석기(서술자 표 `LayoutFieldDesc`): 예전 생성 본문(타입마다 직선 코드)과 같은 바이트를 내고, 같은 경계 · 개수 · 열거자 · 검증 판정으로 거절한다(nogpu).
// 손 인코딩은 예전 생성기가 `LayoutSample` 에 냈을 호출 차례를 그대로 적은 것이다 — 해석기가 이것과 한 비트라도 다르면 와이어 형식이 바뀐 것이다.
#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Layout/LayoutSerializer.h"
#include "Engine/Serialization/Layout/Tests/LayoutSerializerTestTypes.h"

using namespace sw;

namespace
{
    /** @brief 손 인코딩에 넣는 결함입니다. 읽기 쪽 거절을 하나씩 본다. */
    enum class LayoutSampleFault : uint8
    {
        None,
        UsedAboveLimit,  ///< `_used` 가 앞선 필드 `_limit` 보다 크다
        ModeOutOfSet,    ///< 열거자 범위 비트에 범위 밖 번호
        UnknownFlagBit,  ///< 등록되지 않은 플래그 비트
        NameTooLong,     ///< 글 길이가 `MaxCount` 를 넘는다
        ListOverMax,     ///< vector 개수가 `MaxCount` 를 넘는다
        OffsetOutOfRange ///< 펼친 중첩 타입의 칸이 바깥 필드 범위를 넘는다
    };

    struct LayoutSampleUtil
    {
        static LayoutSample makeSample()
        {
            LayoutSample sample;
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
            sample._mode          = LayoutSampleMode::Crouch;
            sample._flags         = static_cast<LayoutSampleFlags>( BitUtil::bit( 0 ) | BitUtil::bit( 2 ) );
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
        static void writeHandEncoded( const LayoutSample& sample, BitWriter& writer, const LayoutSampleFault fault )
        {
            constexpr int64   kNoLower      = LayoutFieldCodec::kNoLowerBound;
            constexpr int64   kNoUpper      = LayoutFieldCodec::kNoUpperBound;
            constexpr float64 kNoUpperFloat = LayoutFieldCodec::kNoUpperFloatBound;
            LayoutFieldCodec::writeRawBits( writer, LayoutSerializer::getSchemaHash<LayoutSample>(), 64 );
            // 기반 클래스
            SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, sample._id, kNoLower, kNoUpper ) );
            writer.writeBool( sample._bEnabled );
            writer.writeBool( sample._bFirst != 0 );
            writer.writeBool( sample._bSecond != 0 );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, sample._percent, 0, 100 ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, sample._score, 0, kNoUpper ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, sample._limit, 1, 64 ) );
            if ( fault == LayoutSampleFault::UsedAboveLimit )
                SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, static_cast<uint32>( sample._limit + 1 ), kNoLower, kNoUpper ) );
            else
                SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, sample._used, kNoLower, static_cast<int64>( sample._limit ) ) );
            writer.writeFloat( sample._raw );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeBoundedFloat32( writer, sample._bounded, -1.0, 1.0 ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeQuantizedFloat32( writer, sample._quantized, 0.0f, 10.0f, 0.5f ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeBoundedFloat64( writer, sample._time, 0.0, kNoUpperFloat ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeBoundedFloat32( writer, sample._position._x, -10.0, 10.0 ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeBoundedFloat32( writer, sample._position._y, -10.0, 10.0 ) );
            SW_EXPECT_TRUE( LayoutFieldCodec::writeBoundedFloat32( writer, sample._position._z, -10.0, 10.0 ) );
            if ( fault == LayoutSampleFault::ModeOutOfSet )
                SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, int64{ 3 }, 0, 3 ) ); // [0, 2] 와 같은 2 비트
            else
                SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, static_cast<int64>( sample._mode ), 0, 2 ) );
            const uint8 flags = fault == LayoutSampleFault::UnknownFlagBit ? uint8{ 0x02 } : static_cast<uint8>( sample._flags );
            LayoutFieldCodec::writeRawBits( writer, flags, 8 );
            if ( fault == LayoutSampleFault::NameTooLong )
                SW_EXPECT_TRUE( LayoutFieldCodec::writeString( writer, "seventeen-letters", 31 ) ); // [0, 16] 과 같은 5 비트
            else
                SW_EXPECT_TRUE( LayoutFieldCodec::writeString( writer, sample._name, 16 ) );
            const size_t listCount = fault == LayoutSampleFault::ListOverMax ? 5 : sample._listValue.size();
            SW_EXPECT_TRUE( LayoutFieldCodec::writeCount( writer, listCount, 7 ) ); // [0, 4] 와 같은 3 비트
            for ( size_t index = 0; index < listCount; ++index )
            {
                const int16 value = index < sample._listValue.size() ? sample._listValue[index] : int16{ 0 };
                SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, value, kNoLower, kNoUpper ) );
            }
            for ( const uint8 byte : sample._arrByte )
            {
                SW_EXPECT_TRUE( LayoutFieldCodec::writeVarInteger( writer, byte, kNoLower, kNoUpper ) );
            }
            // 펼친 중첩 타입 — 범위는 바깥 `_limit`
            const int64 limit = sample._limit;
            if ( fault == LayoutSampleFault::OffsetOutOfRange )
                SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, static_cast<int64>( 2 * limit + 1 ), -limit, 2 * limit + 1 ) ); // [-limit, limit] 와 같은 5 비트에 범위 밖 칸 번호
            else
                SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, sample._point._offset, -limit, limit ) );
            writer.writeFloat( sample._point._weight );
            // 다른 스키마(검증 함수가 있는 타입)
            SW_EXPECT_TRUE( LayoutFieldCodec::writeCount( writer, sample._listStat.size(), 2 ) );
            for ( const LayoutSampleStat& stat : sample._listStat )
            {
                SW_EXPECT_TRUE( LayoutFieldCodec::writeRangedInteger( writer, stat._health, 0, 1000 ) );
            }
        }

        static LayoutReadResult readHandEncoded( const LayoutSample& source, const LayoutSampleFault fault, LayoutSample& inoutTarget )
        {
            BitWriter writer;
            writeHandEncoded( source, writer, fault );
            const vector<uint8> bytes = writer.getBytes();
            return LayoutSerializer::read( bytes.data(), static_cast<int32>( bytes.size() ), inoutTarget );
        }
    };
} // namespace

/** @brief [LayoutSerializerTest] 해석기가 쓴 바이트가 예전 생성 본문의 호출 차례로 손 인코딩한 바이트와 한 비트도 다르지 않다 */
SW_TEST_CASE( LayoutSerializerTest, DescriptorCodecMatchesHandEncodedBytes )
{
    const LayoutSample sample = LayoutSampleUtil::makeSample();

    BitWriter written;
    SW_REQUIRE_TRUE( LayoutSerializer::write( sample, written ) );
    BitWriter expected;
    LayoutSampleUtil::writeHandEncoded( sample, expected, LayoutSampleFault::None );
    SW_EXPECT_TRUE( written.getBytes() == expected.getBytes() );
}

/** @brief [LayoutSerializerTest] 쓴 바이트를 읽으면 같은 값이고(Step 칸은 칸 값), 비교가 같다고 본다. 한 칸만 달라도 다르다 */
SW_TEST_CASE( LayoutSerializerTest, DescriptorCodecRoundTripsAndCompares )
{
    const LayoutSample sample = LayoutSampleUtil::makeSample();
    BitWriter          writer;
    SW_REQUIRE_TRUE( LayoutSerializer::write( sample, writer ) );
    const vector<uint8> bytes = writer.getBytes();

    LayoutSample read;
    SW_REQUIRE_TRUE( LayoutSerializer::read( bytes.data(), static_cast<int32>( bytes.size() ), read ) == LayoutReadResult::Ok );
    SW_EXPECT_EQUAL( 3.5f, read._quantized ); // 0.5 간격 칸으로 내려앉는다(3.7 → 7 번 칸)
    SW_EXPECT_TRUE( read._listValue == sample._listValue );
    SW_EXPECT_EQUAL( -9, read._point._offset );

    LayoutSample expected = sample;
    expected._quantized   = 3.5f;
    SW_EXPECT_TRUE( LayoutSerializer::equals( expected, read ) );

    read._bSecond = 1;
    SW_EXPECT_FALSE( LayoutSerializer::equals( expected, read ) ); // 비트필드 한 칸
    read._bSecond             = 0;
    read._listStat[1]._health = 999;
    SW_EXPECT_FALSE( LayoutSerializer::equals( expected, read ) ); // 다른 스키마의 칸
    read._listStat[1]._health = 1000;
    read._id                  = 1;
    SW_EXPECT_FALSE( LayoutSerializer::equals( expected, read ) ); // 기반 클래스의 칸
}

/** @brief [LayoutSerializerTest] 네트워크 · 세이브에서 온 범위 밖 바이트는 하나씩 거절되고, 거절된 읽기는 대상을 바꾸지 않는다 */
SW_TEST_CASE( LayoutSerializerTest, DescriptorCodecRefusesOutOfBoundBytes )
{
    const LayoutSample      sample     = LayoutSampleUtil::makeSample();
    const LayoutSampleFault arrFault[] = { LayoutSampleFault::UsedAboveLimit, LayoutSampleFault::ModeOutOfSet, LayoutSampleFault::UnknownFlagBit,
                                           LayoutSampleFault::NameTooLong, LayoutSampleFault::ListOverMax, LayoutSampleFault::OffsetOutOfRange };
    for ( const LayoutSampleFault fault : arrFault )
    {
        LayoutSample target;
        SW_EXPECT_TRUE( LayoutSampleUtil::readHandEncoded( sample, fault, target ) == LayoutReadResult::OutOfRange );
        SW_EXPECT_EQUAL( uint16{ 0 }, target._id ); // 기반 칸도 그대로다(원자적 읽기)
        SW_EXPECT_TRUE( target._name.empty() );
    }

    // 검증 함수는 다른 스키마 안에서도 읽기 끝에 불린다
    LayoutSample invalidSample         = sample;
    invalidSample._listStat[0]._health = 13;
    LayoutSample target;
    SW_EXPECT_TRUE( LayoutSampleUtil::readHandEncoded( invalidSample, LayoutSampleFault::None, target ) == LayoutReadResult::ValidateFailed );

    // 잘린 스트림
    BitWriter writer;
    SW_REQUIRE_TRUE( LayoutSerializer::write( sample, writer ) );
    const vector<uint8> bytes = writer.getBytes();
    SW_EXPECT_TRUE( LayoutSerializer::read( bytes.data(), static_cast<int32>( bytes.size() ) - 2, target ) == LayoutReadResult::Truncated );
}

/** @brief [LayoutSerializerTest] 쓰기도 경계 밖 값을 잘라 쓰지 않고 실패한다 — 앞선 필드 범위 · 개수 · 열거자 · 플래그 · 글 길이 */
SW_TEST_CASE( LayoutSerializerTest, DescriptorCodecRefusesOutOfBoundValuesOnWrite )
{
    const LayoutSample sample = LayoutSampleUtil::makeSample();
    for ( int32 caseIndex = 0; caseIndex < 6; ++caseIndex )
    {
        LayoutSample bad = sample;
        switch ( caseIndex )
        {
            case 0:
                bad._used = static_cast<uint32>( bad._limit + 1 );
                break;
            case 1:
                bad._listValue.resize( 5 );
                break;
            case 2:
                bad._mode = static_cast<LayoutSampleMode>( 7 );
                break;
            case 3:
                bad._flags = static_cast<LayoutSampleFlags>( BitUtil::bit( 1 ) );
                break;
            case 4:
                bad._name = "seventeen-letters";
                break;
            default:
                bad._point._offset = bad._limit + 1;
                break;
        }
        BitWriter writer;
        SW_EXPECT_FALSE( LayoutSerializer::write( bad, writer ) );
    }
}

/**
 * @brief [LayoutSerializerTest] 비트 플래그 열거형은 기반 타입 전체 폭으로 쓰고 읽는다 — uint8 플래그는 한 바이트이고, 등록되지 않은 비트 조합도 그대로 돌아온다
 */
SW_TEST_CASE( LayoutSerializerTest, EnumBitsUseTheWholeUnderlyingWidth )
{
    const LayoutSampleFlags flags = static_cast<LayoutSampleFlags>( 0xA5 );
    BitWriter               writer;
    LayoutFieldCodec::writeEnumBits( writer, flags );
    const vector<uint8> bytes = writer.getBytes();
    SW_REQUIRE_EQUAL( size_t{ 1 }, bytes.size() );

    BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_EXPECT_TRUE( LayoutFieldCodec::readEnumBits<LayoutSampleFlags>( reader ) == flags );
}

/** @brief [LayoutSerializerTest] `EnumArray<E, int32>` 칸은 원소 수가 같은 `array<int32, N>` 과 같은 본문 바이트를 쓰고, 읽으면 열거자 칸마다 같은 값이다 */
SW_TEST_CASE( LayoutSerializerTest, EnumArrayMatchesStaticArrayBytes )
{
    LayoutPlainArraySample plain;
    plain._arrValue = { -7, 0, 300000 };
    LayoutEnumArraySample byMode;
    byMode._arrValue[LayoutSampleMode::Idle]   = -7;
    byMode._arrValue[LayoutSampleMode::Crouch] = 0;
    byMode._arrValue[LayoutSampleMode::Run]    = 300000;

    BitWriter plainWriter;
    BitWriter enumWriter;
    SW_REQUIRE_TRUE( LayoutSerializer::write( plain, plainWriter ) );
    SW_REQUIRE_TRUE( LayoutSerializer::write( byMode, enumWriter ) );
    // 머리 64 비트는 스키마 해시다 — 정규 글에 타입 이름 · 칸 철자가 들어가 두 타입은 늘 다르다. 견주는 것은 그 뒤의 본문이다.
    constexpr size_t    kSchemaHashBytes = 8;
    const vector<uint8> plainBytes       = plainWriter.getBytes();
    const vector<uint8> enumBytes        = enumWriter.getBytes();
    SW_REQUIRE_TRUE( plainBytes.size() == enumBytes.size() && plainBytes.size() > kSchemaHashBytes );
    SW_EXPECT_TRUE( std::equal( plainBytes.begin() + kSchemaHashBytes, plainBytes.end(), enumBytes.begin() + kSchemaHashBytes ) );

    const vector<uint8>   bytes = enumWriter.getBytes();
    LayoutEnumArraySample read;
    SW_REQUIRE_TRUE( LayoutSerializer::read( bytes.data(), static_cast<int32>( bytes.size() ), read ) == LayoutReadResult::Ok );
    SW_EXPECT_EQUAL( -7, read._arrValue[LayoutSampleMode::Idle] );
    SW_EXPECT_EQUAL( 300000, read._arrValue[LayoutSampleMode::Run] );
    SW_EXPECT_TRUE( LayoutSerializer::equals( byMode, read ) );
}

namespace sw
{
    namespace
    {
        /** @brief 테스트용 옛 스키마 해시입니다(어느 빌드의 해시도 아닌 값). */
        constexpr uint64 kLayoutSampleStatOldHash = 0x5EED5EED5EED5EEDull;

        /** @brief 옛 배치: 체력을 16 비트 그대로 실었다. */
        LayoutReadResult readLayoutSampleStatOld( BitReader& reader, void* pObject )
        {
            static_cast<LayoutSampleStat*>( pObject )->_health = static_cast<int32>( LayoutFieldCodec::readRawBits( reader, 16 ) );
            return LayoutReadResult::Ok;
        }
    } // namespace

    SW_LAYOUT_SCHEMA_MIGRATION( layoutSampleStatOld, "sw::LayoutSampleStat", kLayoutSampleStatOldHash, &readLayoutSampleStatOld );
} // namespace sw

/**
 * @brief [LayoutSerializerTest] 다른 스키마 해시의 바이트는 (타입, 해시)에 등록한 마이그레이션으로 읽고, 마이그레이션이 없는 해시는 `SchemaMismatch` 로 거절하며 대상은 그대로다(감사 11)
 */
SW_TEST_CASE( LayoutSerializerTest, OtherSchemaHashReadsThroughRegisteredConversion )
{
    const auto readWithHash = []( uint64 schemaHash, LayoutSampleStat& inoutStat )
    {
        BitWriter writer;
        LayoutFieldCodec::writeRawBits( writer, schemaHash, 64 );
        LayoutFieldCodec::writeRawBits( writer, 250, 16 );
        const vector<uint8> bytes = writer.getBytes();
        return LayoutSerializer::read( bytes.data(), static_cast<int32>( bytes.size() ), inoutStat );
    };
    LayoutSampleStat stat;
    SW_EXPECT_TRUE( readWithHash( kLayoutSampleStatOldHash, stat ) == LayoutReadResult::Ok );
    SW_EXPECT_EQUAL( 250, stat._health );

    stat._health = 40;
    SW_EXPECT_TRUE( readWithHash( 0xBADull, stat ) == LayoutReadResult::SchemaMismatch );
    SW_EXPECT_EQUAL( 40, stat._health );
}
