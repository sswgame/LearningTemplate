// Archive — 바이너리 스트림: 원시 · 수학 타입 왕복, 파일 저장 · 읽기, CRC32 체크섬과 변조 감지, 범위 밖 읽기 · 오류 표시, 섹션 · 서브 아카이브, VarInt · ZigZag 경계(nogpu).
// TLV · 문자열 풀 · 압축 · JSON/XML 임베딩 · 세이브 · 씬 문서 케이스는 손으로 만든 TypeInfo · GameFramework 타입 · 옛 임시 리소스 id 에 기대 아직 옮기지 않았다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VarIntUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

/**
 * @brief [ArchiveTest] Archive 바이너리 기본 타입 직렬화/역직렬화
 */
SW_TEST_CASE( ArchiveTest, MemoryArchiveBinarySerialization )
{
    sw::Archive arch;

    int32   intVal   = 42;
    float32 floatVal = 3.14159f;

    arch << intVal << floatVal;
    arch.setReadModeAndResetPosition( true );

    int32   readInt{ 0 };
    float32 readFloat{ 0.0f };

    arch >> readInt >> readFloat;

    SW_EXPECT_EQUAL( 42, readInt );
    SW_EXPECT_NEAR( 3.14159f, readFloat, 1e-4 );
}

/**
 * @brief [ArchiveTest] 모든 원시 자료형(bool, u8~u64, i8~i64, f32, f64, string) 직렬화 라운드트립
 */
SW_TEST_CASE( ArchiveTest, ArchiveAllPrimitiveTypesStreaming )
{
    sw::Archive writeArch;

    const bool       inBool     = true;
    const uint8      inU8       = 0xEFu;
    const uint16     inU16      = 0xBEEFu;
    const uint32     inU32      = 0xDEADBEEFu;
    const uint64     inU64      = 0x0123456789ABCDEFull;
    const int8       inI8       = -120;
    const int16      inI16      = -32000;
    const int32      inI32      = -12345678;
    const int64      inI64      = -987654321012345ll;
    const float32    inF32      = -123.456f;
    const float64    inF64      = 9876543.2109876;
    const sw::string inStrEmpty = "";
    const sw::string inStrText  = "SW_Engine_Archive_Stream_Test_12345";

    writeArch << inBool << inU8 << inU16 << inU32 << inU64;
    writeArch << inI8 << inI16 << inI32 << inI64;
    writeArch << inF32 << inF64;
    writeArch << sw::string_view( inStrEmpty ) << sw::string_view( inStrText );

    sw::vector<uint8> rawBytes;
    writeArch.writeData( rawBytes );
    SW_EXPECT_FALSE( rawBytes.empty() );

    sw::Archive readArch( rawBytes.data(), rawBytes.size() );
    SW_EXPECT_TRUE( readArch.isReadMode() );
    SW_EXPECT_EQUAL( static_cast<uint64>( rawBytes.size() ), readArch.getSize() );
    SW_EXPECT_EQUAL( 0ull, readArch.getOffset() );

    bool       outBool{ false };
    uint8      outU8{ 0 };
    uint16     outU16{ 0 };
    uint32     outU32{ 0 };
    uint64     outU64{ 0 };
    int8       outI8{ 0 };
    int16      outI16{ 0 };
    int32      outI32{ 0 };
    int64      outI64{ 0 };
    float32    outF32{ 0.0f };
    float64    outF64{ 0.0 };
    sw::string outStrEmpty{ "dummy" };
    sw::string outStrText;

    readArch >> outBool >> outU8 >> outU16 >> outU32 >> outU64;
    readArch >> outI8 >> outI16 >> outI32 >> outI64;
    readArch >> outF32 >> outF64;
    readArch >> outStrEmpty >> outStrText;

    SW_EXPECT_EQUAL( inBool, outBool );
    SW_EXPECT_EQUAL( inU8, outU8 );
    SW_EXPECT_EQUAL( inU16, outU16 );
    SW_EXPECT_EQUAL( inU32, outU32 );
    SW_EXPECT_EQUAL( inU64, outU64 );
    SW_EXPECT_EQUAL( inI8, outI8 );
    SW_EXPECT_EQUAL( inI16, outI16 );
    SW_EXPECT_EQUAL( inI32, outI32 );
    SW_EXPECT_EQUAL( inI64, outI64 );
    SW_EXPECT_NEAR( inF32, outF32, 1e-4 );
    SW_EXPECT_NEAR( inF64, outF64, 1e-6 );
    SW_EXPECT_TRUE( outStrEmpty.empty() );
    SW_EXPECT_EQUAL( inStrText, outStrText );
    SW_EXPECT_EQUAL( readArch.getSize(), readArch.getOffset() );
}

