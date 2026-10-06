#include "pch.h"

#include "Core/String/Base64Util.h"

namespace sw
{
    namespace
    {
        struct Base64UtilInternal
        {
            static constexpr utf8  kStandardAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            static constexpr utf8  kUrlAlphabet[]      = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            static constexpr int32 kInvalidDigit       = -1;

            static string encode( const uint8* pData, size_t size, const utf8* pAlphabet, bool bPad )
            {
                string text;
                text.reserve( ( size + 2 ) / 3 * 4 );
                uint32 buffer           = 0;
                int32  bufferedBitCount = 0;
                for ( size_t index = 0; index < size; ++index )
                {
                    buffer = ( buffer << 8 ) | pData[index];
                    bufferedBitCount += 8;
                    while ( bufferedBitCount >= 6 )
                    {
                        bufferedBitCount -= 6;
                        text.push_back( pAlphabet[( buffer >> bufferedBitCount ) & 0x3Fu] );
                    }
                }
                if ( bufferedBitCount > 0 )
                    text.push_back( pAlphabet[( buffer << ( 6 - bufferedBitCount ) ) & 0x3Fu] );
                while ( bPad && text.size() % 4 != 0 )
                    text.push_back( '=' );
                return text;
            }

            static int32 findDigit( utf8 character, const utf8* pAlphabet )
            {
                for ( int32 digit = 0; digit < 64; ++digit )
                {
                    if ( pAlphabet[digit] == character )
                        return digit;
                }
                return kInvalidDigit;
            }

            static bool decode( string_view text, const utf8* pAlphabet, vector<uint8>& outBytes )
            {
                outBytes.clear();
                while ( text.empty() == false && text.back() == '=' )
                    text.remove_suffix( 1 );
                if ( text.size() % 4 == 1 )
                    return false; // 6 비트 하나로는 바이트가 되지 않는다
                outBytes.reserve( text.size() * 3 / 4 );
                uint32 buffer           = 0;
                int32  bufferedBitCount = 0;
                for ( const utf8 character : text )
                {
                    const int32 digit = findDigit( character, pAlphabet );
                    if ( digit == kInvalidDigit )
                        return false;
                    buffer = ( buffer << 6 ) | static_cast<uint32>( digit );
                    bufferedBitCount += 6;
                    if ( bufferedBitCount >= 8 )
                    {
                        bufferedBitCount -= 8;
                        outBytes.push_back( static_cast<uint8>( ( buffer >> bufferedBitCount ) & 0xFFu ) );
                    }
                }
                const uint32 leftoverMask = ( 1u << bufferedBitCount ) - 1u;
                return ( buffer & leftoverMask ) == 0; // 끝에 남는 비트는 0 이어야 한다(한 바이트 열에 글 하나 — 변조 막기)
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string Base64Util::encode( const uint8* pData, size_t size ) { return Base64UtilInternal::encode( pData, size, Base64UtilInternal::kStandardAlphabet, true ); }

    string Base64Util::encodeUrl( const uint8* pData, size_t size ) { return Base64UtilInternal::encode( pData, size, Base64UtilInternal::kUrlAlphabet, false ); }

    bool Base64Util::decode( string_view text, vector<uint8>& outBytes ) { return Base64UtilInternal::decode( text, Base64UtilInternal::kStandardAlphabet, outBytes ); }

    bool Base64Util::decodeUrl( string_view text, vector<uint8>& outBytes ) { return Base64UtilInternal::decode( text, Base64UtilInternal::kUrlAlphabet, outBytes ); }
} // namespace sw
