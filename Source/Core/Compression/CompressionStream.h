#pragma once

#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
	/**
	 * @struct CompressionHeader
	 * @brief 압축 바이너리 컨테이너 헤더 (28 바이트)
	 */
#pragma pack( push, 1 )
	struct SW_API CompressionHeader
	{
		uint32 _magic{ 0x53574353 }; // 'SWCS' (SW Compression Stream)
		uint8  _version{ 1 };
		uint8  _codecType{ 0 }; // CompressionCodecType
		uint16 _flags{ 0 };		// 0x01: 체크섬 포함
		uint64 _uncompressedSize{ 0 };
		uint64 _compressedSize{ 0 };
		uint32 _checksum{ 0 }; // FNV-1a 체크섬 (무결성 검증용)
	};
#pragma pack( pop )

	/**
	 * @class CompressionStream
	 * @brief 바이너리 스트림 압축 및 역압축 헬퍼
	 * @details 헤더 캡슐화, 무결성 체크섬 검증, 자동 코덱 디스패칭을 수행합니다.
	 */
	class SW_API CompressionStream
	{
	public:
		static constexpr uint32 kMagicNumber = 0x53574353; // 'SWCS'

		/**
		 * @brief 메모리 버퍼를 압축하여 헤더가 포함된 압축 바이너리 스트림으로 생성합니다.
		 */
		static bool compressBuffer( const void*			 pSrc,
									size_t				 srcSize,
									vector<uint8>&		 listOutBuffer,
									CompressionCodecType codecType		  = CompressionCodecType::RLE,
									int32				 compressionLevel = 0 );

		/**
		 * @brief 압축 바이너리 스트림을 역압축하여 원본 바이트 버퍼를 복원합니다.
		 */
		static bool decompressBuffer( const void*	 pSrc,
									  size_t		 srcSize,
									  vector<uint8>& listOutBuffer );

		/**
		 * @brief 고정 크기 출력 버퍼에 압축 데이터를 복원합니다.
		 */
		static bool decompressBuffer( const void* pSrc,
									  size_t	  srcSize,
									  void*		  pDst,
									  size_t	  dstCapacity,
									  size_t&	  outUncompressedSize );

		/**
		 * @brief 압축 스트림 헤더를 검증하고 메타데이터를 파싱합니다.
		 */
		static bool verifyHeader( const void*		 pData,
								  size_t			 dataSize,
								  CompressionHeader& outHeader );

		/**
		 * @brief 버퍼의 FNV-1a 32비트 체크섬을 계산합니다.
		 */
		static uint32 calculateChecksum( const void* pData, size_t dataSize );
	};
} // namespace sw