/**
 * @brief [ArchiveTest] 수학 벡터/행렬(float2, float3, float4, float4x4) 직렬화 검증
 */
SW_TEST_CASE( ArchiveTest, ArchiveMathTypesStreaming )
{
    sw::Archive writeArch;

    const sw::float2   inV2{ 1.5f, -2.5f };
    const sw::float3   inV3{ 10.0f, 20.0f, 30.0f };
    const sw::float4   inV4{ 0.1f, 0.2f, 0.3f, 0.4f };
    const sw::float4x4 inM4{
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f };

    writeArch << inV2 << inV3 << inV4 << inM4;

    sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

    sw::float2   outV2{};
    sw::float3   outV3{};
    sw::float4   outV4{};
    sw::float4x4 outM4{};

    readArch >> outV2 >> outV3 >> outV4 >> outM4;

    SW_EXPECT_NEAR( inV2._x, outV2._x, 1e-5 );
    SW_EXPECT_NEAR( inV2._y, outV2._y, 1e-5 );

    SW_EXPECT_NEAR( inV3._x, outV3._x, 1e-5 );
    SW_EXPECT_NEAR( inV3._y, outV3._y, 1e-5 );
    SW_EXPECT_NEAR( inV3._z, outV3._z, 1e-5 );

    SW_EXPECT_NEAR( inV4._x, outV4._x, 1e-5 );
    SW_EXPECT_NEAR( inV4._y, outV4._y, 1e-5 );
    SW_EXPECT_NEAR( inV4._z, outV4._z, 1e-5 );
    SW_EXPECT_NEAR( inV4._w, outV4._w, 1e-5 );

    SW_EXPECT_NEAR( inM4._11, outM4._11, 1e-5 );
    SW_EXPECT_NEAR( inM4._12, outM4._12, 1e-5 );
    SW_EXPECT_NEAR( inM4._13, outM4._13, 1e-5 );
    SW_EXPECT_NEAR( inM4._14, outM4._14, 1e-5 );
    SW_EXPECT_NEAR( inM4._21, outM4._21, 1e-5 );
    SW_EXPECT_NEAR( inM4._22, outM4._22, 1e-5 );
    SW_EXPECT_NEAR( inM4._23, outM4._23, 1e-5 );
    SW_EXPECT_NEAR( inM4._24, outM4._24, 1e-5 );
    SW_EXPECT_NEAR( inM4._31, outM4._31, 1e-5 );
    SW_EXPECT_NEAR( inM4._32, outM4._32, 1e-5 );
    SW_EXPECT_NEAR( inM4._33, outM4._33, 1e-5 );
    SW_EXPECT_NEAR( inM4._34, outM4._34, 1e-5 );
    SW_EXPECT_NEAR( inM4._41, outM4._41, 1e-5 );
    SW_EXPECT_NEAR( inM4._42, outM4._42, 1e-5 );
    SW_EXPECT_NEAR( inM4._43, outM4._43, 1e-5 );
    SW_EXPECT_NEAR( inM4._44, outM4._44, 1e-5 );
}

/**
 * @brief [ArchiveTest] 디스크 파일 저장/로드(saveFile 및 Archive(file, true)) 라운드트립
 */
