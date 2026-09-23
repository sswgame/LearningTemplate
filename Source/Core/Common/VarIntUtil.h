/**
 * @file VarIntUtil.h
 * @brief LEB128 가변 길이 정수 인코딩과 ZigZag(부호 있는 정수를 작은 부호 없는 정수로 바꾸기) 도우미입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct VarIntUtil
     * @brief LEB128 · ZigZag 인코딩과 디코딩을 하는 정적 도우미입니다.
     */
    struct VarIntUtil
    {
        // **인코딩에는 32비트 오버로드가 없다(일부러 비대칭이다).**
        // 32비트 값을 `encodeVarUint64` 에 넘기면 승격되어 같은 바이트가 나온다. 오버로드는 `static_cast` 한 줄을 감쌀 뿐
        // 하는 일이 없었고, 실제로 쓰는 곳도 없었다(`BinaryStream` 도 32비트 값을 64비트 인코더로 넣는다).
        // 반대로 **디코딩에는 있다.** 그쪽은 캐스팅이 아니라 uint32/int32 범위를 벗어난 값을 걸러 내는 진짜 검사를 한다.
        //
        // 오랫동안 이 설명만 맞고 코드는 틀렸다. 32비트 디코더 둘 다 `static_cast` 한 줄이라 범위 밖 값을 **조용히 잘라** 냈고,
        // 그래서 망가진 아카이브가 거부되지 않고 엉뚱하게 읽혔다(`Archive::readPooledString` 의 `poolId >= getCount()` 검사는
        // 0x1'0000'0000+n 이 n 으로 잘린 뒤에 보므로 통과한다). 지금은 `narrowToUint32` · `narrowToInt32` 가 실제로 거른다.

        /**
         * @brief 64비트 값이 uint32 에 **손실 없이** 들어갈 때만 옮깁니다.
         * @return 범위를 벗어나면 false 이고, 그때 outValue 는 그대로 둡니다.
         */
        static bool narrowToUint32( uint64 value, uint32& outValue )
        {
            if ( value > static_cast<uint64>( std::numeric_limits<uint32>::max() ) )
                return false;
            outValue = static_cast<uint32>( value );
            return true;
        }

        /**
         * @brief 64비트 값이 int32 에 **손실 없이** 들어갈 때만 옮깁니다.
         * @return 범위를 벗어나면 false 이고, 그때 outValue 는 그대로 둡니다.
         */
        static bool narrowToInt32( int64 value, int32& outValue )
        {
            if ( value < static_cast<int64>( std::numeric_limits<int32>::lowest() ) || value > static_cast<int64>( std::numeric_limits<int32>::max() ) )
                return false;
            outValue = static_cast<int32>( value );
            return true;
        }

        /**
         * @brief 64비트 부호 없는 정수를 LEB128 가변 길이 바이트열로 벡터에 추가합니다.
         * @param value 인코딩할 64비트 정수 (0~127: 1B, 128~16383: 2B 등)
         * @param outBytes 대상 바이트 벡터
         * @return 기록된 바이트 수 (1~10)
         */
        static size_t encodeVarUint64( uint64 value, vector<uint8>& outBytes )
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
         * @brief 64비트 부호 있는 정수를 ZigZag 인코딩 후 LEB128로 벡터에 추가합니다.
         */
        static size_t encodeVarInt64( int64 value, vector<uint8>& outBytes )
        {
            // 부호 있는 값의 왼쪽 시프트는 C++17 에서 **음수면 UB** 다. 부호 없는 쪽에서 민다.
            const uint64 zigZag = ( static_cast<uint64>( value ) << 1 ) ^ static_cast<uint64>( value >> 63 );
            return encodeVarUint64( zigZag, outBytes );
        }

        /**
         * @brief 버퍼에서 LEB128 인코딩된 64비트 부호 없는 정수를 읽습니다.
         * @param pData 데이터 시작 포인터
         * @param dataSize 전체 버퍼 크기
         * @param inoutOffset 현재 오프셋 (성공 시 소비된 바이트만큼 증가)
         * @param outValue 디코딩된 값 출력
         * @return 성공 여부
         */
        static bool decodeVarUint64( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint64& outValue )
        {
            if ( pData == nullptr || inoutOffset >= dataSize )
                return false;

            uint64 result    = 0;
            uint32 shift     = 0;
            size_t curOffset = inoutOffset;

            while ( curOffset < dataSize && shift < 64 )
            {
                const uint8 byte = pData[curOffset++];
                // 10번째 바이트(shift == 63)에 남은 자리는 1비트뿐이다. 나머지 비트가 켜져 있으면 64비트를 넘는 값이거나
                // 불필요하게 긴 인코딩이다 — 조용히 버리지 말고 거절한다. 정상 인코더는 여기서 0 이나 1 만 낸다
                // (encodeVarUint64 의 마지막 바이트).
                if ( shift == 63 && ( byte & 0x7E ) != 0 )
                    return false;
                result |= static_cast<uint64>( byte & 0x7FULL ) << shift;
                if ( ( byte & 0x80 ) == 0 )
                {
                    outValue    = result;
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
        static bool decodeVarUint32( const uint8* pData, size_t dataSize, size_t& inoutOffset, uint32& outValue )
        {
            size_t curOffset = inoutOffset;
            uint64 val64     = 0;
            if ( decodeVarUint64( pData, dataSize, curOffset, val64 ) == false )
                return false;
            // 범위를 벗어나면 **오프셋도 되돌린다** — 실패한 읽기가 스트림을 먹고 가면 안 된다.
            if ( narrowToUint32( val64, outValue ) == false )
                return false;
            inoutOffset = curOffset;
            return true;
        }

        /**
         * @brief 버퍼에서 ZigZag + LEB128 인코딩된 64비트 부호 있는 정수를 읽습니다.
         */
        static bool decodeVarInt64( const uint8* pData, size_t dataSize, size_t& inoutOffset, int64& outValue )
        {
            uint64 zigZag = 0;
            if ( decodeVarUint64( pData, dataSize, inoutOffset, zigZag ) == false )
                return false;
            const uint64 mask = static_cast<uint64>( -( static_cast<int64>( zigZag & 1ULL ) ) );
            outValue          = static_cast<int64>( ( zigZag >> 1 ) ^ mask );
            return true;
        }

        /**
         * @brief 버퍼에서 ZigZag + LEB128 인코딩된 32비트 부호 있는 정수를 읽습니다.
         */
        static bool decodeVarInt32( const uint8* pData, size_t dataSize, size_t& inoutOffset, int32& outValue )
        {
            size_t curOffset = inoutOffset;
            int64  val64     = 0;
            if ( decodeVarInt64( pData, dataSize, curOffset, val64 ) == false )
                return false;
            if ( narrowToInt32( val64, outValue ) == false )
                return false;
            inoutOffset = curOffset;
            return true;
        }
    };
} // namespace sw
