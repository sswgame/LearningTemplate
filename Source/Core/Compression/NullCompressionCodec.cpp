#include "pch.h"

#include "Core/Compression/NullCompressionCodec.h"

#include "Core/Memory/Memory.h"
namespace sw
{
	CompressionCodecType NullCompressionCodec::getCodecType() const
	{
		return CompressionCodecType::None;
	}

	const utf8* NullCompressionCodec::getCodecName() const
	{
		return "Null";
	}

	size_t NullCompressionCodec::compressBound( size_t uncompressedSize ) const
	{
		return uncompressedSize;
	}

	bool NullCompressionCodec::compress( const void* pSrc,
										 size_t		 srcSize,
										 void*		 pDst,
										 size_t		 dstCapacity,
										 size_t&	 outCompressedSize,
										 int32 /*compressionLevel*/ )
	{
		outCompressedSize = 0;
		if ( srcSize == 0 )
			return true;

		if ( pSrc == nullptr || pDst == nullptr || dstCapacity < srcSize )
			return false;

		Memory::copy( pDst, pSrc, srcSize );
		outCompressedSize = srcSize;
		return true;
	}

	bool NullCompressionCodec::decompress( const void* pSrc,
										   size_t	   srcSize,
										   void*	   pDst,
										   size_t	   dstCapacity,
										   size_t&	   outUncompressedSize )
	{
		outUncompressedSize = 0;
		if ( srcSize == 0 )
			return true;

		if ( pSrc == nullptr || pDst == nullptr || dstCapacity < srcSize )
			return false;

		Memory::copy( pDst, pSrc, srcSize );
		outUncompressedSize = srcSize;
		return true;
	}
} // namespace sw
