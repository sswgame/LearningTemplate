#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Compression — Null 코덱 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( Core_Compression, NullCodecPassthrough )
{
	sw::NullCompressionCodec codec;
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::None ), static_cast<uint32>( codec.getCodecType() ) );
	SW_EXPECT_EQUAL( sw::string( "Null" ), sw::string( codec.getCodecName() ) );

	const sw::string original = "Hello SW Engine Compression!";
	const size_t	 bound	  = codec.compressBound( original.size() );
	SW_EXPECT_TRUE( bound >= original.size() );

	sw::vector<uint8> compressed( bound );
	size_t			  compressedSize = 0;
	SW_EXPECT_TRUE( codec.compress( original.data(), original.size(), compressed.data(), compressed.size(), compressedSize ) );
	SW_EXPECT_EQUAL( original.size(), compressedSize );

	sw::vector<uint8> decompressed( original.size() );
	size_t			  decompressedSize = 0;
	SW_EXPECT_TRUE( codec.decompress( compressed.data(), compressedSize, decompressed.data(), decompressed.size(), decompressedSize ) );
	SW_EXPECT_EQUAL( original.size(), decompressedSize );

	const sw::string restored( reinterpret_cast<const utf8*>( decompressed.data() ), decompressedSize );
	SW_EXPECT_EQUAL( original, restored );
}

// ------------------------------------------------------------------------------
// 2) Core_Compression — RLE 코덱 반복 패턴 및 리터럴 압축/복원
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, RleCodecRepetitionAndLiterals )
{
	sw::RleCompressionCodec codec;
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::RLE ), static_cast<uint32>( codec.getCodecType() ) );

	// 1) 고반복 데이터 (압축률이 높은 경우)
	sw::vector<uint8> repetitiveData( 1024, 0xAA );
	sw::vector<uint8> compBuffer( codec.compressBound( repetitiveData.size() ) );
	size_t			  compSize = 0;

	SW_EXPECT_TRUE( codec.compress( repetitiveData.data(), repetitiveData.size(), compBuffer.data(), compBuffer.size(), compSize ) );
	SW_EXPECT_TRUE( compSize < repetitiveData.size() / 4 ); // 1024바이트가 수십 바이트 이하로 압축됨

	sw::vector<uint8> decompBuffer( repetitiveData.size() );
	size_t			  decompSize = 0;
	SW_EXPECT_TRUE( codec.decompress( compBuffer.data(), compSize, decompBuffer.data(), decompBuffer.size(), decompSize ) );
	SW_EXPECT_EQUAL( repetitiveData.size(), decompSize );
	SW_EXPECT_TRUE( repetitiveData == decompBuffer );

	// 2) 비반복 리터럴 데이터
	sw::vector<uint8> literalData( 256 );
	for ( size_t index = 0; index < literalData.size(); ++index )
		literalData[index] = static_cast<uint8>( index & 0xFF );

	compBuffer.resize( codec.compressBound( literalData.size() ) );
	SW_EXPECT_TRUE( codec.compress( literalData.data(), literalData.size(), compBuffer.data(), compBuffer.size(), compSize ) );

	decompBuffer.resize( literalData.size() );
	SW_EXPECT_TRUE( codec.decompress( compBuffer.data(), compSize, decompBuffer.data(), decompBuffer.size(), decompSize ) );
	SW_EXPECT_EQUAL( literalData.size(), decompSize );
	SW_EXPECT_TRUE( literalData == decompBuffer );
}

// ------------------------------------------------------------------------------
// 3) Core_Compression — 레지스트리 및 동적 코덱 조회
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, CodecRegistryAndDynamicLookup )
{
	sw::CompressionCodecRegistry& reg = sw::CompressionCodecRegistry::get();
	reg.initialize();

	sw::ICompressionCodec* pRle = reg.getCodec( sw::CompressionCodecType::RLE );
	SW_EXPECT_TRUE( pRle != nullptr );
	SW_EXPECT_EQUAL( sw::string( "RLE" ), sw::string( pRle->getCodecName() ) );

	sw::ICompressionCodec* pNull = reg.getCodec( "Null" );
	SW_EXPECT_TRUE( pNull != nullptr );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::None ), static_cast<uint32>( pNull->getCodecType() ) );

	SW_EXPECT_TRUE( reg.getDefaultCodec() != nullptr );
}

// ------------------------------------------------------------------------------
// 4) Core_Compression — CompressionStream 바이너리 패키징 및 체크섬 무결성
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, CompressionStreamRoundtrip )
{
	const sw::string original = "SW Engine High-Performance Pluggable Compression Serialization Stream Test Payload 1234567890!";

	// 1) RLE 압축 스트림 생성
	sw::vector<uint8> compressedStream;
	SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), compressedStream, sw::CompressionCodecType::RLE ) );
	SW_EXPECT_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );

	// 2) 헤더 검증
	sw::CompressionHeader header{};
	SW_EXPECT_TRUE( sw::CompressionStream::verifyHeader( compressedStream.data(), compressedStream.size(), header ) );
	SW_EXPECT_EQUAL( sw::CompressionStream::kMagicNumber, header._magic );
	SW_EXPECT_EQUAL( static_cast<uint64>( original.size() ), header._uncompressedSize );

	// 3) 역압축 및 무결성 확인
	sw::vector<uint8> decompressed;
	SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), decompressed ) );
	SW_EXPECT_EQUAL( original.size(), decompressed.size() );

	const sw::string restored( reinterpret_cast<const utf8*>( decompressed.data() ), decompressed.size() );
	SW_EXPECT_EQUAL( original, restored );

	// 4) 손상된 데이터 방어 검증 (체크섬 불일치)
	if ( compressedStream.size() > sizeof( sw::CompressionHeader ) + 2 )
	{
		SW_TEST_SUPPRESS_LOGS();
		compressedStream[sizeof( sw::CompressionHeader ) + 1] ^= 0xFF; // 페이로드 오염
		sw::vector<uint8> corruptedResult;
		SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), corruptedResult ) );
	}
}
