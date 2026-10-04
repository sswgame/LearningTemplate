/**
 * @file BitStream.h
 * @brief 비트 단위 직렬화 — 범위가 정해진 정수는 필요한 비트만, 실수는 정밀도만큼 양자화, 가변 길이 정수, 바이트 덩어리입니다.
 * @details 비트는 낮은 비트부터 채우고, 쓰기 · 읽기는 바이트의 남은 칸만큼씩 묶어(32 비트도 다섯 번 안) 처리합니다. 바이트 경계의 덩어리는 통째로 복사합니다.
 *          스냅샷 · 입력은 수가 많고 패킷은 작으므로(1200 바이트) bool 하나에 1 비트, 0..100 체력에 7 비트를 씁니다(Gaffer "Reading and Writing Packets").
 *          읽기가 버퍼를 넘으면 0 을 돌려주고 `hasOverflowed` 가 참이 됩니다 — 깨진 패킷을 받아도 버퍼 밖을 읽지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct BitMath
     * @brief 비트 수 계산입니다.
     */
    struct BitMath
    {
        /** @brief [@p minValue, @p maxValue] 를 담는 데 드는 비트 수입니다. */
        static constexpr int32 computeBitsRequired( int64 minValue, int64 maxValue )
        {
            uint64 range = static_cast<uint64>( maxValue - minValue );
            int32  bits  = 0;
            while ( range > 0 )
            {
                ++bits;
                range >>= 1;
            }
            return bits;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 비트를 이어 씁니다. */
    class SW_API BitWriter
    {
    public:
        BitWriter();

        void writeBits( uint32 value, int32 bitCount );
        void writeBool( bool bValue ) { writeBits( bValue ? 1u : 0u, 1 ); }
        /** @brief [@p minValue, @p maxValue] 의 정수 — 범위를 넘으면 잘라 씁니다. */
        void writeInt( int32 value, int32 minValue, int32 maxValue );
        void writeUint32( uint32 value ) { writeBits( value, 32 ); }
        void writeFloat( float32 value );
        /** @brief [@p minValue, @p maxValue] 를 @p resolution 간격으로 양자화합니다(위치 0.01 m, 각도 0.1°). */
        void writeQuantizedFloat( float32 value, float32 minValue, float32 maxValue, float32 resolution );
        /** @brief 작은 수일수록 짧게(7 비트씩) 씁니다. */
        void writeVarUint( uint64 value );
        void writeVarInt( int64 value );
        void writeBytes( const uint8* pData, int32 byteCount );
        /** @brief 버퍼를 미리 잡아 둡니다(패킷 하나 = `kNetMaxPacketSize` — 쓰는 동안 다시 잡지 않게). */
        void reserve( int32 byteCount );
        /** @brief 다음 쓰기를 바이트 경계에서 시작합니다. */
        void alignToByte();
        /** @brief 비웁니다. 잡아 둔 버퍼는 남겨 다시 씁니다. */
        void clear();

        int32 getBitCount() const { return _bitCount; }
        int32 getByteCount() const { return ( _bitCount + 7 ) / 8; }
        /** @brief 마지막 바이트의 남은 비트는 0 입니다. */
        const vector<uint8>& getBytes() const { return _buffer; }

    private:
        vector<uint8> _buffer;
        int32         _bitCount;
    };
} // namespace sw

namespace sw
{
    /** @brief 비트를 이어 읽습니다. 데이터는 빌려 씁니다. */
    class SW_API BitReader
    {
    public:
        BitReader( const uint8* pData, int32 byteCount );

        uint32             readBits( int32 bitCount );
        [[nodiscard]] bool readBool() { return readBits( 1 ) != 0; }
        int32              readInt( int32 minValue, int32 maxValue );
        uint32             readUint32() { return readBits( 32 ); }
        float32            readFloat();
        float32            readQuantizedFloat( float32 minValue, float32 maxValue, float32 resolution );
        uint64             readVarUint();
        int64              readVarInt();
        /** @brief @p byteCount 바이트를 읽습니다. 모자라면 false 이고 읽지 않습니다. */
        [[nodiscard]] bool readBytes( uint8* pOutData, int32 byteCount );
        void               alignToByte();

        bool  hasOverflowed() const { return _bOverflow != SW_FALSE; }
        int32 getBitsRemaining() const { return _bitCapacity - _bitPosition; }
        int32 getBitPosition() const { return _bitPosition; }

    private:
        const uint8* _pData;
        int32        _bitCapacity;
        int32        _bitPosition;
        uint8        _bOverflow;
    };
} // namespace sw
