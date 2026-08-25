#include "pch.h"

#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/CompressedBinarySerializer.h"

namespace sw
{
	bool CompressedBinarySerializer::serializeCompressed( const void*			  pInstance,
														  const TypeInfo&		  typeInfo,
														  vector<uint8>&		  listOutBuffer,
														  CompressionCodecType	  codecType,
														  const SerializeContext& ctx )
	{
		listOutBuffer.clear();
		if ( pInstance == nullptr )
			return false;

		vector<uint8> listRawBinary;
		BinarySerializer::serialize( pInstance, typeInfo, listRawBinary, ctx );
		if ( listRawBinary.empty() )
			return false;

		return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), listOutBuffer, codecType );
	}

	bool CompressedBinarySerializer::deserializeCompressed( void*					pInstance,
															const TypeInfo&			typeInfo,
															const uint8*			pData,
															size_t					dataSize,
															const SerializeContext& ctx )
	{
		if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
			return false;

		vector<uint8> listRawBinary;
		const bool	  bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
		if ( bDecompressOk == false || listRawBinary.empty() )
			return false;

		return BinarySerializer::deserialize( pInstance, typeInfo, listRawBinary.data(), listRawBinary.size(), ctx );
	}

	bool CompressedBinarySerializer::serializeVersionedCompressed( uint32				   version,
																   const void*			   pInstance,
																   const TypeInfo&		   typeInfo,
																   vector<uint8>&		   listOutBuffer,
																   CompressionCodecType	   codecType,
																   const SerializeContext& ctx )
	{
		listOutBuffer.clear();
		if ( pInstance == nullptr )
			return false;

		vector<uint8> listRawBinary;
		BinarySerializer::serializeVersioned( version, pInstance, typeInfo, listRawBinary, ctx );
		if ( listRawBinary.empty() )
			return false;

		return CompressionStream::compressBuffer( listRawBinary.data(), listRawBinary.size(), listOutBuffer, codecType );
	}

	bool CompressedBinarySerializer::deserializeVersionedCompressed( uint32&				 outVersion,
																	 void*					 pInstance,
																	 const TypeInfo&		 typeInfo,
																	 const uint8*			 pData,
																	 size_t					 dataSize,
																	 uint32					 currentVersion,
																	 SchemaMigrateFn		 migrate,
																	 const TypeInfo*		 pLegacyTypeInfo,
																	 const SerializeContext& ctx )
	{
		outVersion = 0;
		if ( pInstance == nullptr || pData == nullptr || dataSize == 0 )
			return false;

		vector<uint8> listRawBinary;
		const bool	  bDecompressOk = CompressionStream::decompressBuffer( pData, dataSize, listRawBinary );
		if ( bDecompressOk == false || listRawBinary.empty() )
			return false;

		return BinarySerializer::deserializeVersioned( outVersion,
													   pInstance,
													   typeInfo,
													   listRawBinary.data(),
													   listRawBinary.size(),
													   currentVersion,
													   migrate,
													   pLegacyTypeInfo,
													   ctx );
	}
} // namespace sw
