// AudioConvolutionTest — 컨볼루션 리버브가 쓰는 FFT 의 왕복과 임펄스 스펙트럼(임펄스 응답 파일을 읽는 리버브 케이스는 남긴다).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/DSP/AudioFFT.h"

/**
 * @brief [AudioConvolutionTest] FFT 를 갔다 오면 원래 값이고, 임펄스(첫 샘플 1)의 스펙트럼은 모든 빈이 1 이다
 */
SW_TEST_CASE( AudioConvolutionTest, FFTRoundTripsAndTransformsAnImpulse )
{
    sw::AudioFFT fft;
    SW_REQUIRE_TRUE( fft.initialize( 64 ) );
    SW_EXPECT_FALSE( fft.initialize( 48 ) ); // 2 의 거듭제곱이 아니다
    SW_REQUIRE_TRUE( fft.initialize( 64 ) );

    sw::AudioComplex arrData[64]{};
    sw::AudioComplex arrOriginal[64]{};
    for ( uint32 index = 0; index < 64; ++index )
    {
        arrData[index]._real = sw::MathUtil::sin( static_cast<float32>( index ) * 0.37f ) + 0.25f * static_cast<float32>( index % 5 );
        arrOriginal[index]   = arrData[index];
    }
    fft.forward( arrData );
    fft.inverse( arrData );
    for ( uint32 index = 0; index < 64; ++index )
    {
        SW_EXPECT_NEAR( arrOriginal[index]._real, arrData[index]._real, 1e-4f );
        SW_EXPECT_NEAR( 0.0f, arrData[index]._imaginary, 1e-4f );
    }

    sw::AudioComplex arrImpulse[64]{};
    arrImpulse[0]._real = 1.0f;
    fft.forward( arrImpulse );
    for ( const sw::AudioComplex& bin : arrImpulse )
    {
        SW_EXPECT_NEAR( 1.0f, bin._real, 1e-5f );
        SW_EXPECT_NEAR( 0.0f, bin._imaginary, 1e-5f );
    }
}
