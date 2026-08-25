#pragma once

#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
	/**
	 * @class RleCompressionCodec
	 * @brief 엔진 내장 고속 Run-Length Encoding(RLE) 압축 코덱
	 * @details 외부 라이브러리 없이 독립적으로 동작하는 경량 패킷 바이트 런 압축 알고리즘입니다.
	 */
	class SW_API RleCompressionCodec final : public ICompressionCodec
	{
	public:
		RleCompressionCodec()											 = default;
		virtual ~RleCompressionCodec() override							 = default;
		RleCompressionCodec( const RleCompressionCodec& )				 = default;
		RleCompressionCodec& operator=( const RleCompressionCodec& )	 = default;
		RleCompressionCodec( RleCompressionCodec&& ) noexcept			 = default;
		RleCompressionCodec& operator=( RleCompressionCodec&& ) noexcept = default;

		virtual CompressionCodecType getCodecType() const override;
		virtual const char*			 getCodecName() const override;
		virtual size_t				 compressBound( size_t uncompressedSize ) const override;

		virtual bool compress( const void* pSrc,
							   size_t	   srcSize,
							   void*	   pDst,
							   size_t	   dstCapacity,
							   size_t&	   outCompressedSize,
							   int32	   compressionLevel = 0 ) override;

		virtual bool decompress( const void* pSrc,
								 size_t		 srcSize,
								 void*		 pDst,
								 size_t		 dstCapacity,
								 size_t&	 outUncompressedSize ) override;
	};
} // namespace sw
