/**
 * @file AudioRenderThread.h
 * @brief 오디오 엔진을 장치 콜백 밖에서 앞서 렌더해 링 버퍼에 채우는 스레드입니다. 장치 콜백(XAudio2 · AAudio)은 링에서 꺼내기만 합니다.
 * @details 언리얼 `IAudioMixerPlatformInterface` 의 `AudioRenderThread`(콜백은 준비된 버퍼를 건네고 신호만 보낸다) · FMOD 믹서 스레드와 같은 배치다. 예전에는 두 장치가 콜백 안에서
 *          믹스해, 믹스 길의 할당(보이스의 스트림 · 소리 그래프 열기) · 잠금(클립 캐시 · 게시 상태) · 늦은 블록 하나가 모두 장치의 실시간 기한 안에 있었다 — 게임 스레드가 쥔
 *          잠금을 실시간 스레드가 기다리면 우선순위 역전으로 끊긴다. 이제 기한이 있는 것은 링 꺼내기(원자 두 개와 복사)뿐이고, 믹스가 한 번 늦어도 링이 받는다.
 *          ALSA · 웹 백엔드는 처음부터 자기 스레드에서 렌더한다(장치가 콜백이 아니라 밀어 넣기다).
 */
#pragma once
#include "Core/Concurrency/RealtimeSection.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/CircularAudioBuffer.h"

namespace sw
{
    class AudioEngine;

    /**
     * @class AudioRenderThread
     * @brief 렌더 스레드와 그 링 하나입니다. 장치 백엔드가 들고, 장치 콜백이 도는 동안 켜 둡니다.
     */
    class SW_API AudioRenderThread
    {
    public:
        /** @brief 한 번에 렌더해 넣는 프레임 수입니다(믹서 블록 하나, 5.3 ms). */
        static constexpr uint32 kRenderFrameCount = 256;
        /**
         * @brief 장치가 한꺼번에 당길 수 있는 프레임 위에 더 채워 두는 프레임 수입니다(10.7 ms) — 믹스가 늦을 때 받아 내는 여유이자 더해지는 지연이다(언리얼 `NumBuffers` 자리).
         * @details 블록 하나보다 커야 한다: 렌더 스레드는 자리가 블록보다 적으면 쉬므로 차 있는 양은 목표보다 최대 블록 하나 − 1 만큼 적다.
         */
        static constexpr uint32 kSpareFrameCount = 2 * kRenderFrameCount;

        explicit AudioRenderThread( AudioEngine& engine );
        /** @brief 멈추고 합류합니다. */
        ~AudioRenderThread();

        AudioRenderThread( const AudioRenderThread& )            = delete;
        AudioRenderThread& operator=( const AudioRenderThread& ) = delete;

        /**
         * @brief 링을 채우고(부르는 스레드에서 렌더한다) 렌더 스레드를 띄웁니다. 장치 콜백을 시작하기 전에 부릅니다.
         * @param maxReadFrameCount 장치가 렌더 스레드에 틈을 주지 않고 잇달아 당길 수 있는 프레임 수입니다(한 콜백이 크거나 버스트 하나에 콜백이 여럿). 링은 이것 + `kSpareFrameCount` 를 채운다.
         */
        void start( uint32 channelCount, uint32 maxReadFrameCount );
        /** @brief 렌더 스레드를 멈추고 합류합니다. 장치 콜백이 더 오지 않게 한 뒤에 부릅니다. */
        void stop();
        /** @brief (장치 콜백) @p frameCount 프레임을 꺼냅니다. 모자라면 나머지를 0 으로 채우고 언더런을 셉니다. 기다리지 않습니다. */
        void read( float32* pOutput, uint32 frameCount );
        /** @brief 켠 뒤로 링이 모자랐던 콜백 수입니다. 아무 스레드나 읽습니다. */
        uint64 getUnderrunCount() const { return _underrunCount.load( std::memory_order_relaxed ); }

    private:
        void run( const StopToken& stopToken );
        /** @brief 블록 하나를 렌더해 링에 넣습니다. 자리가 있을 때만 부릅니다. */
        void renderNext();
        /** @brief 블록 하나를 더 넣어도 목표를 넘지 않으면 true 입니다. 링 용량(2 의 거듭제곱으로 올린 값)까지 채우지 않는다 — 올린 만큼 지연이 는다. */
        bool hasRoom() const { return _pRing->getNum() + static_cast<uint32>( _listRender.size() ) <= _targetSampleCount; }

    private:
        AudioEngine&                    _engine;
        unique_ptr<CircularAudioBuffer> _pRing;              ///< 렌더 스레드 → 장치 콜백(인터리브)
        vector<float32>                 _listRender;         ///< 렌더한 블록 하나
        thread                          _thread;             ///< 렌더 스레드
        atomic<uint32>                  _wakeWord;           ///< 콜백이 꺼낼 때마다 오른다 — 자리를 기다리는 렌더 스레드가 여기서 잠든다
        atomic<uint64>                  _underrunCount;      ///< 링이 모자랐던 콜백 수
        uint32                          _channelCount;       ///< 스트림의 채널 수
        uint32                          _targetSampleCount;  ///< 채워 두는 샘플 수(잇달아 당길 수 있는 양 + 여유)
        RealtimeViolationCounters       _realtimeViolations; ///< 콜백 구간의 할당 · 해제 · 잠금(검사 구성) — 하나라도 있으면 단언한다
    };
} // namespace sw
