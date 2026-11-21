/**
 * @file ByteStream.h
 * @brief 바이트 단위 직렬화 — 리틀 엔디언 정수 · 실수를 버퍼 끝에 덧붙이는 `ByteWriter` 와, 범위를 검사하며 읽는 `ByteReader` 입니다.
 * @details 파일 형식(프로브 · 메시 · 파괴 기록)과 패킷이 같은 바이트 순서 규칙을 쓰도록 한 자리에 둡니다. 바이트 순서는 `LittleEndianUtil` 이 정하고
 *          (호스트 순서와 무관), 이 두 타입은 위치와 범위를 맡습니다. 비트 단위로 줄여 쓰는 패킷은 `BitStream.h` 입니다.
 *          읽기가 남은 바이트를 넘으면 0 을 돌려주고 `hasOverflowed` 가 참이 됩니다(`BitReader` 와 같은 규칙) — 깨진 파일을 받아도 버퍼 밖을 읽지 않으며,
 *          부르는 쪽은 필드를 모두 읽은 뒤 한 번 확인합니다. `read*( T& )` 꼴(가변 정수 · 길이 붙은 글 · POD)은 바로 bool 로도 답한다(넘침 표시도 함께 남긴다).
 *          **파일이 적은 개수는 이 타입의 개수 읽기(`readCount`)와 목록 읽기(`readList` · `readCountedList` · `readPodList`)로만 할당한다.** 남은 바이트로 먼저 거르고 곱셈 대신 나눗셈으로 재므로, 위조된 개수 하나가
 *          수십 기가 할당이나 넘친 곱셈으로 "길이가 맞아" 보이는 파일이 되지 못한다. 바이트 순서 읽기(`LittleEndianUtil::read*`)는 이 타입만 부른다(비공개 + friend). 언리얼 `FArchive::IsError` 처럼 끈적한 실패이고,
 *          원소 최소 크기로 미리 재는 것은 언리얼 `TArray` 직렬화의 `Ar.IsLoading()` 음수 · 상한 검사를 형식 쪽으로 옮긴 것이다(기억에 따른 비교).
 *          `readFloat32` 는 유한한 값만 받는다 — NaN 은 모든 비교를 빠져나가 깨진 파일이 값으로 번진다(`Archive::operator>>` · `BitReader::readFiniteFloat` 와 같은 규칙).
 *          POD 통째 쓰기(`writePod` · `readPod`)는 호스트 바이트를 그대로 옮긴다 — 지원 플랫폼이 모두 리틀 엔디언이라 정수 쓰기와 같은 바이트다(아래 static_assert).
 *          예전 Engine `BinaryStreamWriter/Reader`(호스트 순서 memcpy · 가변 정수)를 이 둘로 합쳤다 — 한 층에 바이트 스트림 모델이 둘이었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/Serialization/VarIntUtil.h"

#include <initializer_list>
#include <type_traits>

#if defined( __BYTE_ORDER__ )
static_assert( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "ByteStream writePod/readPod copy host bytes - they equal the little-endian file format only on little-endian hosts" );
#endif

namespace sw
{
    /**
     * @class ByteWriter
     * @brief 빌린 바이트 벡터 끝에 리틀 엔디언 값을 덧붙입니다. 벡터는 이 객체보다 오래 삽니다.
     */
    class ByteWriter
    {
    public:
        explicit ByteWriter( vector<uint8>& outBytes )
            : _bytes{ outBytes }
        {
        }

        void writeUint8( uint8 value ) { _bytes.push_back( value ); }
        void writeUint16( uint16 value ) { LittleEndianUtil::appendUint16( _bytes, value ); }
        void writeUint32( uint32 value ) { LittleEndianUtil::appendUint32( _bytes, value ); }
        void writeUint64( uint64 value ) { LittleEndianUtil::appendUint64( _bytes, value ); }
        void writeInt64( int64 value ) { LittleEndianUtil::appendInt64( _bytes, value ); }
        void writeFloat32( float32 value ) { LittleEndianUtil::appendFloat32( _bytes, value ); }
        void writeBytes( const uint8* pData, size_t byteCount )
        {
            if ( byteCount > 0 )
                _bytes.insert( _bytes.end(), pData, pData + byteCount );
        }
        void writeVarUint( uint64 value ) { VarIntUtil::encodeVarUint64( value, _bytes ); }
        void writeVarInt( int64 value ) { VarIntUtil::encodeVarInt64( value, _bytes ); }
        /** @brief 길이(uint32)와 바이트를 씁니다. */
        void writeString( string_view text )
        {
            writeUint32( static_cast<uint32>( text.size() ) );
            writeBytes( reinterpret_cast<const uint8*>( text.data() ), text.size() );
        }
        /** @brief 길이(uint32)와 바이트를 씁니다. */
        void writeByteVector( const vector<uint8>& bytes )
        {
            writeUint32( static_cast<uint32>( bytes.size() ) );
            writeBytes( bytes.data(), bytes.size() );
        }
        /** @brief 사소히 복사되는 값 하나를 호스트 바이트 그대로 씁니다(머리 구조체 · float3 등). */
        template <typename T>
        void writePod( const T& value )
        {
            static_assert( std::is_trivially_copyable_v<T>, "writePod copies bytes - T must be trivially copyable" );
            writeBytes( reinterpret_cast<const uint8*>( &value ), sizeof( T ) );
        }
        /** @brief 이미 쓴 자리 @p offset 에 값을 덮어씁니다(길이 · 오프셋 표를 나중에 채운다). 자리가 모자라면 아무것도 하지 않습니다. */
        template <typename T>
        void writePodAt( size_t offset, const T& value )
        {
            static_assert( std::is_trivially_copyable_v<T>, "writePodAt copies bytes - T must be trivially copyable" );
            // 자리는 이 버퍼가 앞서 쓴 길이 · 오프셋 표 칸이다(외부 입력이 아니다) — 개발 구성에서 알리고, 배포는 아래 가드로 아무것도 하지 않는다
            SW_CHECK_INDEX_OR_END( offset, _bytes.size() );
            if ( offset <= _bytes.size() )
                SW_CHECK_INDEX_OR_END( sizeof( T ), _bytes.size() - offset );
            if ( offset <= _bytes.size() && sizeof( T ) <= _bytes.size() - offset )
                Memory::copy( _bytes.data() + offset, &value, sizeof( T ) );
        }
        void reserve( size_t additionalByteCount ) { _bytes.reserve( _bytes.size() + additionalByteCount ); }

        /** @brief 지금까지 버퍼에 있는 바이트 수입니다(이 객체가 만들기 전부터 있던 바이트 포함). */
        size_t getByteCount() const { return _bytes.size(); }

    private:
        vector<uint8>& _bytes;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ByteReader
     * @brief 빌린 바이트를 앞에서부터 리틀 엔디언 값으로 읽습니다. 남은 바이트가 모자라면 읽지 않고 실패로 표시합니다.
     * @details **오류 관례는 하나다 — 끈적한 실패(`_bFailed`)**. 모든 읽기(값 반환 · `bool` 반환 모두)와 `markFailed` 가 같은 깃발을 세우고, 한 번 서면 내려가지 않는다.
     *          한 값 읽기는 모두 값을 돌려준다(`[[nodiscard]]`, 실패면 0 · 빈 값) — 출력 인자 `readX( T& out )` 은 없앴다: 반환 `bool` 을 버리면 실패를 잃었고
     *          (`(void)` · 무시), 실패 때 출력 칸이 옛 값으로 남았다. 실패는 깃발 하나라 매 줄 검사하지 않아도 된다 — 형식의 끝에서 `finish()` 한 번이 모든 실패와
     *          남은 바이트를 보고, 중간에 갈라야 하면 `hasOverflowed()` 를 본다(언리얼 `FArchive::IsError` 와 같은 자리). `finish()` 는 `[[nodiscard]]` 라 끝 검사를 잊는 모양이 컴파일 경고다.
     *          되돌리는 도구 · 남은 옛 모양 검사는 `py -3 -m Scripts byte-reader-values`(린트 `cpp-shape` 규칙 13).
     */
    class ByteReader
    {
    public:
        ByteReader( const uint8* pData, size_t byteCount )
            : _pData{ pData }
            , _byteCount{ pData != nullptr ? byteCount : 0 }
            , _offset{ 0 }
            , _bFailed{ SW_FALSE }
        {
        }

        /**
         * @brief 원소 @p declaredCount 개(하나가 적어도 @p minElementBytes 바이트)가 @p availableBytes 안에 담길 수 있으면 true 입니다.
         * @details 곱하지 않고 나눈다 — 곱셈은 넘쳐서 작은 파일과 "맞아" 보인다. 원소 크기 0 은 1 로 본다(개수 상한을 없애지 않는다).
         *          바이트를 이 타입으로 읽지 않는 쪽(`Archive::readCount`)도 이 계산 하나를 쓴다.
         */
        [[nodiscard]] static constexpr bool canHoldCount( uint64 declaredCount, uint64 minElementBytes, uint64 availableBytes ) noexcept
        {
            return declaredCount <= availableBytes / ( minElementBytes > 0 ? minElementBytes : 1 );
        }

        /** @brief 파일이 적은 차원들(너비 · 높이 · 층 …)의 곱을 @p outProduct 에 답합니다. 64 비트를 넘치면 false 입니다(32 비트 셋의 곱은 64 비트로도 넘친다). */
        [[nodiscard]] static constexpr bool tryMultiplyCounts( std::initializer_list<uint64> listCount, uint64& outProduct ) noexcept
        {
            uint64 product = 1;
            for ( const uint64 count : listCount )
            {
                if ( count != 0 && product > ~uint64{ 0 } / count )
                    return false;
                product *= count;
            }
            outProduct = product;
            return true;
        }

        [[nodiscard]] uint8 readUint8()
        {
            const uint8* pBytes = consume( 1 );
            return pBytes != nullptr ? pBytes[0] : uint8{ 0 };
        }
        [[nodiscard]] uint16 readUint16()
        {
            const uint8* pBytes = consume( 2 );
            return pBytes != nullptr ? LittleEndianUtil::readUint16( pBytes ) : uint16{ 0 };
        }
        [[nodiscard]] int16  readInt16() { return static_cast<int16>( readUint16() ); }
        [[nodiscard]] uint32 readUint32()
        {
            const uint8* pBytes = consume( 4 );
            return pBytes != nullptr ? LittleEndianUtil::readUint32( pBytes ) : 0u;
        }
        [[nodiscard]] uint64 readUint64()
        {
            const uint8* pBytes = consume( 8 );
            return pBytes != nullptr ? LittleEndianUtil::readUint64( pBytes ) : 0ull;
        }
        [[nodiscard]] int64 readInt64() { return static_cast<int64>( readUint64() ); }
        /** @brief 유한한 실수를 읽습니다. NaN · 무한대는 깨진 데이터로 보고 0 을 돌려주며 넘침으로 표시합니다. */
        [[nodiscard]] float32 readFloat32()
        {
            const uint8* pBytes = consume( 4 );
            if ( pBytes == nullptr )
                return 0.0f;
            // 지수 비트가 모두 1 이면 NaN · 무한대다. 이 층은 Math 를 끌어오지 않아도 된다.
            if ( ( LittleEndianUtil::readUint32( pBytes ) & 0x7F800000u ) == 0x7F800000u )
            {
                _bFailed = SW_TRUE;
                return 0.0f;
            }
            return LittleEndianUtil::readFloat32( pBytes );
        }
        /** @brief @p byteCount 바이트를 @p pOutData 에 복사합니다. 모자라면 false 이고 아무것도 쓰지 않습니다. */
        [[nodiscard]] bool readBytes( uint8* pOutData, size_t byteCount )
        {
            if ( byteCount == 0 )
                return true;
            const uint8* pBytes = consume( byteCount );
            if ( pBytes == nullptr )
                return false;
            Memory::copy( pOutData, pBytes, byteCount );
            return true;
        }
        /** @brief @p byteCount 바이트를 읽지 않고 넘깁니다. 모자라면 false 입니다. */
        [[nodiscard]] bool skipBytes( size_t byteCount ) { return byteCount == 0 || consume( byteCount ) != nullptr; }

        /** @brief 가변 길이 정수(`writeVarUint`)를 읽습니다. 깨졌거나 모자라면 0 이고 끈적한 실패로 표시합니다. */
        [[nodiscard]] uint64 readVarUint()
        {
            uint64 value = 0;
            return recordReadResult( VarIntUtil::decodeVarUint64( _pData, _byteCount, _offset, value ) ) ? value : 0ull;
        }
        /** @brief 32 비트를 넘지 않아야 하는 가변 길이 정수를 읽습니다. 넘거나 모자라면 0 이고 실패로 표시합니다. */
        [[nodiscard]] uint32 readVarUint32()
        {
            uint32 value = 0;
            return recordReadResult( VarIntUtil::decodeVarUint32( _pData, _byteCount, _offset, value ) ) ? value : 0u;
        }
        /** @brief 지그재그 가변 길이 정수(`writeVarInt`)를 읽습니다. 깨졌거나 모자라면 0 이고 실패로 표시합니다. */
        [[nodiscard]] int64 readVarInt()
        {
            int64 value = 0;
            return recordReadResult( VarIntUtil::decodeVarInt64( _pData, _byteCount, _offset, value ) ) ? value : int64{ 0 };
        }
        /** @brief 32 비트를 넘지 않아야 하는 지그재그 가변 길이 정수를 읽습니다. 넘거나 모자라면 0 이고 실패로 표시합니다. */
        [[nodiscard]] int32 readVarInt32()
        {
            int32 value = 0;
            return recordReadResult( VarIntUtil::decodeVarInt32( _pData, _byteCount, _offset, value ) ) ? value : int32{ 0 };
        }
        /** @brief `writeString` 이 쓴 글을 읽습니다. 모자라면 빈 글이고 실패로 표시합니다. */
        [[nodiscard]] string readString()
        {
            const string_view view = readStringView();
            return string{ view.data(), view.size() };
        }
        /** @brief `writeString` 이 쓴 글을 복사 없이 봅니다(버퍼보다 오래 쓰지 않는다). 모자라면 빈 뷰이고 실패로 표시합니다. */
        [[nodiscard]] string_view readStringView()
        {
            const uint32 size = readUint32();
            if ( hasOverflowed() || size == 0 )
                return {};
            const uint8* pBytes = consume( size );
            return pBytes != nullptr ? string_view{ reinterpret_cast<const utf8*>( pBytes ), size } : string_view{};
        }
        /** @brief `writeByteVector` 가 쓴 바이트를 읽습니다. 모자라면 빈 목록이고 실패로 표시합니다. */
        [[nodiscard]] vector<uint8> readByteVector()
        {
            const uint32 size = readUint32();
            if ( hasOverflowed() || size == 0 )
                return {};
            // 남은 바이트로 먼저 본다 — 깨진 길이 하나가 4 GB 할당을 일으키지 않게 한다.
            const uint8* pBytes = consume( size );
            return pBytes != nullptr ? vector<uint8>( pBytes, pBytes + size ) : vector<uint8>{};
        }
        /**
         * @brief @p byteCount 바이트를 복사 없이 봅니다(버퍼보다 오래 쓰지 않는다). 모자라면 nullptr 이고 실패로 표시합니다.
         * @details 0 바이트는 지금 자리를 돌려준다(빈 버퍼면 nullptr) — 실패는 반환값이 아니라 `hasOverflowed()` 로 본다.
         */
        [[nodiscard]] const uint8* readView( size_t byteCount ) { return byteCount == 0 ? _pData + _offset : consume( byteCount ); }
        /**
         * @brief 원소 @p declaredCount 개(하나가 적어도 @p minElementBytes 바이트)가 남은 바이트로 담길 수 있는지 봅니다. 없으면 넘침으로 표시하고 false 입니다.
         * @details 목록 읽기가 맞지 않는 자리(맵 · 글 묶음에 넣기)의 문지기입니다. 할당은 이 답을 본 뒤에만 합니다.
         */
        [[nodiscard]] bool canHold( uint64 declaredCount, size_t minElementBytes )
        {
            return recordReadResult( hasOverflowed() == false && canHoldCount( declaredCount, minElementBytes, getRemainingByteCount() ) );
        }
        /**
         * @brief 원소 개수(uint32)를 읽습니다. 원소 하나가 파일에서 적어도 @p minElementBytes 바이트이므로 남은 바이트로 담을 수 없는 개수면 0 이고 실패로 표시합니다.
         * @details 파일이 적은 개수로 맵 · 글 묶음처럼 목록 읽기가 맞지 않는 자리를 채울 때의 입구다(`Archive::readCount` 와 한 계산). 할당은 `hasOverflowed()` 를 본 뒤에만 한다.
         */
        [[nodiscard]] uint32 readCount( size_t minElementBytes )
        {
            const uint32 declaredCount = readUint32();
            return canHold( declaredCount, minElementBytes ) ? declaredCount : 0u;
        }
        /**
         * @brief 파일이 적은 개수만큼 목록을 읽습니다. 개수를 남은 바이트로 먼저 거른 뒤에 자리를 잡고, 원소마다 @p readElement 를 부릅니다.
         * @param minElementBytes 원소 하나가 파일에서 차지하는 최소 바이트입니다. `sizeof( T )` 가 아니라 **파일의** 크기입니다.
         * @param readElement `void( T& )` 또는 `bool( T& )`. false 를 돌려주거나 넘침이 나면 그 자리에서 멈춥니다.
         * @return 실패하면 목록을 비우고 false 입니다(반쯤 읽은 목록을 남기지 않는다).
         */
        template <typename TList, typename TReadElement>
        [[nodiscard]] bool readList( TList& outList, uint64 declaredCount, size_t minElementBytes, TReadElement&& readElement )
        {
            outList.clear();
            if ( canHold( declaredCount, minElementBytes ) == false )
                return false;
            outList.resize( static_cast<size_t>( declaredCount ) );
            for ( typename TList::value_type& element : outList )
            {
                bool bElementRead = true;
                if constexpr ( std::is_void_v<decltype( readElement( element ) )> )
                    readElement( element );
                else
                    bElementRead = readElement( element );
                if ( bElementRead == false || hasOverflowed() )
                {
                    outList.clear();
                    return false;
                }
            }
            return true;
        }
        /** @brief 앞에 붙은 개수(uint32)와 그 목록을 읽습니다(`readList` 규칙). */
        template <typename TList, typename TReadElement>
        [[nodiscard]] bool readCountedList( TList& outList, size_t minElementBytes, TReadElement&& readElement )
        {
            const uint32 declaredCount = readUint32();
            if ( hasOverflowed() )
            {
                outList.clear();
                return false;
            }
            return readList( outList, declaredCount, minElementBytes, static_cast<TReadElement&&>( readElement ) );
        }
        /** @brief 사소히 복사되는 원소 @p declaredCount 개를 통째로 읽습니다. 파일과 메모리의 배치가 같은 표(`writeBytes` 로 쓴 POD 배열)의 길입니다. 실패하면 비우고 false 입니다. */
        template <typename TList>
        [[nodiscard]] bool readPodList( TList& outList, uint64 declaredCount )
        {
            using TElement = typename TList::value_type;
            static_assert( std::is_trivially_copyable_v<TElement>, "readPodList copies bytes - the element must be trivially copyable" );
            outList.clear();
            if ( canHold( declaredCount, sizeof( TElement ) ) == false )
                return false;
            outList.resize( static_cast<size_t>( declaredCount ) );
            return readBytes( reinterpret_cast<uint8*>( outList.data() ), static_cast<size_t>( declaredCount ) * sizeof( TElement ) );
        }
        /** @brief `writePod` 가 쓴 값을 읽습니다. 모자라면 값 초기화한 `T` 이고 실패로 표시합니다. */
        template <typename T>
        [[nodiscard]] T readPod()
        {
            static_assert( std::is_trivially_copyable_v<T>, "readPod copies bytes - T must be trivially copyable" );
            T value{};
            if ( const uint8* pBytes = consume( sizeof( T ) ) )
                Memory::copy( reinterpret_cast<uint8*>( &value ), pBytes, sizeof( T ) );
            return value;
        }

        /** @brief 값이 형식에 맞지 않으면(길이 상한을 넘은 글 등) 부르는 쪽이 실패로 표시합니다. 바이트 부족과 같은 끈적한 실패입니다. */
        void   markFailed() { _bFailed = SW_TRUE; }
        bool   hasOverflowed() const { return _bFailed != SW_FALSE; }
        size_t getOffset() const { return _offset; }
        size_t getRemainingByteCount() const { return _byteCount - _offset; }
        /** @brief 실패 없이 끝까지 읽었으면 true 입니다. 형식의 끝에서 한 번 봅니다 — 남은 바이트가 있으면 길이가 틀린 파일입니다. */
        bool isFullyRead() const { return hasOverflowed() == false && getRemainingByteCount() == 0; }
        /**
         * @brief 형식의 끝에서 한 번 부르는 끝 검사입니다. 읽기 중 실패(바이트 부족 · `markFailed`)나 남은 바이트가 있으면 그 까닭을 오류로 돌려줍니다.
         * @details `isFullyRead` 와 같은 판정에 까닭(실패 / 남은 바이트 수)을 붙인다. 결과를 버리면 컴파일 경고다(`[[nodiscard]]`).
         */
        [[nodiscard]] expected<void> finish() const
        {
            if ( hasOverflowed() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "byte stream is malformed (read past the end or a value out of range)" } );
            if ( getRemainingByteCount() != 0 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "byte stream has trailing bytes (length does not match the format)" } );
            return {};
        }

    private:
        /** @brief 읽기 결과 @p bRead 를 그대로 돌려줍니다. false 면 스트림에 실패를 남깁니다(뒤 읽기 · `finish` 가 본다 — 언리얼 `FArchive::SetError` 자리). */
        bool recordReadResult( bool bRead )
        {
            if ( bRead == false )
                _bFailed = SW_TRUE;
            return bRead;
        }
        /** @brief @p byteCount 바이트를 차지하고 그 시작을 돌려줍니다. 모자라면 nullptr 이고 위치는 그대로, 넘침으로 표시합니다. */
        const uint8* consume( size_t byteCount )
        {
            if ( byteCount > _byteCount - _offset )
            {
                _bFailed = SW_TRUE;
                return nullptr;
            }
            const uint8* pBytes = _pData + _offset;
            _offset += byteCount;
            return pBytes;
        }

    private:
        const uint8* _pData;
        size_t       _byteCount;
        size_t       _offset;
        uint8        _bFailed;
    };
} // namespace sw
