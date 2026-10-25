#include "pch.h"

#include "GameFramework/Base/Online/HTTP/Hpack.h"

#include "GameFramework/Base/Online/HTTP/HpackHuffmanTable.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HpackInternal
        {
            struct StaticEntry
            {
                const utf8* _pName;
                const utf8* _pValue;
            };

            /** @brief RFC 7541 부록 A 의 정적 표입니다. 색인 1 부터 61 까지(배열 위치 + 1). */
            static constexpr StaticEntry kArrStatic[] = {
                {                 ":authority",              ""},
                {                    ":method",           "GET"},
                {                    ":method",          "POST"},
                {                      ":path",             "/"},
                {                      ":path",   "/index.html"},
                {                    ":scheme",          "http"},
                {                    ":scheme",         "https"},
                {                    ":status",           "200"},
                {                    ":status",           "204"},
                {                    ":status",           "206"},
                {                    ":status",           "304"},
                {                    ":status",           "400"},
                {                    ":status",           "404"},
                {                    ":status",           "500"},
                {             "accept-charset",              ""},
                {            "accept-encoding", "gzip, deflate"},
                {            "accept-language",              ""},
                {              "accept-ranges",              ""},
                {                     "accept",              ""},
                {"access-control-allow-origin",              ""},
                {                        "age",              ""},
                {                      "allow",              ""},
                {              "authorization",              ""},
                {              "cache-control",              ""},
                {        "content-disposition",              ""},
                {           "content-encoding",              ""},
                {           "content-language",              ""},
                {             "content-length",              ""},
                {           "content-location",              ""},
                {              "content-range",              ""},
                {               "content-type",              ""},
                {                     "cookie",              ""},
                {                       "date",              ""},
                {                       "etag",              ""},
                {                     "expect",              ""},
                {                    "expires",              ""},
                {                       "from",              ""},
                {                       "host",              ""},
                {                   "if-match",              ""},
                {          "if-modified-since",              ""},
                {              "if-none-match",              ""},
                {                   "if-range",              ""},
                {        "if-unmodified-since",              ""},
                {              "last-modified",              ""},
                {                       "link",              ""},
                {                   "location",              ""},
                {               "max-forwards",              ""},
                {         "proxy-authenticate",              ""},
                {        "proxy-authorization",              ""},
                {                      "range",              ""},
                {                    "referer",              ""},
                {                    "refresh",              ""},
                {                "retry-after",              ""},
                {                     "server",              ""},
                {                 "set-cookie",              ""},
                {  "strict-transport-security",              ""},
                {          "transfer-encoding",              ""},
                {                 "user-agent",              ""},
                {                       "vary",              ""},
                {                        "via",              ""},
                {           "www-authenticate",              ""},
            };
            static constexpr uint32 kStaticCount     = static_cast<uint32>( sizeof( kArrStatic ) / sizeof( kArrStatic[0] ) );
            static constexpr uint32 kEntryOverhead   = 32; ///< 항목 크기 = 이름 + 값 + 32 (RFC 7541 4.1)
            static constexpr uint32 kMaxIntegerShift = 28; ///< 이어지는 바이트로 늘어나는 정수의 상한 자리(32 비트 안)

            static uint32 entrySize( const HpackHeader& header ) { return static_cast<uint32>( header._name.size() + header._value.size() ) + kEntryOverhead; }

            static void writeString( const string& text, vector<uint8>& outListBlock )
            {
                HpackEncoder::writeInteger( static_cast<uint32>( text.size() ), 7, 0x00, outListBlock ); // H 비트 0 — 허프만 없이 글자 그대로
                outListBlock.insert( outListBlock.end(), text.begin(), text.end() );
            }

            static bool hasUppercase( const string& name )
            {
                for ( const utf8 ch : name )
                {
                    if ( 'A' <= ch && ch <= 'Z' )
                        return true;
                }
                return false;
            }
        };

        /** @brief 허프만 부호를 (비트 수, 부호) 순으로 정렬한 목록을 만듭니다. 항목은 `(비트 수 << 32 | 부호) << 9 | 기호` 다. 부호가 접두사 코드라 한 비트씩 늘려 가며 찾으면 처음 맞는 것이 기호다. */
        vector<uint64> buildHuffmanLookup()
        {
            vector<uint64> listLookup;
            listLookup.reserve( HpackHuffman::kSymbolCount );
            for ( int32 symbol = 0; symbol < HpackHuffman::kSymbolCount; ++symbol )
            {
                const HpackHuffmanCode& entry = HpackHuffman::kArrCode[symbol];
                listLookup.push_back( ( ( ( static_cast<uint64>( entry._bits ) << 32 ) | entry._code ) << 9 ) | static_cast<uint64>( symbol ) );
            }
            std::sort( listLookup.begin(), listLookup.end() );
            return listLookup;
        }

        const vector<uint64>& getHuffmanLookup()
        {
            static const vector<uint64> s_listLookup = buildHuffmanLookup();
            return s_listLookup;
        }
    } // namespace

    void HpackEncoder::writeInteger( uint32 value, int32 prefixBits, uint8 firstByteFlags, vector<uint8>& outListBlock )
    {
        const uint32 prefixMax = ( 1u << prefixBits ) - 1u;
        if ( value < prefixMax )
        {
            outListBlock.push_back( static_cast<uint8>( firstByteFlags | value ) );
            return;
        }
        outListBlock.push_back( static_cast<uint8>( firstByteFlags | prefixMax ) );
        value -= prefixMax;
        while ( value >= 128u )
        {
            outListBlock.push_back( static_cast<uint8>( ( value & 0x7Fu ) | 0x80u ) );
            value >>= 7;
        }
        outListBlock.push_back( static_cast<uint8>( value ) );
    }

    bool HpackEncoder::encode( const vector<HpackHeader>& listHeader, vector<uint8>& outListBlock )
    {
        using Internal = HpackInternal;
        for ( const HpackHeader& header : listHeader )
        {
            if ( header._name.empty() || Internal::hasUppercase( header._name ) )
                return false;
        }
        for ( const HpackHeader& header : listHeader )
        {
            uint32 nameIndex = 0;
            uint32 fullIndex = 0;
            for ( uint32 index = 0; index < Internal::kStaticCount; ++index )
            {
                if ( header._name != Internal::kArrStatic[index]._pName )
                    continue;
                if ( nameIndex == 0 )
                    nameIndex = index + 1;
                if ( header._value == Internal::kArrStatic[index]._pValue )
                {
                    fullIndex = index + 1;
                    break;
                }
            }
            if ( fullIndex != 0 )
            {
                writeInteger( fullIndex, 7, 0x80, outListBlock ); // 색인된 헤더
                continue;
            }
            if ( nameIndex != 0 )
            {
                writeInteger( nameIndex, 4, 0x00, outListBlock ); // 색인에 더하지 않는 글자 그대로, 이름은 색인
            }
            else
            {
                outListBlock.push_back( 0x00 ); // 색인에 더하지 않는 글자 그대로, 이름도 글자
                Internal::writeString( header._name, outListBlock );
            }
            Internal::writeString( header._value, outListBlock );
        }
        return true;
    }

    HpackDecoder::HpackDecoder( uint32 maxDynamicTableSize )
        : _listDynamic{}
        , _dynamicSize{ 0 }
        , _currentLimit{ maxDynamicTableSize }
        , _maxLimit{ maxDynamicTableSize }
    {
    }

    bool HpackDecoder::readInteger( const uint8*& pCursor, const uint8* pEnd, int32 prefixBits, uint32& outValue, string& outError ) const
    {
        using Internal = HpackInternal;
        if ( pCursor >= pEnd )
        {
            outError = "HPACK integer is cut off";
            return false;
        }
        const uint32 prefixMax = ( 1u << prefixBits ) - 1u;
        uint32       value     = static_cast<uint32>( *pCursor++ ) & prefixMax;
        if ( value < prefixMax )
        {
            outValue = value;
            return true;
        }
        uint32 shift = 0;
        while ( true )
        {
            if ( pCursor >= pEnd )
            {
                outError = "HPACK integer is cut off";
                return false;
            }
            if ( shift > Internal::kMaxIntegerShift )
            {
                outError = "HPACK integer is too large";
                return false;
            }
            const uint8 byte = *pCursor++;
            value += static_cast<uint32>( byte & 0x7Fu ) << shift;
            shift += 7;
            if ( ( byte & 0x80u ) == 0 )
                break;
        }
        outValue = value;
        return true;
    }

    bool HpackDecoder::decodeHuffman( const uint8* pData, size_t size, string& outText, string& outError ) const
    {
        const vector<uint64>& listLookup = getHuffmanLookup();
        outText.clear();
        uint32 code     = 0;
        int32  bitCount = 0;
        bool   bAllOnes = true; // 지금까지 모은 미완성 부호가 모두 1 인가(끝 패딩은 EOS 의 앞부분이라 1 이어야 한다)
        for ( size_t byteIndex = 0; byteIndex < size; ++byteIndex )
        {
            for ( int32 shift = 7; 0 <= shift; --shift )
            {
                const uint32 bit = ( pData[byteIndex] >> shift ) & 1u;
                code             = ( code << 1 ) | bit;
                ++bitCount;
                bAllOnes = bAllOnes && bit == 1u;
                if ( bitCount > 30 )
                {
                    outError = "HPACK Huffman string has an invalid code";
                    return false;
                }
                const uint64 key   = ( ( static_cast<uint64>( bitCount ) << 32 ) | code ) << 9;
                const auto   found = std::lower_bound( listLookup.begin(), listLookup.end(), key );
                if ( found == listLookup.end() || ( *found >> 9 ) != ( key >> 9 ) )
                    continue;
                const int32 symbol = static_cast<int32>( *found & 0x1FFu );
                if ( symbol == HpackHuffman::kEndOfString )
                {
                    outError = "HPACK Huffman string contains the end-of-string symbol";
                    return false;
                }
                outText.push_back( static_cast<utf8>( symbol ) );
                code     = 0;
                bitCount = 0;
                bAllOnes = true;
            }
        }
        if ( 7 < bitCount || bAllOnes == false )
        {
            outError = "HPACK Huffman string has invalid padding";
            return false;
        }
        return true;
    }

    bool HpackDecoder::readString( const uint8*& pCursor, const uint8* pEnd, string& outText, string& outError ) const
    {
        if ( pCursor >= pEnd )
        {
            outError = "HPACK string is cut off";
            return false;
        }
        const bool bHuffman = ( *pCursor & 0x80u ) != 0;
        uint32     length   = 0;
        if ( readInteger( pCursor, pEnd, 7, length, outError ) == false )
            return false;
        if ( length > static_cast<size_t>( pEnd - pCursor ) )
        {
            outError = "HPACK string is longer than the block";
            return false;
        }
        if ( bHuffman )
        {
            const bool bDecoded = decodeHuffman( pCursor, length, outText, outError );
            pCursor += length;
            return bDecoded;
        }
        outText.assign( reinterpret_cast<const utf8*>( pCursor ), length );
        pCursor += length;
        return true;
    }

    bool HpackDecoder::findEntry( uint32 index, HpackHeader& outHeader, string& outError ) const
    {
        using Internal = HpackInternal;
        if ( index == 0 )
        {
            outError = "HPACK index 0 is not valid";
            return false;
        }
        if ( index <= Internal::kStaticCount )
        {
            outHeader._name  = Internal::kArrStatic[index - 1]._pName;
            outHeader._value = Internal::kArrStatic[index - 1]._pValue;
            return true;
        }
        const uint32 dynamicIndex = index - Internal::kStaticCount - 1;
        if ( dynamicIndex >= _listDynamic.size() )
        {
            outError = "HPACK index is outside the tables";
            return false;
        }
        outHeader = _listDynamic[dynamicIndex];
        return true;
    }

    void HpackDecoder::shrinkTo( uint32 size )
    {
        while ( _dynamicSize > size && _listDynamic.empty() == false )
        {
            _dynamicSize -= HpackInternal::entrySize( _listDynamic.back() );
            _listDynamic.pop_back();
        }
    }

    void HpackDecoder::insert( const HpackHeader& header )
    {
        const uint32 size = HpackInternal::entrySize( header );
        shrinkTo( size > _currentLimit ? 0 : _currentLimit - size ); // 표보다 큰 항목은 표를 비우고 들어가지 않는다 (RFC 7541 4.4)
        if ( size > _currentLimit )
            return;
        _listDynamic.push_front( header );
        _dynamicSize += size;
    }

    bool HpackDecoder::decode( const uint8* pBlock, size_t size, vector<HpackHeader>& outListHeader, string& outError )
    {
        outListHeader.clear();
        const uint8* pCursor = pBlock;
        const uint8* pEnd    = pBlock + size;
        while ( pCursor < pEnd )
        {
            const uint8 first = *pCursor;
            HpackHeader header;
            if ( ( first & 0x80u ) != 0 ) // 색인된 헤더
            {
                uint32 index = 0;
                if ( readInteger( pCursor, pEnd, 7, index, outError ) == false || findEntry( index, header, outError ) == false )
                {
                    outListHeader.clear();
                    return false;
                }
                outListHeader.push_back( header );
                continue;
            }
            if ( ( first & 0xE0u ) == 0x20u ) // 동적 표 크기 갱신
            {
                uint32 newLimit = 0;
                if ( readInteger( pCursor, pEnd, 5, newLimit, outError ) == false )
                {
                    outListHeader.clear();
                    return false;
                }
                if ( newLimit > _maxLimit )
                {
                    outError = "HPACK table size update is above the limit we announced";
                    outListHeader.clear();
                    return false;
                }
                _currentLimit = newLimit;
                shrinkTo( newLimit );
                continue;
            }
            const bool  bIncremental = ( first & 0xC0u ) == 0x40u;
            const int32 prefixBits   = bIncremental ? 6 : 4;
            uint32      nameIndex    = 0;
            if ( readInteger( pCursor, pEnd, prefixBits, nameIndex, outError ) == false )
            {
                outListHeader.clear();
                return false;
            }
            bool bNameRead = true;
            if ( nameIndex == 0 )
                bNameRead = readString( pCursor, pEnd, header._name, outError );
            else
                bNameRead = findEntry( nameIndex, header, outError );
            if ( bNameRead == false || readString( pCursor, pEnd, header._value, outError ) == false )
            {
                outListHeader.clear();
                return false;
            }
            if ( bIncremental )
                insert( header );
            outListHeader.push_back( header );
        }
        return true;
    }
} // namespace sw
