#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	uint32 CompressionStream::calculateChecksum( const void* pData, size_t dataSize )
	{
		if ( pData == nullptr || dataSize == 0 )
			return 0;

		const auto* const pBytes = static_cast<const uint8*>( pData );
		uint32			  hash	 = 2166136261u;
		for ( size_t byteIndex = 0; byteIndex < dataSize; ++byteIndex )
		{
			hash ^= pBytes[byteIndex];
			hash *= 16777619u;
		}
		return hash;
	}

	bool CompressionStream::verifyHeader( const void* pData, size_t dataSize, CompressionHeader& outHeader )
	{
		if ( pData == nullptr || dataSize < sizeof( CompressionHeader ) )
			return false;

		Memory::copy( &outHeader, pData, sizeof( CompressionHeader ) );
		if ( outHeader._magic != kMagicNumber )
			return false;

		if ( outHeader._version != 1 )
			return false;

		if ( outHeader._compressedSize + sizeof( CompressionHeader ) > dataSize )
			return false;

		return true;
	}

	bool CompressionStream::compressBuffer( const void*			 pSrc,
											size_t				 srcSize,
											vector<uint8>&		 listOutBuffer,
											CompressionCodecType codecType,
											int32				 compressionLevel )
	{
		listOutBuffer.clear();
		if ( pSrc == nullptr || srcSize == 0 )
			return true;

		ICompressionCodec* pCodec = CompressionCodecRegistry::get().getCodec( codecType );
		if ( pCodec == nullptr )
		{
			SW_LOG_WARNING( "[CompressionStream] Requested codec %# not found, falling back to Null codec", static_cast<uint8>( codecType ) );
			pCodec = CompressionCodecRegistry::get().getCodec( CompressionCodecType::None );
			if ( pCodec == nullptr )
				return false;
			codecType = CompressionCodecType::None;
		}

		const size_t bound = pCodec->compressBound( srcSize );
		listOutBuffer.resize( sizeof( CompressionHeader ) + bound );

		auto* const pHeader		   = reinterpret_cast<CompressionHeader*>( listOutBuffer.data() );
		pHeader->_magic			   = kMagicNumber;
		pHeader->_version		   = 1;
		pHeader->_codecType		   = static_cast<uint8>( codecType );
		pHeader->_flags			   = 0x01; // With Checksum
		pHeader->_uncompressedSize = static_cast<uint64>( srcSize );
		pHeader->_checksum		   = calculateChecksum( pSrc, srcSize );

		uint8* const pDstPayload	= listOutBuffer.data() + sizeof( CompressionHeader );
		size_t		 compressedSize = 0;

		const bool bSuccess = pCodec->compress( pSrc, srcSize, pDstPayload, bound, compressedSize, compressionLevel );
		if ( bSuccess == false )
		{
			listOutBuffer.clear();
			return false;
		}

		pHeader->_compressedSize = static_cast<uint64>( compressedSize );
		listOutBuffer.resize( sizeof( CompressionHeader ) + compressedSize );
		return true;
	}

	bool CompressionStream::decompressBuffer( const void*	 pSrc,
											  size_t		 srcSize,
											  vector<uint8>& listOutBuffer )
	{
		listOutBuffer.clear();
		if ( pSrc == nullptr || srcSize == 0 )
			return true;

		CompressionHeader header{};
		if ( verifyHeader( pSrc, srcSize, header ) == false )
		{
			SW_LOG_ERROR( "[CompressionStream] Invalid compression header (size=%#)", srcSize );
			return false;
		}

		if ( header._uncompressedSize == 0 )
			return true;

		listOutBuffer.resize( static_cast<size_t>( header._uncompressedSize ) );

		size_t	   outUncompressedSize = 0;
		const bool bSuccess			   = decompressBuffer( pSrc, srcSize, listOutBuffer.data(), listOutBuffer.size(), outUncompressedSize );
		if ( bSuccess == false || outUncompressedSize != static_cast<size_t>( header._uncompressedSize ) )
		{
			listOutBuffer.clear();
			return false;
		}

		return true;
	}

	bool CompressionStream::decompressBuffer( const void* pSrc,
											  size_t	  srcSize,
											  void*		  pDst,
											  size_t	  dstCapacity,
											  size_t&	  outUncompressedSize )
	{
		outUncompressedSize = 0;
		if ( pSrc == nullptr || srcSize == 0 )
			return true;

		if ( pDst == nullptr )
			return false;

		CompressionHeader header{};
		if ( verifyHeader( pSrc, srcSize, header ) == false )
			return false;

		if ( dstCapacity < static_cast<size_t>( header._uncompressedSize ) )
			return false;

		const auto		   codecType = static_cast<CompressionCodecType>( header._codecType );
		ICompressionCodec* pCodec	 = CompressionCodecRegistry::get().getCodec( codecType );
		if ( pCodec == nullptr )
		{
			SW_LOG_ERROR( "[CompressionStream] Unsupported codec type in stream: %#", header._codecType );
			return false;
		}

		const uint8* const pSrcPayload	  = static_cast<const uint8*>( pSrc ) + sizeof( CompressionHeader );
		const size_t	   srcPayloadSize = static_cast<size_t>( header._compressedSize );

		const bool bSuccess = pCodec->decompress( pSrcPayload, srcPayloadSize, pDst, dstCapacity, outUncompressedSize );
		if ( bSuccess == false )
		{
			SW_LOG_ERROR( "[CompressionStream] Decompression failed using codec: %#", pCodec->getCodecName() );
			return false;
		}

		if ( ( header._flags & 0x01 ) != 0 )
		{
			const uint32 calculatedChecksum = calculateChecksum( pDst, outUncompressedSize );
			if ( calculatedChecksum != header._checksum )
			{
				SW_LOG_ERROR( "[CompressionStream] Checksum mismatch: expected %#x, got %#x", header._checksum, calculatedChecksum );
				return false;
			}
		}

		return true;
	}
} // namespace sw
