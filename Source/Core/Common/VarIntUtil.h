/**
 * @file VarIntUtil.h
 * @brief LEB128 가변 길이 정수 인코딩 및 ZigZag 부호 있는 정수 압축 유틸리티
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
	/**
	 * @struct VarIntUtil
	 * @brief LEB128 가변 길이 정수 인코딩 및 ZigZag 인코딩/디코딩 정적 유틸리티
	 */
	struct VarIntUtil
	{
		/**
		 * @brief 64비트 부호 없는 정수를 LEB128 가변 길이 바이트열로 벡터에 추가합니다.
		 * @param value 인코딩할 64비트 정수 (0~127: 1B, 128~16383: 2B 등)
		 * @param outBytes 대상 바이트 벡터
		 * @return 기록된 바이트 수 (1~10)
		 */
		static size_t encodeVarUInt64( uint64 value, vector<uint8>& outBytes )
		{
			size_t written = 0;
			do
			{
				uint8 byte = static_cast<uint8>( value & 0x7FULL );
				value >>= 7;
				if ( value != 0 )
					byte |= 0x80;
				outBytes.push_back( byte );
				++written;
			} while ( value != 0 );
			return written;
		}

		/**
		 * @brief 32비트 부호 없는 정수를 LEB128 가변 길이 바이트열로 벡터에 추가합니다.
		 */
		static size_t encodeVarUInt32( uint32 value, vector<uint8>& outBytes )
		{
			return encodeVarUInt64( static_cast<uint64>( value ), outBytes );
		}

		/**
		 * @brief 64비트 부호 있는 정수를 ZigZag 인코딩 후 LEB128로 벡터에 추가합니다.
		 */
		static size_t encodeVarInt64( int64 value, vector<uint8>& outBytes )
		{
			const uint64 zigZag = static_cast<uint64>( ( value << 1 ) ^ ( value >> 63 ) );
			return encodeVarUInt64( zigZag, outBytes );
		}

		/**
		 * @brief 32비트 부호 있는 정수를 ZigZag 인코딩 후 LEB128로 벡터에 추가합니다.
		 */
		static size_t encodeVarInt32( int32 value, vector<uint8>& outBytes )
		{
			return encodeVarInt64( static_cast<int64>( value ), outBytes );
		}

		/**
		 * @brief 버퍼에서 LEB128 인코딩된 64비트 부호 없는 정수를 읽습니다.
		 * @param pData 데이터 시작 포인터
		 * @param dataSize 전체 버퍼 크기
		 * @param inoutOffset 현재 오프셋 (성공 시 소비된 바이트만큼 증가)
		 * @param outValue 디코딩된 값 출력
		 * @return 성공 여부
		 */
		static bool decodeVarUInt64( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint64& outValue )
		{
			if ( pData == nullptr || inoutOffset >= dataSize )
				return false;

			uint64 result	 = 0;
			uint32 shift	 = 0;
			size_t curOffset = inoutOffset;

			while ( curOffset < dataSize && shift < 64 )
			{
				const uint8 byte = pData[curOffset++];
				result |= static_cast<uint64>( byte & 0x7FULL ) << shift;
				if ( ( byte & 0x80 ) == 0 )
				{
					outValue	= result;
					inoutOffset = curOffset;
					return true;
				}
				shift += 7;
			}
			return false;
		}

		/**
		 * @brief 버퍼에서 LEB128 인코딩된 32비트 부호 없는 정수를 읽습니다.
		 */
		static bool decodeVarUInt32( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint32& outValue )
		{
			uint64 val64 = 0;
			if ( decodeVarUInt64( pData, dataSize, inoutOffset, val64 ) == false )
				return false;
			outValue = static_cast<uint32>( val64 );
			return true;
		}

		/**
		 * @brief 버퍼에서 ZigZag + LEB128 인코딩된 64비트 부호 있는 정수를 읽습니다.
		 */
		static bool decodeVarInt64( const uint8* pData, size_t dataSize, size_t& inoutOffset, int64& outValue )
		{
			uint64 zigZag = 0;
			if ( decodeVarUInt64( pData, dataSize, inoutOffset, zigZag ) == false )
				return false;
			const uint64 mask = static_cast<uint64>( -( static_cast<int64>( zigZag & 1ULL ) ) );
			outValue		  = static_cast<int64>( ( zigZag >> 1 ) ^ mask );
			return true;
		}

		/**
		 * @brief 버퍼에서 ZigZag + LEB128 인코딩된 32비트 부호 있는 정수를 읽습니다.
		 */
		static bool decodeVarInt32( const uint8* pData, size_t dataSize, size_t& inoutOffset, int32& outValue )
		{
			int64 val64 = 0;
			if ( decodeVarInt64( pData, dataSize, inoutOffset, val64 ) == false )
				return false;
			outValue = static_cast<int32>( val64 );
			return true;
		}
	};
} // namespace sw
