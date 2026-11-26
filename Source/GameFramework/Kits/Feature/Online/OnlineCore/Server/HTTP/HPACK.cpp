#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/HPACK.h"

#include "Core/Algorithm/VectorUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        // RFC 7541 부록 A(정적 표, 색인 1~61)와 부록 B(허프만 부호표, 기호 0~255 는 바이트 · 256 은 EOS)를 RFC 본문에서 기계로 옮긴 표다. 손으로 고치지 않는다.
        struct HPACKStaticEntry
        {
            const utf8* _pName;
            const utf8* _pValue;
        };

        namespace HPACKStatic
        {
            inline constexpr uint32 kEntryCount = 61;

            inline constexpr HPACKStaticEntry kArrEntry[kEntryCount] = {
                {                 ":authority",              ""}, // 1
                {                    ":method",           "GET"}, // 2
                {                    ":method",          "POST"}, // 3
                {                      ":path",             "/"}, // 4
                {                      ":path",   "/index.html"}, // 5
                {                    ":scheme",          "http"}, // 6
                {                    ":scheme",         "https"}, // 7
                {                    ":status",           "200"}, // 8
                {                    ":status",           "204"}, // 9
                {                    ":status",           "206"}, // 10
                {                    ":status",           "304"}, // 11
                {                    ":status",           "400"}, // 12
                {                    ":status",           "404"}, // 13
                {                    ":status",           "500"}, // 14
                {             "accept-charset",              ""}, // 15
                {            "accept-encoding", "gzip, deflate"}, // 16
                {            "accept-language",              ""}, // 17
                {              "accept-ranges",              ""}, // 18
                {                     "accept",              ""}, // 19
                {"access-control-allow-origin",              ""}, // 20
                {                        "age",              ""}, // 21
                {                      "allow",              ""}, // 22
                {              "authorization",              ""}, // 23
                {              "cache-control",              ""}, // 24
                {        "content-disposition",              ""}, // 25
                {           "content-encoding",              ""}, // 26
                {           "content-language",              ""}, // 27
                {             "content-length",              ""}, // 28
                {           "content-location",              ""}, // 29
                {              "content-range",              ""}, // 30
                {               "content-type",              ""}, // 31
                {                     "cookie",              ""}, // 32
                {                       "date",              ""}, // 33
                {                       "etag",              ""}, // 34
                {                     "expect",              ""}, // 35
                {                    "expires",              ""}, // 36
                {                       "from",              ""}, // 37
                {                       "host",              ""}, // 38
                {                   "if-match",              ""}, // 39
                {          "if-modified-since",              ""}, // 40
                {              "if-none-match",              ""}, // 41
                {                   "if-range",              ""}, // 42
                {        "if-unmodified-since",              ""}, // 43
                {              "last-modified",              ""}, // 44
                {                       "link",              ""}, // 45
                {                   "location",              ""}, // 46
                {               "max-forwards",              ""}, // 47
                {         "proxy-authenticate",              ""}, // 48
                {        "proxy-authorization",              ""}, // 49
                {                      "range",              ""}, // 50
                {                    "referer",              ""}, // 51
                {                    "refresh",              ""}, // 52
                {                "retry-after",              ""}, // 53
                {                     "server",              ""}, // 54
                {                 "set-cookie",              ""}, // 55
                {  "strict-transport-security",              ""}, // 56
                {          "transfer-encoding",              ""}, // 57
                {                 "user-agent",              ""}, // 58
                {                       "vary",              ""}, // 59
                {                        "via",              ""}, // 60
                {           "www-authenticate",              ""}, // 61
            };
        } // namespace HPACKStatic

        struct HPACKHuffmanCode
        {
            uint32 _code;
            uint8  _bits;
        };

        namespace HPACKHuffman
        {
            inline constexpr int32 kSymbolCount = 257;
            inline constexpr int32 kEndOfString = 256;

            inline constexpr HPACKHuffmanCode kArrCode[kSymbolCount] = {
                {    0x1ff8u, 13}, // 0
                {  0x7fffd8u, 23}, // 1
                { 0xfffffe2u, 28}, // 2
                { 0xfffffe3u, 28}, // 3
                { 0xfffffe4u, 28}, // 4
                { 0xfffffe5u, 28}, // 5
                { 0xfffffe6u, 28}, // 6
                { 0xfffffe7u, 28}, // 7
                { 0xfffffe8u, 28}, // 8
                {  0xffffeau, 24}, // 9
                {0x3ffffffcu, 30}, // 10
                { 0xfffffe9u, 28}, // 11
                { 0xfffffeau, 28}, // 12
                {0x3ffffffdu, 30}, // 13
                { 0xfffffebu, 28}, // 14
                { 0xfffffecu, 28}, // 15
                { 0xfffffedu, 28}, // 16
                { 0xfffffeeu, 28}, // 17
                { 0xfffffefu, 28}, // 18
                { 0xffffff0u, 28}, // 19
                { 0xffffff1u, 28}, // 20
                { 0xffffff2u, 28}, // 21
                {0x3ffffffeu, 30}, // 22
                { 0xffffff3u, 28}, // 23
                { 0xffffff4u, 28}, // 24
                { 0xffffff5u, 28}, // 25
                { 0xffffff6u, 28}, // 26
                { 0xffffff7u, 28}, // 27
                { 0xffffff8u, 28}, // 28
                { 0xffffff9u, 28}, // 29
                { 0xffffffau, 28}, // 30
                { 0xffffffbu, 28}, // 31
                {      0x14u,  6}, // 32
                {     0x3f8u, 10}, // 33
                {     0x3f9u, 10}, // 34
                {     0xffau, 12}, // 35
                {    0x1ff9u, 13}, // 36
                {      0x15u,  6}, // 37
                {      0xf8u,  8}, // 38
                {     0x7fau, 11}, // 39
                {     0x3fau, 10}, // 40
                {     0x3fbu, 10}, // 41
                {      0xf9u,  8}, // 42
                {     0x7fbu, 11}, // 43
                {      0xfau,  8}, // 44
                {      0x16u,  6}, // 45
                {      0x17u,  6}, // 46
                {      0x18u,  6}, // 47
                {       0x0u,  5}, // 48
                {       0x1u,  5}, // 49
                {       0x2u,  5}, // 50
                {      0x19u,  6}, // 51
                {      0x1au,  6}, // 52
                {      0x1bu,  6}, // 53
                {      0x1cu,  6}, // 54
                {      0x1du,  6}, // 55
                {      0x1eu,  6}, // 56
                {      0x1fu,  6}, // 57
                {      0x5cu,  7}, // 58
                {      0xfbu,  8}, // 59
                {    0x7ffcu, 15}, // 60
                {      0x20u,  6}, // 61
                {     0xffbu, 12}, // 62
                {     0x3fcu, 10}, // 63
                {    0x1ffau, 13}, // 64
                {      0x21u,  6}, // 65
                {      0x5du,  7}, // 66
                {      0x5eu,  7}, // 67
                {      0x5fu,  7}, // 68
                {      0x60u,  7}, // 69
                {      0x61u,  7}, // 70
                {      0x62u,  7}, // 71
                {      0x63u,  7}, // 72
                {      0x64u,  7}, // 73
                {      0x65u,  7}, // 74
                {      0x66u,  7}, // 75
                {      0x67u,  7}, // 76
                {      0x68u,  7}, // 77
                {      0x69u,  7}, // 78
                {      0x6au,  7}, // 79
                {      0x6bu,  7}, // 80
                {      0x6cu,  7}, // 81
                {      0x6du,  7}, // 82
                {      0x6eu,  7}, // 83
                {      0x6fu,  7}, // 84
                {      0x70u,  7}, // 85
                {      0x71u,  7}, // 86
                {      0x72u,  7}, // 87
                {      0xfcu,  8}, // 88
                {      0x73u,  7}, // 89
                {      0xfdu,  8}, // 90
                {    0x1ffbu, 13}, // 91
                {   0x7fff0u, 19}, // 92
                {    0x1ffcu, 13}, // 93
                {    0x3ffcu, 14}, // 94
                {      0x22u,  6}, // 95
                {    0x7ffdu, 15}, // 96
                {       0x3u,  5}, // 97
                {      0x23u,  6}, // 98
                {       0x4u,  5}, // 99
                {      0x24u,  6}, // 100
                {       0x5u,  5}, // 101
                {      0x25u,  6}, // 102
                {      0x26u,  6}, // 103
                {      0x27u,  6}, // 104
                {       0x6u,  5}, // 105
                {      0x74u,  7}, // 106
                {      0x75u,  7}, // 107
                {      0x28u,  6}, // 108
                {      0x29u,  6}, // 109
                {      0x2au,  6}, // 110
                {       0x7u,  5}, // 111
                {      0x2bu,  6}, // 112
                {      0x76u,  7}, // 113
                {      0x2cu,  6}, // 114
                {       0x8u,  5}, // 115
                {       0x9u,  5}, // 116
                {      0x2du,  6}, // 117
                {      0x77u,  7}, // 118
                {      0x78u,  7}, // 119
                {      0x79u,  7}, // 120
                {      0x7au,  7}, // 121
                {      0x7bu,  7}, // 122
                {    0x7ffeu, 15}, // 123
                {     0x7fcu, 11}, // 124
                {    0x3ffdu, 14}, // 125
                {    0x1ffdu, 13}, // 126
                { 0xffffffcu, 28}, // 127
                {   0xfffe6u, 20}, // 128
                {  0x3fffd2u, 22}, // 129
                {   0xfffe7u, 20}, // 130
                {   0xfffe8u, 20}, // 131
                {  0x3fffd3u, 22}, // 132
                {  0x3fffd4u, 22}, // 133
                {  0x3fffd5u, 22}, // 134
                {  0x7fffd9u, 23}, // 135
                {  0x3fffd6u, 22}, // 136
                {  0x7fffdau, 23}, // 137
                {  0x7fffdbu, 23}, // 138
                {  0x7fffdcu, 23}, // 139
                {  0x7fffddu, 23}, // 140
                {  0x7fffdeu, 23}, // 141
                {  0xffffebu, 24}, // 142
                {  0x7fffdfu, 23}, // 143
                {  0xffffecu, 24}, // 144
                {  0xffffedu, 24}, // 145
                {  0x3fffd7u, 22}, // 146
                {  0x7fffe0u, 23}, // 147
                {  0xffffeeu, 24}, // 148
                {  0x7fffe1u, 23}, // 149
                {  0x7fffe2u, 23}, // 150
                {  0x7fffe3u, 23}, // 151
                {  0x7fffe4u, 23}, // 152
                {  0x1fffdcu, 21}, // 153
                {  0x3fffd8u, 22}, // 154
                {  0x7fffe5u, 23}, // 155
                {  0x3fffd9u, 22}, // 156
                {  0x7fffe6u, 23}, // 157
                {  0x7fffe7u, 23}, // 158
                {  0xffffefu, 24}, // 159
                {  0x3fffdau, 22}, // 160
                {  0x1fffddu, 21}, // 161
                {   0xfffe9u, 20}, // 162
                {  0x3fffdbu, 22}, // 163
                {  0x3fffdcu, 22}, // 164
                {  0x7fffe8u, 23}, // 165
                {  0x7fffe9u, 23}, // 166
                {  0x1fffdeu, 21}, // 167
                {  0x7fffeau, 23}, // 168
                {  0x3fffddu, 22}, // 169
                {  0x3fffdeu, 22}, // 170
                {  0xfffff0u, 24}, // 171
                {  0x1fffdfu, 21}, // 172
                {  0x3fffdfu, 22}, // 173
                {  0x7fffebu, 23}, // 174
                {  0x7fffecu, 23}, // 175
                {  0x1fffe0u, 21}, // 176
                {  0x1fffe1u, 21}, // 177
                {  0x3fffe0u, 22}, // 178
                {  0x1fffe2u, 21}, // 179
                {  0x7fffedu, 23}, // 180
                {  0x3fffe1u, 22}, // 181
                {  0x7fffeeu, 23}, // 182
                {  0x7fffefu, 23}, // 183
                {   0xfffeau, 20}, // 184
                {  0x3fffe2u, 22}, // 185
                {  0x3fffe3u, 22}, // 186
                {  0x3fffe4u, 22}, // 187
                {  0x7ffff0u, 23}, // 188
                {  0x3fffe5u, 22}, // 189
                {  0x3fffe6u, 22}, // 190
                {  0x7ffff1u, 23}, // 191
                { 0x3ffffe0u, 26}, // 192
                { 0x3ffffe1u, 26}, // 193
                {   0xfffebu, 20}, // 194
                {   0x7fff1u, 19}, // 195
                {  0x3fffe7u, 22}, // 196
                {  0x7ffff2u, 23}, // 197
                {  0x3fffe8u, 22}, // 198
                { 0x1ffffecu, 25}, // 199
                { 0x3ffffe2u, 26}, // 200
                { 0x3ffffe3u, 26}, // 201
                { 0x3ffffe4u, 26}, // 202
                { 0x7ffffdeu, 27}, // 203
                { 0x7ffffdfu, 27}, // 204
                { 0x3ffffe5u, 26}, // 205
                {  0xfffff1u, 24}, // 206
                { 0x1ffffedu, 25}, // 207
                {   0x7fff2u, 19}, // 208
                {  0x1fffe3u, 21}, // 209
                { 0x3ffffe6u, 26}, // 210
                { 0x7ffffe0u, 27}, // 211
                { 0x7ffffe1u, 27}, // 212
                { 0x3ffffe7u, 26}, // 213
                { 0x7ffffe2u, 27}, // 214
                {  0xfffff2u, 24}, // 215
                {  0x1fffe4u, 21}, // 216
                {  0x1fffe5u, 21}, // 217
                { 0x3ffffe8u, 26}, // 218
                { 0x3ffffe9u, 26}, // 219
                { 0xffffffdu, 28}, // 220
                { 0x7ffffe3u, 27}, // 221
                { 0x7ffffe4u, 27}, // 222
                { 0x7ffffe5u, 27}, // 223
                {   0xfffecu, 20}, // 224
                {  0xfffff3u, 24}, // 225
                {   0xfffedu, 20}, // 226
                {  0x1fffe6u, 21}, // 227
                {  0x3fffe9u, 22}, // 228
                {  0x1fffe7u, 21}, // 229
                {  0x1fffe8u, 21}, // 230
                {  0x7ffff3u, 23}, // 231
                {  0x3fffeau, 22}, // 232
                {  0x3fffebu, 22}, // 233
                { 0x1ffffeeu, 25}, // 234
                { 0x1ffffefu, 25}, // 235
                {  0xfffff4u, 24}, // 236
                {  0xfffff5u, 24}, // 237
                { 0x3ffffeau, 26}, // 238
                {  0x7ffff4u, 23}, // 239
                { 0x3ffffebu, 26}, // 240
                { 0x7ffffe6u, 27}, // 241
                { 0x3ffffecu, 26}, // 242
                { 0x3ffffedu, 26}, // 243
                { 0x7ffffe7u, 27}, // 244
                { 0x7ffffe8u, 27}, // 245
                { 0x7ffffe9u, 27}, // 246
                { 0x7ffffeau, 27}, // 247
                { 0x7ffffebu, 27}, // 248
                { 0xffffffeu, 28}, // 249
                { 0x7ffffecu, 27}, // 250
                { 0x7ffffedu, 27}, // 251
                { 0x7ffffeeu, 27}, // 252
                { 0x7ffffefu, 27}, // 253
                { 0x7fffff0u, 27}, // 254
                { 0x3ffffeeu, 26}, // 255
                {0x3fffffffu, 30}, // 256
            };
        } // namespace HPACKHuffman
    } // namespace
} // namespace sw

