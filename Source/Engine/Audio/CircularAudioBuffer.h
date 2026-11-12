/**
 * @file CircularAudioBuffer.h
 * @brief 단일 생산자 · 단일 소비자(SPSC) 오디오 샘플 링 버퍼입니다. 생산자(믹서 스레드)가 인터리브 샘플을 한 묶음씩 넣고, 소비자(장치 쪽)가 꺼냅니다.
 * @details 언리얼 `Audio::TCircularAudioBuffer`(SignalProcessing — SPSC, Push · Pop · Num · Remainder)와 같은 자리다. 플랫폼을 모르는 순수 코드다.
 *          읽기 · 쓰기 위치는 늘기만 하는 `uint32` 이고 용량(2의 거듭제곱)으로 감싼다 — 위치가 2^32 에서 넘쳐도 차(`write - read`)와 가림(`& mask`)이 그대로 맞는다.
 *          웹 백엔드는 소비자가 C++ 가 아니다(AudioWorklet 의 JS — `Audio/Web/WebAudioBackend.js`). 그쪽이 같은 규칙으로 읽도록 위치 · 샘플의 주소를 내준다
 *          (`getReadIndexAddress` …). 위치를 바꾸면 그 JS 도 함께 바꾼다.
 *          `SpscQueue`(원소 하나씩 · 컴파일 때 용량)와 다르다 — 샘플을 묶음으로 옮기고, 용량을 채널 수로 실행 중에 정하며, 바깥 소비자에게 배치를 연다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/span.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class CircularAudioBuffer
     * @brief SPSC 샘플 링 버퍼입니다. `push` 는 생산자 스레드 하나, `pop` 은 소비자 스레드 하나에서만 부릅니다.
     */
    class SW_API CircularAudioBuffer
    {
    public:
        /** @brief 샘플 @p capacity 개 이상을 담는 버퍼를 만듭니다. 용량은 2의 거듭제곱으로 올립니다(0 은 오류로 멈춘다). */
        explicit CircularAudioBuffer( uint32 capacity );
        ~CircularAudioBuffer() = default;

        CircularAudioBuffer( const CircularAudioBuffer& )            = delete;
        CircularAudioBuffer& operator=( const CircularAudioBuffer& ) = delete;
        CircularAudioBuffer( CircularAudioBuffer&& )                 = delete;
        CircularAudioBuffer& operator=( CircularAudioBuffer&& )      = delete;

        /** @brief (생산자) 샘플을 넣습니다. 빈 자리만큼만 넣고 넣은 개수를 돌려줍니다(가득 차면 0). */
        uint32 push( vector_reference<const float32> listSample );
        /** @brief (소비자) 샘플을 꺼냅니다. 있는 만큼만 꺼내고 꺼낸 개수를 돌려줍니다(비면 0). */
        uint32 pop( vector_reference<float32> outListSample );

        /** @brief 꺼낼 수 있는 샘플 수입니다(언리얼 `Num`). 다른 쪽 스레드가 움직이는 중이면 그 순간의 값입니다. */
        uint32 getNum() const;
        /** @brief 넣을 수 있는 샘플 수입니다(언리얼 `Remainder`). */
        uint32 getRemainder() const { return _capacity - getNum(); }
        /** @brief 용량(샘플, 2의 거듭제곱)입니다. */
        uint32 getCapacity() const { return _capacity; }

        /** @brief (바깥 소비자) 읽기 위치의 주소입니다 — 소비자가 꺼낸 뒤 늘리고, 생산자는 여기서 자리가 나기를 기다린다(`memory.atomic.wait32`). */
        uint32* getReadIndexAddress() { return reinterpret_cast<uint32*>( &_readIndex ); }
        /** @brief (바깥 소비자) 쓰기 위치의 주소입니다 — 생산자가 샘플을 쓴 뒤 늘린다. */
        uint32* getWriteIndexAddress() { return reinterpret_cast<uint32*>( &_writeIndex ); }
        /** @brief (바깥 소비자) 샘플 배열의 첫 원소입니다(길이 `getCapacity`). */
        float32* getSampleData() { return _arrSample.get(); }

    private:
        // 위치는 늘기만 하는 원자 정수다. 바깥 소비자(JS `Atomics` 의 Int32Array)가 4 바이트 정수로 읽는다.
        static_assert( sizeof( atomic<uint32> ) == sizeof( uint32 ), "the outside consumer reads the indices as plain 32-bit words" );
        /** @brief 두 위치를 떼어 놓는 간격(바이트) — x64 · ARM64 · Wasm 엔진이 도는 CPU 의 캐시 라인입니다. */
        static constexpr size_t kIndexAlignment = 64;

        unique_ptr<float32[]> _arrSample; ///< 샘플 배열입니다(`_capacity` 개). 생산자와 소비자가 서로 다른 칸을 동시에 만지므로 레이스 탐지 컨테이너를 쓰지 않는다.
        uint32                _capacity;  ///< 용량(2의 거듭제곱)입니다.
        uint32                _mask;      ///< `_capacity - 1` 입니다.
        // 생산자와 소비자가 서로의 캐시 라인을 무효화하지 않도록 떨어뜨린다(`SpscQueue` 와 같다).
        alignas( kIndexAlignment ) atomic<uint32> _readIndex;  ///< 소비자가 다음에 읽을 위치입니다.
        alignas( kIndexAlignment ) atomic<uint32> _writeIndex; ///< 생산자가 다음에 쓸 위치입니다.
    };
} // namespace sw
