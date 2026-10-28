#include "pch.h"

#include "Engine/Audio/DSP/AudioFFT.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AudioFFT::AudioFFT()
        : _listTwiddle{}
        , _listBitReverse{}
        , _size{ 0 }
    {
    }

    bool AudioFFT::initialize( uint32 size )
    {
        const bool bPowerOfTwo = size >= 2 && ( size & ( size - 1 ) ) == 0;
        if ( bPowerOfTwo == false )
            return false;
        _size = size;
        _listTwiddle.resize( size / 2 );
        for ( uint32 index = 0; index < size / 2; ++index )
        {
            const float64 angle            = -2.0 * MathUtil::kPi64 * static_cast<float64>( index ) / static_cast<float64>( size );
            _listTwiddle[index]._real      = static_cast<float32>( MathUtil::cos( angle ) );
            _listTwiddle[index]._imaginary = static_cast<float32>( MathUtil::sin( angle ) );
        }
        uint32 bitCount = 0;
        while ( ( 1u << bitCount ) < size )
        {
            ++bitCount;
        }
        _listBitReverse.resize( size );
        for ( uint32 index = 0; index < size; ++index )
        {
            uint32 reversed = 0;
            for ( uint32 bit = 0; bit < bitCount; ++bit )
            {
                reversed |= ( ( index >> bit ) & 1u ) << ( bitCount - 1 - bit );
            }
            _listBitReverse[index] = reversed;
        }
        return true;
    }

    void AudioFFT::forward( AudioComplex* pData ) const
    {
        transform( pData, false );
    }

    void AudioFFT::inverse( AudioComplex* pData ) const
    {
        transform( pData, true );
        const float32 scale = 1.0f / static_cast<float32>( _size );
        for ( uint32 index = 0; index < _size; ++index )
        {
            pData[index]._real *= scale;
            pData[index]._imaginary *= scale;
        }
    }

    void AudioFFT::transform( AudioComplex* pData, bool bInverse ) const
    {
        SW_ASSERT( _size > 0 );
        for ( uint32 index = 0; index < _size; ++index )
        {
            const uint32 reversed = _listBitReverse[index];
            if ( index < reversed )
            {
                const AudioComplex swapped = pData[index];
                pData[index]               = pData[reversed];
                pData[reversed]            = swapped;
            }
        }
        const float32 conjugate = bInverse ? -1.0f : 1.0f;
        for ( uint32 half = 1; half < _size; half *= 2 )
        {
            const uint32 twiddleStride = _size / ( half * 2 );
            for ( uint32 start = 0; start < _size; start += half * 2 )
            {
                for ( uint32 offset = 0; offset < half; ++offset )
                {
                    const AudioComplex& twiddle = _listTwiddle[offset * twiddleStride];
                    const float32       wReal   = twiddle._real;
                    const float32       wImag   = twiddle._imaginary * conjugate;
                    AudioComplex&       even    = pData[start + offset];
                    AudioComplex&       odd     = pData[start + offset + half];
                    const float32       tReal   = odd._real * wReal - odd._imaginary * wImag;
                    const float32       tImag   = odd._real * wImag + odd._imaginary * wReal;
                    odd._real                   = even._real - tReal;
                    odd._imaginary              = even._imaginary - tImag;
                    even._real += tReal;
                    even._imaginary += tImag;
                }
            }
        }
    }
} // namespace sw
