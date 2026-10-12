/**
 * @file LittleEndianUtil.h
 * @brief 파일 · 네트워크 바이트를 리틀 엔디언 정수로 읽고 쓰는 도우미입니다 — 호스트 바이트 순서와 무관합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct LittleEndianUtil
     * @brief 리틀 엔디언 정수 하나를 바이트 포인터에서 읽고(`read*`), 그 자리에 쓰고(`write*`), 벡터 끝에 덧붙이는(`append*`) static 도우미입니다.
     * @details 범위 검사는 하지 않습니다 — 호출부가 남은 바이트 수를 먼저 확인합니다. 정렬되지 않은 주소도 됩니다(바이트 단위로 읽습니다).
     *          부호 있는 값은 같은 비트의 부호 없는 값으로 읽고 씁니다.
     */
    struct LittleEndianUtil
    {
        [[nodiscard]] static constexpr uint16 readUint16( const uint8* pData ) noexcept
        {
            return static_cast<uint16>( static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) );
        }

        [[nodiscard]] static constexpr uint32 readUint32( const uint8* pData ) noexcept
        {
            return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                   ( static_cast<uint32>( pData[3] ) << 24 );
        }

        [[nodiscard]] static constexpr uint64 readUint64( const uint8* pData ) noexcept
        {
            return static_cast<uint64>( readUint32( pData ) ) | ( static_cast<uint64>( readUint32( pData + 4 ) ) << 32 );
        }

        [[nodiscard]] static constexpr int16 readInt16( const uint8* pData ) noexcept { return static_cast<int16>( readUint16( pData ) ); }

        [[nodiscard]] static constexpr int64 readInt64( const uint8* pData ) noexcept { return static_cast<int64>( readUint64( pData ) ); }

        static constexpr void writeUint16( uint8* pOut, const uint16 value ) noexcept
        {
            pOut[0] = static_cast<uint8>( value & 0xFFu );
            pOut[1] = static_cast<uint8>( value >> 8 );
        }

        static constexpr void writeUint32( uint8* pOut, const uint32 value ) noexcept
        {
            for ( int32 byteIndex = 0; byteIndex < 4; ++byteIndex )
            {
                pOut[byteIndex] = static_cast<uint8>( value >> ( byteIndex * 8 ) );
            }
        }

        static constexpr void writeUint64( uint8* pOut, const uint64 value ) noexcept
        {
            for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
            {
                pOut[byteIndex] = static_cast<uint8>( value >> ( byteIndex * 8 ) );
            }
        }

        static constexpr void writeInt64( uint8* pOut, const int64 value ) noexcept { writeUint64( pOut, static_cast<uint64>( value ) ); }

        static void appendUint16( vector<uint8>& inoutBytes, const uint16 value )
        {
            inoutBytes.push_back( static_cast<uint8>( value & 0xFFu ) );
            inoutBytes.push_back( static_cast<uint8>( value >> 8 ) );
        }

        static void appendUint32( vector<uint8>& inoutBytes, const uint32 value )
        {
            for ( int32 byteIndex = 0; byteIndex < 4; ++byteIndex )
            {
                inoutBytes.push_back( static_cast<uint8>( value >> ( byteIndex * 8 ) ) );
            }
        }

        static void appendUint64( vector<uint8>& inoutBytes, const uint64 value )
        {
            for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
            {
                inoutBytes.push_back( static_cast<uint8>( value >> ( byteIndex * 8 ) ) );
            }
        }

        static void appendInt16( vector<uint8>& inoutBytes, const int16 value ) { appendUint16( inoutBytes, static_cast<uint16>( value ) ); }
    };
} // namespace sw