namespace sw
{
    namespace
    {
        struct HPACKInternal
        {
            static constexpr uint32 kStaticCount     = HPACKStatic::kEntryCount;
            static constexpr uint32 kEntryOverhead   = 32; ///< 항목 크기 = 이름 + 값 + 32 (RFC 7541 4.1)
            static constexpr uint32 kMaxIntegerShift = 28; ///< 이어지는 바이트로 늘어나는 정수의 상한 자리(32 비트 안)

            static uint32 entrySize( const HTTPHeader& header ) { return static_cast<uint32>( header._name.size() + header._value.size() ) + kEntryOverhead; }

            static void writeString( const string& text, vector<uint8>& outListBlock )
            {
                HPACKEncoder::writeInteger( static_cast<uint32>( text.size() ), 7, 0x00, outListBlock ); // H 비트 0 — 허프만 없이 글자 그대로
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
            listLookup.reserve( HPACKHuffman::kSymbolCount );
            for ( int32 symbol = 0; symbol < HPACKHuffman::kSymbolCount; ++symbol )
            {
                const HPACKHuffmanCode& entry = HPACKHuffman::kArrCode[symbol];
                listLookup.push_back( ( ( ( static_cast<uint64>( entry._bits ) << 32 ) | entry._code ) << 9 ) | static_cast<uint64>( symbol ) );
            }
            VectorUtil::sort( listLookup );
            return listLookup;
        }

