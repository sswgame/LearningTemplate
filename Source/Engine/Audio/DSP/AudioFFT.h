/**
 * @file AudioFFT.h
 * @brief 오디오 DSP 용 복소수 FFT(반복형 radix-2, 크기는 2 의 거듭제곱)입니다. 컨볼루션 리버브가 씁니다.
 * @details 비트 뒤집기 표와 회전 인자(twiddle)를 `initialize` 때 한 번 만들고, 변환은 제자리에서 할당 없이 한다(오디오 스레드).
 *          역변환은 1/N 로 나눠 forward → inverse 가 원래 값으로 돌아온다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 복소수 하나입니다. */
    struct AudioComplex
    {
        float32 _real{ 0.0f };
        float32 _imaginary{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioFFT
     * @brief 크기 하나의 FFT 표입니다.
     */
    class SW_API AudioFFT
    {
    public:
        AudioFFT();

        /** @brief 크기(2 의 거듭제곱, 2 이상)로 표를 만듭니다. 오디오 스레드 밖에서 부릅니다. 2 의 거듭제곱이 아니면 false 입니다. */
        [[nodiscard]] bool initialize( uint32 size );

        /** @brief @p pData(크기만큼)를 제자리에서 주파수 영역으로 바꿉니다. */
        void forward( AudioComplex* pData ) const;
        /** @brief @p pData 를 제자리에서 시간 영역으로 되돌립니다(1/N 로 나눕니다). */
        void inverse( AudioComplex* pData ) const;

        /** @brief 크기입니다. 만들기 전에는 0 입니다. */
        uint32 getSize() const { return _size; }

    private:
        /** @brief 비트 뒤집기 뒤 나비 연산을 합니다. @p bInverse 면 회전 인자의 켤레를 씁니다. */
        void transform( AudioComplex* pData, bool bInverse ) const;

    private:
        vector<AudioComplex> _listTwiddle;    /**< e^(−2πik/N), k < N/2 입니다. */
        vector<uint32>       _listBitReverse; /**< 자리마다 비트를 뒤집은 자리입니다. */
        uint32               _size;           /**< 변환 크기입니다. */
    };
} // namespace sw