SW_TEST_CASE( ArchiveTest, ArchiveFileIORoundTrip )
{
    const sw::string tempFilePath = sw::test::makeScratchPath( "test_archive_io.bin" );

    sw::Archive writeArch;
    writeArch << 0x12345678u;
    writeArch << sw::string( "TempArchiveFilePayload" );
    writeArch << -999.5f;

    SW_REQUIRE_TRUE( writeArch.saveFile( tempFilePath ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( tempFilePath ) );

    sw::Archive readArch( tempFilePath, true );
    SW_EXPECT_TRUE( readArch.isReadMode() );
    SW_EXPECT_TRUE( readArch.getSize() > 0 );

    uint32     outMagic{ 0 };
    sw::string outText;
    float32    outVal{ 0.0f };

    readArch >> outMagic >> outText >> outVal;

    SW_EXPECT_EQUAL( 0x12345678u, outMagic );
    SW_EXPECT_EQUAL( sw::string( "TempArchiveFilePayload" ), outText );
    SW_EXPECT_NEAR( -999.5f, outVal, 1e-4 );
}

/**
 * @brief [ArchiveTest] CRC32 체크섬 계산 및 데이터 변조(Corruption) 감지 검증
 */
SW_TEST_CASE( ArchiveTest, ArchiveChecksumAndCorruptionDetection )
{
    sw::Archive writeArch;
    writeArch << sw::string( "SafeSecurePayloadData" );
    writeArch << 424242;

    const uint32 crc = writeArch.computeChecksum();
    SW_EXPECT_TRUE( crc != 0 );

    writeArch.writeChecksum();
    SW_EXPECT_TRUE( writeArch.validateChecksum() );

    // 1) 정상 역직렬화
    sw::vector<uint8> buffer;
    writeArch.writeData( buffer );

    sw::Archive normalArch( buffer.data(), buffer.size() );
    SW_EXPECT_TRUE( normalArch.validateChecksum() );

    // 2) 바이트 변조 시뮬레이션
    buffer[4] ^= 0xFF; // 페이로드 내부 1바이트 반전
    const uint32 tamperedCrc = sw::StringUtil::computeCRC32( buffer.data(), buffer.size() - sizeof( uint32 ) );
    SW_EXPECT_TRUE( crc != tamperedCrc );
}

/**
 * @brief [ArchiveTest] 읽기 범위 초과(Out of Bounds) 및 에러 경계 검증
 */
SW_TEST_CASE( ArchiveTest, ArchiveBoundaryAndErrorHandling )
{
    sw::Archive emptyArch;
    emptyArch.setReadModeAndResetPosition( true );

    int32      val{ 0 };
    const bool bReadOk = emptyArch.readBytes( &val, sizeof( int32 ) );
    SW_EXPECT_FALSE( bReadOk );

    sw::Archive smallArch;
    smallArch << static_cast<uint8>( 7 );
    smallArch.setReadModeAndResetPosition( true );

    uint32     largeVal{ 0 };
    const bool bLargeReadOk = smallArch.readBytes( &largeVal, sizeof( uint32 ) );
    SW_EXPECT_FALSE( bLargeReadOk );
}

/**
 * @brief [ArchiveTest] Archive 에러 추적 및 안전성 헬퍼 (isOk, isError, hasBytesAvailable, getRemainingBytes)
 */
SW_TEST_CASE( ArchiveTest, ArchiveErrorTrackingAndSafetyHelpers )
{
    sw::Archive writeArch;
    SW_EXPECT_TRUE( writeArch.isOk() );
    SW_EXPECT_FALSE( writeArch.isError() );
    SW_EXPECT_TRUE( static_cast<bool>( writeArch ) );

    writeArch << 12345 << sw::string( "Hello" );
    SW_EXPECT_TRUE( writeArch.isOk() );

    sw::Archive readArch( writeArch.getData(), writeArch.getSize() );
    SW_EXPECT_TRUE( readArch.isOk() );
    SW_EXPECT_EQUAL( writeArch.getSize(), readArch.getRemainingBytes() );
    SW_EXPECT_TRUE( readArch.hasBytesAvailable( sizeof( int32 ) ) );

    int32 val{ 0 };
    readArch >> val;
    SW_EXPECT_EQUAL( 12345, val );
    SW_EXPECT_TRUE( readArch.isOk() );

    // 남은 크기를 초과하는 데이터 읽기 시도 시 에러 플래그 설정 검증
    uint64 tooBig = 0;
    SW_EXPECT_FALSE( readArch.readBytes( &tooBig, 999999 ) );
    SW_EXPECT_TRUE( readArch.isError() );
    SW_EXPECT_FALSE( readArch.isOk() );
    SW_EXPECT_FALSE( static_cast<bool>( readArch ) );

    readArch.clearError();
    SW_EXPECT_TRUE( readArch.isOk() );
}

/**
 * @brief [ArchiveTest] Archive 섹션 및 서브 아카이브 분할 슬라이싱 검증
 */
SW_TEST_CASE( ArchiveTest, ArchiveSubArchiveAndSectionSlice )
{
    sw::Archive writeArch;

    // 1) 섹션 기록 (크기 접두사 + 페이로드)
    const sw::string sectionA = "HeaderSection_01";
    const sw::string sectionB = "PayloadBodySection_02_With_More_Data";

    writeArch.writeSection( sectionA.data(), static_cast<uint32>( sectionA.size() ) );
    writeArch.writeSection( nullptr, 0 ); // 빈 섹션
    writeArch.writeSection( sectionB.data(), static_cast<uint32>( sectionB.size() ) );

    sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

    sw::vector<uint8> outSecA;
    SW_EXPECT_TRUE( readArch.readSection( outSecA ) );
    SW_EXPECT_EQUAL( sectionA.size(), outSecA.size() );
    SW_EXPECT_EQUAL( sectionA, sw::string( reinterpret_cast<const utf8*>( outSecA.data() ), outSecA.size() ) );

    sw::vector<uint8> outSecEmpty;
    SW_EXPECT_TRUE( readArch.readSection( outSecEmpty ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( outSecEmpty.size() ) );

    sw::vector<uint8> outSecB;
    SW_EXPECT_TRUE( readArch.readSection( outSecB ) );
    SW_EXPECT_EQUAL( sectionB.size(), outSecB.size() );
    SW_EXPECT_EQUAL( sectionB, sw::string( reinterpret_cast<const utf8*>( outSecB.data() ), outSecB.size() ) );

    // 2) 서브 아카이브 분할 검증
    sw::Archive packetArch;
    packetArch << 100 << 200 << 300;

    sw::Archive streamArch( packetArch.getData(), packetArch.getSize() );
    sw::Archive sub = streamArch.readSubArchive( sizeof( int32 ) * 2 );
    SW_EXPECT_TRUE( sub.isOk() );
    SW_EXPECT_EQUAL( sizeof( int32 ) * 2, sub.getSize() );

    int32 a{ 0 };
    int32 b{ 0 };
    sub >> a >> b;
    SW_EXPECT_EQUAL( 100, a );
    SW_EXPECT_EQUAL( 200, b );

    int32 c{ 0 };
    streamArch >> c;
    SW_EXPECT_EQUAL( 300, c );
}

/**
 * @brief [ArchiveTest] 가변 길이 정수(VarInt / ZigZag) 경계값 및 압축율 전수 검증
 */
SW_TEST_CASE( ArchiveTest, VarIntAndZigZagBoundaryEncoding )
{
    sw::Archive writeArch;

    // 1) VarUInt 경계값
    const uint64 u0       = 0;
    const uint64 u1       = 1;
    const uint64 u127     = 127;
    const uint64 u128     = 128;
    const uint64 u16383   = 16383;
    const uint64 u16384   = 16384;
    const uint64 u32Max   = 0xFFFFFFFFull;
    const uint64 u64Large = 0x123456789ABCDEFull;
    const uint64 u64Max   = 0xFFFFFFFFFFFFFFFFull;

    writeArch.writeVarUint( u0 );
    writeArch.writeVarUint( u1 );
    writeArch.writeVarUint( u127 );
    writeArch.writeVarUint( u128 );
    writeArch.writeVarUint( u16383 );
    writeArch.writeVarUint( u16384 );
    writeArch.writeVarUint( u32Max );
    writeArch.writeVarUint( u64Large );
    writeArch.writeVarUint( u64Max );

    // 2) VarInt (ZigZag) 경계값 (음수 포함)
    const int64 i0           = 0;
    const int64 iNeg1        = -1;
    const int64 iNeg2        = -2;
    const int64 i63          = 63;
    const int64 iNeg64       = -64;
    const int64 iNeg128      = -128;
    const int64 iNeg32768    = -32768;
    const int64 iNeg2Billion = -2147483648ll;
    const int64 iNegLarge    = -9223372036854775807ll;

    writeArch.writeVarInt( i0 );
    writeArch.writeVarInt( iNeg1 );
    writeArch.writeVarInt( iNeg2 );
    writeArch.writeVarInt( i63 );
    writeArch.writeVarInt( iNeg64 );
    writeArch.writeVarInt( iNeg128 );
    writeArch.writeVarInt( iNeg32768 );
    writeArch.writeVarInt( iNeg2Billion );
    writeArch.writeVarInt( iNegLarge );

    // 3) 개별 바이트 수 검증
    sw::vector<uint8> testBuffer;
    testBuffer.clear();
    SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarUint64( 0, testBuffer ) );
    testBuffer.clear();
    SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarUint64( 127, testBuffer ) );
    testBuffer.clear();
    SW_EXPECT_EQUAL( 2ULL, sw::VarIntUtil::encodeVarUint64( 128, testBuffer ) );
    testBuffer.clear();
    SW_EXPECT_EQUAL( 2ULL, sw::VarIntUtil::encodeVarUint64( 16383, testBuffer ) );
    testBuffer.clear();
    SW_EXPECT_EQUAL( 3ULL, sw::VarIntUtil::encodeVarUint64( 16384, testBuffer ) );
    testBuffer.clear();
    SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarInt64( -1, testBuffer ) ); // ZigZag(-1) = 1 (1 byte!)

    // 4) 스트림 역직렬화 전수 일치 검증
    sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

    uint64 outU0 = 99, outU1 = 99, outU127 = 99, outU128 = 99, outU16383 = 99, outU16384 = 99, outU32Max = 99, outU64Large = 99, outU64Max = 99;
    SW_EXPECT_TRUE( readArch.readVarUint( outU0 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU1 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU127 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU128 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU16383 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU16384 ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU32Max ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU64Large ) );
    SW_EXPECT_TRUE( readArch.readVarUint( outU64Max ) );

    SW_EXPECT_EQUAL( u0, outU0 );
    SW_EXPECT_EQUAL( u1, outU1 );
    SW_EXPECT_EQUAL( u127, outU127 );
    SW_EXPECT_EQUAL( u128, outU128 );
    SW_EXPECT_EQUAL( u16383, outU16383 );
    SW_EXPECT_EQUAL( u16384, outU16384 );
    SW_EXPECT_EQUAL( u32Max, outU32Max );
    SW_EXPECT_EQUAL( u64Large, outU64Large );
    SW_EXPECT_EQUAL( u64Max, outU64Max );

    int64 outI0 = 99, outINeg1 = 99, outINeg2 = 99, outI63 = 99, outINeg64 = 99, outINeg128 = 99, outINeg32768 = 99, outINeg2Billion = 99, outINegLarge = 99;
    SW_EXPECT_TRUE( readArch.readVarInt( outI0 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg1 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg2 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outI63 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg64 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg128 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg32768 ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINeg2Billion ) );
    SW_EXPECT_TRUE( readArch.readVarInt( outINegLarge ) );

    SW_EXPECT_EQUAL( i0, outI0 );
    SW_EXPECT_EQUAL( iNeg1, outINeg1 );
    SW_EXPECT_EQUAL( iNeg2, outINeg2 );
    SW_EXPECT_EQUAL( i63, outI63 );
    SW_EXPECT_EQUAL( iNeg64, outINeg64 );
    SW_EXPECT_EQUAL( iNeg128, outINeg128 );
    SW_EXPECT_EQUAL( iNeg32768, outINeg32768 );
    SW_EXPECT_EQUAL( iNeg2Billion, outINeg2Billion );
    SW_EXPECT_EQUAL( iNegLarge, outINegLarge );
}
