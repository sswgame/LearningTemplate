#include "pch.h"

#include "Editor/Common/Asset/Texture/TextureBlockEncoder.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw::editor
{
    namespace
    {
        struct TextureBlockEncoderInternal
        {
            /** @brief 4x4 블록의 RGBA 입니다. 줄 우선(y * 4 + x)입니다. */
            struct RGBABlock
            {
                uint8 _arrPixel[16][4];

                /** @brief 축 위 두 끝 화소를 끝점으로 삼고 화소마다 가장 가까운 가중치를 고릅니다. 끝점 합이 줄면 디코더가 파랑 수축을 하므로 뒤집어 항상 직접 모드로 둔다. */
                void encodeASTC( uint8 ( &arrOut )[16] ) const
                {
                    int32 arrMin[4] = { 255, 255, 255, 255 };
                    int32 arrMax[4] = { 0, 0, 0, 0 };
                    for ( const uint8( &pixel )[4] : _arrPixel )
                    {
                        for ( uint32 channel = 0; channel < 4; ++channel )
                        {
                            arrMin[channel] = MathUtil::min( arrMin[channel], static_cast<int32>( pixel[channel] ) );
                            arrMax[channel] = MathUtil::max( arrMax[channel], static_cast<int32>( pixel[channel] ) );
                        }
                    }
                    const int32 arrAxis[4] = { arrMax[0] - arrMin[0], arrMax[1] - arrMin[1], arrMax[2] - arrMin[2], arrMax[3] - arrMin[3] };
                    uint32      lowIndex   = 0;
                    uint32      highIndex  = 0;
                    int32       lowDot     = INT32_MAX;
                    int32       highDot    = INT32_MIN;
                    for ( uint32 index = 0; index < 16; ++index )
                    {
                        const uint8* pPixel = _arrPixel[index];
                        const int32  dot    = pPixel[0] * arrAxis[0] + pPixel[1] * arrAxis[1] + pPixel[2] * arrAxis[2] + pPixel[3] * arrAxis[3];
                        if ( dot < lowDot )
                        {
                            lowDot   = dot;
                            lowIndex = index;
                        }
                        if ( dot > highDot )
                        {
                            highDot   = dot;
                            highIndex = index;
                        }
                    }
                    const uint8* pLow    = _arrPixel[lowIndex];
                    const uint8* pHigh   = _arrPixel[highIndex];
                    const int32  lowSum  = pLow[0] + pLow[1] + pLow[2];
                    const int32  highSum = pHigh[0] + pHigh[1] + pHigh[2];
                    const uint8* pFirst  = highSum >= lowSum ? pLow : pHigh;
                    const uint8* pSecond = highSum >= lowSum ? pHigh : pLow;
                    for ( uint8& byte : arrOut )
                    {
                        byte = 0;
                    }
                    setBits( arrOut, 0, 11, kASTCBlockMode );
                    setBits( arrOut, 11, 2, 0 ); // 분할 수 - 1
                    setBits( arrOut, 13, 4, kASTCEndpointModeRGBA );
                    for ( uint32 channel = 0; channel < 4; ++channel )
                    {
                        setBits( arrOut, 17 + channel * 16, 8, pFirst[channel] ); // 값 순서: r0 r1 g0 g1 b0 b1 a0 a1
                        setBits( arrOut, 25 + channel * 16, 8, pSecond[channel] );
                    }
                    for ( uint32 index = 0; index < 16; ++index )
                    {
                        uint32 bestWeight = 0;
                        int32  bestError  = INT32_MAX;
                        for ( uint32 weight = 0; weight < 4; ++weight )
                        {
                            int32 error = 0;
                            for ( uint32 channel = 0; channel < 4; ++channel )
                            {
                                const int32 decoded = ( pFirst[channel] * ( 64 - kArrASTCWeight[weight] ) + pSecond[channel] * kArrASTCWeight[weight] + 32 ) / 64;
                                const int32 delta   = decoded - _arrPixel[index][channel];
                                error += delta * delta;
                            }
                            if ( error < bestError )
                            {
                                bestError  = error;
                                bestWeight = weight;
                            }
                        }
                        // 가중치는 블록 맨 위 비트부터 거꾸로 쌓는다(가중치 0 의 LSB 가 비트 127).
                        setBits( arrOut, 127 - index * 2, 1, bestWeight & 1u );
                        setBits( arrOut, 126 - index * 2, 1, bestWeight >> 1 );
                    }
                }

                /** @brief 2x4 칸 하나(x 가 @p firstX, @p firstX + 1)의 기본색(4 비트)과 표를 고르고 화소 값을 평면 둘에 씁니다. */
                void encodeETCSubBlock( uint32 firstX, uint32 ( &arrOutBase )[3], uint32& outTable, uint32& ioMSB, uint32& ioLSB ) const
                {
                    int32 arrSum[3] = { 0, 0, 0 };
                    for ( uint32 y = 0; y < 4; ++y )
                    {
                        for ( uint32 x = firstX; x < firstX + 2; ++x )
                        {
                            for ( uint32 channel = 0; channel < 3; ++channel )
                            {
                                arrSum[channel] += _arrPixel[y * 4 + x][channel];
                            }
                        }
                    }
                    int32 arrBase8[3] = {};
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        arrOutBase[channel] = static_cast<uint32>( ( arrSum[channel] / 8 * 15 + 127 ) / 255 ); // 4 비트로
                        arrBase8[channel]   = static_cast<int32>( arrOutBase[channel] * 17 );                  // 디코더가 펼치는 값
                    }
                    int32 bestTotal = INT32_MAX;
                    for ( uint32 table = 0; table < 8; ++table )
                    {
                        int32  total = 0;
                        uint32 msb   = 0;
                        uint32 lsb   = 0;
                        for ( uint32 y = 0; y < 4; ++y )
                        {
                            for ( uint32 x = firstX; x < firstX + 2; ++x )
                            {
                                const int32 arrModifier[4] = { kArrETCModifier[table][0], kArrETCModifier[table][1], -kArrETCModifier[table][0], -kArrETCModifier[table][1] };
                                int32       bestError      = INT32_MAX;
                                uint32      bestIndex      = 0;
                                for ( uint32 index = 0; index < 4; ++index )
                                {
                                    int32 error = 0;
                                    for ( uint32 channel = 0; channel < 3; ++channel )
                                    {
                                        const int32 decoded = MathUtil::clamp( arrBase8[channel] + arrModifier[index], 0, 255 );
                                        const int32 delta   = decoded - _arrPixel[y * 4 + x][channel];
                                        error += delta * delta;
                                    }
                                    if ( error < bestError )
                                    {
                                        bestError = error;
                                        bestIndex = index;
                                    }
                                }
                                total += bestError;
                                const uint32 bit = x * 4 + y; // 화소 번호는 열 우선이다
                                msb |= ( bestIndex >> 1 ) << bit;
                                lsb |= ( bestIndex & 1u ) << bit;
                            }
                        }
                        if ( total < bestTotal )
                        {
                            bestTotal = total;
                            outTable  = table;
                            ioMSB     = ( ioMSB & ~( 0xFFu << ( firstX * 4 ) ) ) | msb;
                            ioLSB     = ( ioLSB & ~( 0xFFu << ( firstX * 4 ) ) ) | lsb;
                        }
                    }
                }

                /** @brief ETC2 색 블록 8 바이트입니다(개별 모드, 뒤집기 없음). */
                void encodeETC2Color( uint8* pOut ) const
                {
                    uint32 arrBase0[3] = {};
                    uint32 arrBase1[3] = {};
                    uint32 table0      = 0;
                    uint32 table1      = 0;
                    uint32 msb         = 0;
                    uint32 lsb         = 0;
                    encodeETCSubBlock( 0, arrBase0, table0, msb, lsb );
                    encodeETCSubBlock( 2, arrBase1, table1, msb, lsb );
                    pOut[0] = static_cast<uint8>( ( arrBase0[0] << 4 ) | arrBase1[0] );
                    pOut[1] = static_cast<uint8>( ( arrBase0[1] << 4 ) | arrBase1[1] );
                    pOut[2] = static_cast<uint8>( ( arrBase0[2] << 4 ) | arrBase1[2] );
                    pOut[3] = static_cast<uint8>( ( table0 << 5 ) | ( table1 << 2 ) ); // diff 0, flip 0
                    pOut[4] = static_cast<uint8>( msb >> 8 );
                    pOut[5] = static_cast<uint8>( msb );
                    pOut[6] = static_cast<uint8>( lsb >> 8 );
                    pOut[7] = static_cast<uint8>( lsb );
                }

                /** @brief EAC 한 채널 블록 8 바이트입니다. 기본값은 범위 가운데, 배수는 표마다 범위에서 정하고 가장 나은 표를 고른다. */
                void encodeEAC( uint32 channel, uint8* pOut ) const
                {
                    int32 minValue = 255;
                    int32 maxValue = 0;
                    for ( const uint8( &pixel )[4] : _arrPixel )
                    {
                        minValue = MathUtil::min( minValue, static_cast<int32>( pixel[channel] ) );
                        maxValue = MathUtil::max( maxValue, static_cast<int32>( pixel[channel] ) );
                    }
                    const int32 base      = ( minValue + maxValue + 1 ) / 2;
                    int32       bestTotal = INT32_MAX;
                    uint64      bestBits  = 0;
                    uint32      bestTable = 0;
                    int32       bestMult  = 1;
                    for ( uint32 table = 0; table < 16; ++table )
                    {
                        const int32 span       = kArrEACModifier[table][7] - kArrEACModifier[table][3];
                        const int32 multiplier = MathUtil::clamp( ( maxValue - minValue + span / 2 ) / span, 1, 15 );
                        int32       total      = 0;
                        uint64      bits       = 0;
                        for ( uint32 x = 0; x < 4; ++x )
                        {
                            for ( uint32 y = 0; y < 4; ++y )
                            {
                                const int32 value     = _arrPixel[y * 4 + x][channel];
                                int32       bestError = INT32_MAX;
                                uint32      bestIndex = 0;
                                for ( uint32 index = 0; index < 8; ++index )
                                {
                                    const int32 delta = MathUtil::clamp( base + kArrEACModifier[table][index] * multiplier, 0, 255 ) - value;
                                    if ( delta * delta < bestError )
                                    {
                                        bestError = delta * delta;
                                        bestIndex = index;
                                    }
                                }
                                total += bestError;
                                bits = ( bits << 3 ) | bestIndex; // 열 우선, 맨 위 비트가 (0, 0) 이다
                            }
                        }
                        if ( total < bestTotal )
                        {
                            bestTotal = total;
                            bestBits  = bits;
                            bestTable = table;
                            bestMult  = multiplier;
                        }
                    }
                    pOut[0] = static_cast<uint8>( base );
                    pOut[1] = static_cast<uint8>( ( static_cast<uint32>( bestMult ) << 4 ) | bestTable );
                    for ( uint32 index = 0; index < 6; ++index )
                    {
                        pOut[2 + index] = static_cast<uint8>( bestBits >> ( ( 5 - index ) * 8 ) );
                    }
                }
            };

            // ---- ASTC 4x4: 분할 없음, 끝점 형식 12(LDR RGBA 직접), 끝점 8 비트, 가중치 4x4 × 2 비트 ----

            /** @brief 블록 모드입니다(명세의 `D H B B A A R0 0 0 R2 R1` — 너비 B+4 = 4, 높이 A+2 = 4, 가중치 범위 R=100b 는 0..3). */
            static constexpr uint32 kASTCBlockMode = 0x042;
            /** @brief 끝점 형식 12: LDR RGBA 직접(값 8 개, 부족한 비트 없음 — 79 비트 안에 8 비트 × 8). */
            static constexpr uint32 kASTCEndpointModeRGBA = 12;
            /** @brief 2 비트 가중치를 푼 값입니다(명세의 범위 0..3 표). */
            static constexpr int32 kArrASTCWeight[4] = { 0, 21, 43, 64 };

            static void setBits( uint8 ( &arrBlock )[16], uint32 bitOffset, uint32 bitCount, uint32 value )
            {
                for ( uint32 index = 0; index < bitCount; ++index )
                {
                    const uint32 bit = bitOffset + index;
                    if ( ( ( value >> index ) & 1u ) != 0 )
                        arrBlock[bit / 8] = static_cast<uint8>( arrBlock[bit / 8] | ( 1u << ( bit % 8 ) ) );
                }
            }

            // ---- ETC2 색(개별 모드 — ETC1 과 같은 비트), EAC 한 채널 ----

            /** @brief ETC 수식 표입니다(명세). 화소 값 00 → +a, 01 → +b, 10 → -a, 11 → -b. */
            static constexpr int32 kArrETCModifier[8][2] = {
                { 2,   8},
                { 5,  17},
                { 9,  29},
                {13,  42},
                {18,  60},
                {24,  80},
                {33, 106},
                {47, 183}
            };
            /** @brief EAC 수식 표입니다(명세 — 기억에 의존해 옮겼고 테스트의 디코더가 같은 표를 쓴다: 실제 장치 대조는 통합 뒤 lavapipe 로). */
            static constexpr int32 kArrEACModifier[16][8] = {
                {-3, -6,  -9, -15, 2, 5, 8, 14},
                {-3, -7, -10, -13, 2, 6, 9, 12},
                {-2, -5,  -8, -13, 1, 4, 7, 12},
                {-2, -4,  -6, -13, 1, 3, 5, 12},
                {-3, -6,  -8, -12, 2, 5, 7, 11},
                {-3, -7,  -9, -11, 2, 6, 8, 10},
                {-4, -7,  -8, -11, 3, 6, 7, 10},
                {-3, -5,  -8, -11, 2, 4, 7, 10},
                {-2, -6,  -8, -10, 1, 5, 7,  9},
                {-2, -5,  -8, -10, 1, 4, 7,  9},
                {-2, -4,  -8, -10, 1, 3, 7,  9},
                {-2, -5,  -7, -10, 1, 4, 6,  9},
                {-3, -4,  -7, -10, 2, 3, 6,  9},
                {-1, -2,  -3, -10, 0, 1, 2,  9},
                {-4, -6,  -8,  -9, 3, 5, 7,  8},
                {-3, -5,  -7,  -9, 2, 4, 6,  8},
            };
        };
    } // namespace

    bool TextureBlockEncoder::isEncodable( RHIFormat format )
    {
        return format == RHIFormat::ASTC_4x4_UNORM || format == RHIFormat::ETC2_RGBA8_UNORM || format == RHIFormat::EAC_R11_UNORM || format == RHIFormat::EAC_RG11_UNORM;
    }

    bool TextureBlockEncoder::encodeMip( RHIFormat format, const uint8* pRGBA, uint32 width, uint32 height, vector<uint8>& ioBytes )
    {
        using Internal = TextureBlockEncoderInternal;
        if ( isEncodable( format ) == false )
        {
            SW_LOG_ERROR_ID( "TextureBlockEncoder.UnknownFormat", "TextureBlockEncoder cannot encode RHIFormat %#", static_cast<uint32>( format ) );
            return false;
        }
        const uint32 blockBytes = getRHIFormatBlockInfo( format )._blockBytes;
        const uint32 blocksX    = ( width + 3 ) / 4;
        const uint32 blocksY    = ( height + 3 ) / 4;
        size_t       offset     = ioBytes.size();
        ioBytes.resize( offset + static_cast<size_t>( blocksX ) * blocksY * blockBytes );
        for ( uint32 blockY = 0; blockY < blocksY; ++blockY )
        {
            for ( uint32 blockX = 0; blockX < blocksX; ++blockX )
            {
                Internal::RGBABlock block{};
                for ( uint32 y = 0; y < 4; ++y )
                {
                    for ( uint32 x = 0; x < 4; ++x )
                    {
                        const uint32 sourceX = MathUtil::min( blockX * 4 + x, width - 1 );
                        const uint32 sourceY = MathUtil::min( blockY * 4 + y, height - 1 );
                        const uint8* pSource = pRGBA + ( static_cast<size_t>( sourceY ) * width + sourceX ) * 4;
                        for ( uint32 channel = 0; channel < 4; ++channel )
                        {
                            block._arrPixel[y * 4 + x][channel] = pSource[channel];
                        }
                    }
                }
                uint8* pOut = ioBytes.data() + offset;
                if ( format == RHIFormat::ASTC_4x4_UNORM )
                {
                    uint8 arrASTC[16];
                    block.encodeASTC( arrASTC );
                    for ( uint32 index = 0; index < 16; ++index )
                    {
                        pOut[index] = arrASTC[index];
                    }
                }
                else if ( format == RHIFormat::ETC2_RGBA8_UNORM )
                {
                    block.encodeEAC( 3, pOut ); // EAC 알파가 앞이다
                    block.encodeETC2Color( pOut + 8 );
                }
                else
                {
                    block.encodeEAC( 0, pOut );
                    if ( format == RHIFormat::EAC_RG11_UNORM )
                        block.encodeEAC( 1, pOut + 8 );
                }
                offset += blockBytes;
            }
        }
        return true;
    }
} // namespace sw::editor
