#include "pch.h"

#include "Core/Compression/RleCompressionCodec.h"

#include "Core/Memory/Memory.h"
namespace sw
{
	namespace
	{
		constexpr size_t kMaxRunLength	   = 130;
		constexpr size_t kMaxLiteralLength = 128;
	} // namespace

	CompressionCodecType RleCompressionCodec::getCodecType() const
	{
		return CompressionCodecType::RLE;
	}

	const utf8* RleCompressionCodec::getCodecName() const
	{
		return "RLE";
	}

	size_t RleCompressionCodec::compressBound( size_t uncompressedSize ) const
	{
		if ( uncompressedSize == 0 )
			return 16;
		return uncompressedSize + ( uncompressedSize / 128 ) + 64;
	}

	bool RleCompressionCodec::compress( const void* pSrc,
										size_t		srcSize,
										void*		pDst,
										size_t		dstCapacity,
										size_t&		outCompressedSize,
										int32 /*compressionLevel*/ )
	{
		outCompressedSize = 0;
		if ( srcSize == 0 )
			return true;

		if ( pSrc == nullptr || pDst == nullptr )
			return false;

		const auto* const pInput  = static_cast<const uint8*>( pSrc );
		auto* const		  pOutput = static_cast<uint8*>( pDst );

		size_t readPos	= 0;
		size_t writePos = 0;

		while ( readPos < srcSize )
		{
			// 1) 연속된 동일 바이트 런 검출
			size_t runLength = 1;
			while ( readPos + runLength < srcSize && runLength < kMaxRunLength && pInput[readPos + runLength] == pInput[readPos] )
			{
				++runLength;
			}

			if ( runLength >= 3 )
			{
				// 반복 런 기록: MSB = 1, (count - 3) [0..127]
				if ( writePos + 2 > dstCapacity )
					return false;

				pOutput[writePos++] = static_cast<uint8>( 0x80 | ( runLength - 3 ) );
				pOutput[writePos++] = pInput[readPos];
				readPos += runLength;
			}
			else
			{
				// 2) 리터럴 런 검출 (반복되지 않는 바이트 열)
				size_t literalLength = 0;
				while ( readPos + literalLength < srcSize && literalLength < kMaxLiteralLength )
				{
					// 남은 부분에서 3개 이상 반복되는 런이 시작되는지 확인
					if ( readPos + literalLength + 2 < srcSize &&
						 pInput[readPos + literalLength] == pInput[readPos + literalLength + 1] &&
						 pInput[readPos + literalLength] == pInput[readPos + literalLength + 2] )
					{
						break;
					}
					++literalLength;
				}

				if ( literalLength == 0 )
					literalLength = 1;

				// 리터럴 런 기록: MSB = 0, (count - 1) [0..127]
				if ( writePos + 1 + literalLength > dstCapacity )
					return false;

				pOutput[writePos++] = static_cast<uint8>( literalLength - 1 );
				Memory::copy( &pOutput[writePos], &pInput[readPos], literalLength );
				writePos += literalLength;
				readPos += literalLength;
			}
		}

		outCompressedSize = writePos;
		return true;
	}

	bool RleCompressionCodec::decompress( const void* pSrc,
										  size_t	  srcSize,
										  void*		  pDst,
										  size_t	  dstCapacity,
										  size_t&	  outUncompressedSize )
	{
		outUncompressedSize = 0;
		if ( srcSize == 0 )
			return true;

		if ( pSrc == nullptr || pDst == nullptr )
			return false;

		const auto* const pInput  = static_cast<const uint8*>( pSrc );
		auto* const		  pOutput = static_cast<uint8*>( pDst );

		size_t readPos	= 0;
		size_t writePos = 0;

		while ( readPos < srcSize )
		{
			const uint8 control = pInput[readPos++];

			if ( ( control & 0x80 ) != 0 )
			{
				// 반복 런
				const size_t runLength = static_cast<size_t>( control & 0x7F ) + 3;
				if ( readPos >= srcSize )
					return false;

				const uint8 value = pInput[readPos++];
				if ( writePos + runLength > dstCapacity )
					return false;

				Memory::set( &pOutput[writePos], value, runLength );
				writePos += runLength;
			}
			else
			{
				// 리터럴 런
				const size_t literalLength = static_cast<size_t>( control ) + 1;
				if ( readPos + literalLength > srcSize || writePos + literalLength > dstCapacity )
					return false;

				Memory::copy( &pOutput[writePos], &pInput[readPos], literalLength );
				readPos += literalLength;
				writePos += literalLength;
			}
		}

		outUncompressedSize = writePos;
		return true;
	}
} // namespace sw