        const vector<uint64>& getHuffmanLookup()
        {
            static const vector<uint64> s_listLookup = buildHuffmanLookup();
            return s_listLookup;
        }
    } // namespace

    void HPACKEncoder::writeInteger( uint32 value, int32 prefixBits, uint8 firstByteFlags, vector<uint8>& outListBlock )
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

    bool HPACKEncoder::encode( const vector<HTTPHeader>& listHeader, vector<uint8>& outListBlock )
    {
        using Internal = HPACKInternal;
        for ( const HTTPHeader& header : listHeader )
        {
            if ( header._name.empty() || Internal::hasUppercase( header._name ) )
                return false;
        }
        for ( const HTTPHeader& header : listHeader )
        {
            uint32 nameIndex = 0;
            uint32 fullIndex = 0;
            for ( uint32 index = 0; index < Internal::kStaticCount; ++index )
            {
                if ( header._name != HPACKStatic::kArrEntry[index]._pName )
                    continue;
                if ( nameIndex == 0 )
                    nameIndex = index + 1;
                if ( header._value == HPACKStatic::kArrEntry[index]._pValue )
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

    HPACKDecoder::HPACKDecoder( uint32 maxDynamicTableSize, uint32 maxHeaderListBytes )
        : _listDynamic{}
        , _dynamicSize{ 0 }
        , _currentLimit{ maxDynamicTableSize }
        , _maxLimit{ maxDynamicTableSize }
        , _maxHeaderListBytes{ maxHeaderListBytes }
    {
    }

    expected<uint32> HPACKDecoder::readInteger( const uint8*& pCursor, const uint8* pEnd, int32 prefixBits ) const
    {
        using Internal = HPACKInternal;
        if ( pCursor >= pEnd )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK integer is cut off" } );
        const uint32 prefixMax = ( 1u << prefixBits ) - 1u;
        // 64 비트로 모아 uint32 를 넘는지 본다 — 32 비트로 모으면 자리 28 의 7 비트가 위로 잘리고 더하기가 감겨 작은 값이 된다.
        uint64 value = static_cast<uint64>( *pCursor++ ) & prefixMax;
        if ( value < prefixMax )
            return static_cast<uint32>( value );
        uint32 shift = 0;
        while ( true )
        {
            if ( pCursor >= pEnd )
                return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK integer is cut off" } );
            if ( shift > Internal::kMaxIntegerShift )
                return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK integer is too large" } );
            const uint8 byte = *pCursor++;
            value += static_cast<uint64>( byte & 0x7Fu ) << shift;
            if ( value > static_cast<uint64>( 0xFFFFFFFFu ) )
                return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK integer is too large" } );
            shift += 7;
            if ( ( byte & 0x80u ) == 0 )
                break;
        }
        return static_cast<uint32>( value );
    }

    expected<void> HPACKDecoder::decodeHuffman( const uint8* pData, size_t size, string& outText ) const
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
                    return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK Huffman string has an invalid code" } );
                const uint64 key   = ( ( static_cast<uint64>( bitCount ) << 32 ) | code ) << 9;
                const auto   found = std::lower_bound( listLookup.begin(), listLookup.end(), key );
                if ( found == listLookup.end() || ( *found >> 9 ) != ( key >> 9 ) )
                    continue;
                const int32 symbol = static_cast<int32>( *found & 0x1FFu );
                if ( symbol == HPACKHuffman::kEndOfString )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK Huffman string contains the end-of-string symbol" } );
                outText.push_back( static_cast<utf8>( symbol ) );
                code     = 0;
                bitCount = 0;
                bAllOnes = true;
            }
        }
        if ( 7 < bitCount || bAllOnes == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK Huffman string has invalid padding" } );
        return {};
    }

    expected<void> HPACKDecoder::readString( const uint8*& pCursor, const uint8* pEnd, string& outText ) const
    {
        if ( pCursor >= pEnd )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK string is cut off" } );
        const bool             bHuffman = ( *pCursor & 0x80u ) != 0;
        const expected<uint32> length   = readInteger( pCursor, pEnd, 7 );
        if ( length.has_value() == false )
            return unexpected( length.error() );
        if ( *length > static_cast<size_t>( pEnd - pCursor ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK string is longer than the block" } );
        if ( bHuffman )
        {
            expected<void> decoded = decodeHuffman( pCursor, *length, outText );
            pCursor += *length;
            return decoded;
        }
        outText.assign( reinterpret_cast<const utf8*>( pCursor ), *length );
        pCursor += *length;
        return {};
    }

    expected<void> HPACKDecoder::findEntry( uint32 index, HTTPHeader& outHeader ) const
    {
        using Internal = HPACKInternal;
        if ( index == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK index 0 is not valid" } );
        if ( index <= Internal::kStaticCount )
        {
            outHeader._name  = HPACKStatic::kArrEntry[index - 1]._pName;
            outHeader._value = HPACKStatic::kArrEntry[index - 1]._pValue;
            return {};
        }
        const uint32 dynamicIndex = index - Internal::kStaticCount - 1;
        if ( dynamicIndex >= _listDynamic.size() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK index is outside the tables" } );
        outHeader = _listDynamic[dynamicIndex];
        return {};
    }

    void HPACKDecoder::shrinkTo( uint32 size )
    {
        while ( _dynamicSize > size && _listDynamic.empty() == false )
        {
            _dynamicSize -= HPACKInternal::entrySize( _listDynamic.back() );
            _listDynamic.pop_back();
        }
    }

    void HPACKDecoder::insert( const HTTPHeader& header )
    {
        const uint32 size = HPACKInternal::entrySize( header );
        shrinkTo( size > _currentLimit ? 0 : _currentLimit - size ); // 표보다 큰 항목은 표를 비우고 들어가지 않는다 (RFC 7541 4.4)
        if ( size > _currentLimit )
            return;
        _listDynamic.push_front( header );
        _dynamicSize += size;
    }

    expected<void> HPACKDecoder::decode( const uint8* pBlock, size_t size, vector<HTTPHeader>& outListHeader )
    {
        outListHeader.clear();
        expected<void> decoded = decodeHeaders( pBlock, size, outListHeader );
        if ( decoded.has_value() == false )
            outListHeader.clear();
        return decoded;
    }

    expected<void> HPACKDecoder::decodeHeaders( const uint8* pBlock, size_t size, vector<HTTPHeader>& outListHeader )
    {
        const uint8* pCursor   = pBlock;
        const uint8* pEnd      = pBlock + size;
        uint64       listBytes = 0; // 푼 목록의 크기(항목마다 이름 + 값 + 32)
        while ( pCursor < pEnd )
        {
            const uint8 first = *pCursor;
            HTTPHeader  header;
            if ( ( first & 0x80u ) != 0 ) // 색인된 헤더
            {
                const expected<uint32> index = readInteger( pCursor, pEnd, 7 );
                expected<void>         found = index.has_value() ? findEntry( *index, header ) : expected<void>{ unexpected( index.error() ) };
                if ( found.has_value() == false )
                    return found;
                listBytes += HPACKInternal::entrySize( header );
                if ( listBytes > _maxHeaderListBytes )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK header list is larger than the limit" } );
                outListHeader.push_back( std::move( header ) );
                continue;
            }
            if ( ( first & 0xE0u ) == 0x20u ) // 동적 표 크기 갱신
            {
                const expected<uint32> newLimit = readInteger( pCursor, pEnd, 5 );
                if ( newLimit.has_value() == false )
                    return unexpected( newLimit.error() );
                if ( *newLimit > _maxLimit )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK table size update is above the limit we announced" } );
                _currentLimit = *newLimit;
                shrinkTo( *newLimit );
                continue;
            }
            const bool             bIncremental = ( first & 0xC0u ) == 0x40u;
            const int32            prefixBits   = bIncremental ? 6 : 4;
            const expected<uint32> nameIndex    = readInteger( pCursor, pEnd, prefixBits );
            if ( nameIndex.has_value() == false )
                return unexpected( nameIndex.error() );
            expected<void> read = *nameIndex == 0 ? readString( pCursor, pEnd, header._name ) : findEntry( *nameIndex, header );
            if ( read.has_value() )
                read = readString( pCursor, pEnd, header._value );
            if ( read.has_value() == false )
                return read;
            listBytes += HPACKInternal::entrySize( header );
            if ( listBytes > _maxHeaderListBytes )
                return unexpected( Error{ ErrorKind::InvalidArgument, "HPACK header list is larger than the limit" } );
            if ( bIncremental )
                insert( header );
            outListHeader.push_back( std::move( header ) );
        }
        return {};
    }
} // namespace sw
