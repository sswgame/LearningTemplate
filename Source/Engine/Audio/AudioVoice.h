/**
 * @file AudioVoice.h
 * @brief 클립 하나를 버스 입력에 섞는 보이스입니다 — 재생 위치 · 피치 리샘플 · 게인 램프 · 팬 · 페이드 · 루프 · 가상 진행.
 * @details 보이스는 오디오 스레드만 만집니다. 블록마다 엔진이 목표(볼륨 · 팬 · 피치)를 정하면 보이스는 블록 안에서 샘플마다 게인을 목표로 램프합니다.
 *          **가상(virtual)** 보이스는 섞지 않고 재생 위치만 진행합니다 — 다시 들리게 되면 그 시각의 자리에서 이어집니다(Wwise "virtual voice").
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/Dsp/AudioBiquad.h"

namespace sw
{
    /**
     * @class AudioVoice
     * @brief 재생 중인 소리 하나입니다.
     * @details 팬 법칙: 모노 소스는 등전력(equal-power) — 가운데에서 채널마다 -3 dB, 끝에서 한쪽 0 dB. 스테레오 소스는 밸런스 — 가운데에서 두 채널
     *          0 dB, 한쪽으로 갈수록 반대쪽이 줄어듭니다.
     */
    class SW_API AudioVoice
    {
    public:
        AudioVoice();

        /**
         * @brief 클립으로 재생을 시작합니다. 게인은 0 에서 첫 블록의 목표로 램프합니다(클릭 없음).
         * @param startDelayFrames 이 블록의 시작에서 몇 프레임 뒤에 소리를 낼지입니다(박자에 맞춘 시작). 그동안 위치는 진행하지 않습니다.
         * @param bRampIn false 면 첫 블록에서 게인을 0 에서 램프하지 않고 바로 겁니다 — 박에 맞춰 시작하는 음악 레이어(첫 샘플이 정확해야 한다).
         */
        void start( shared_ptr<const AudioClipData> pClip, bool bLoop, uint32 startDelayFrames, bool bRampIn = true );
        /** @brief 보이스를 비웁니다(클립을 놓습니다). */
        void reset();

        /**
         * @brief 블록 하나만큼 @p pBusInput(스테레오 교차)에 더하고 위치를 진행합니다. 끝나면(루프가 아니고 끝에 닿았거나 페이드아웃이 0 에 닿음) 끝남으로 표시합니다.
         * @param frameCount 블록 길이(프레임)입니다.
         * @param pScratch 로우패스가 걸렸을 때 쓰는 블록 길이 스테레오 작업 버퍼입니다(없으면 필터를 건너뜁니다).
         */
        void mix( float32* pBusInput, uint32 frameCount, float32* pScratch );
        /** @brief 섞지 않고 위치만 진행합니다(가상 보이스). 게인 램프 상태는 0 으로 둡니다 — 다시 섞일 때 0 에서 오릅니다. */
        void advanceVirtual( uint32 frameCount );

        /** @brief 블록의 목표 볼륨(선형)과 팬([-1, 1], -1 = 왼쪽)을 정합니다. */
        void setTarget( float32 volume, float32 pan );
        /** @brief 보이스 로우패스 컷오프(Hz)입니다 — 가림 · 공기 흡수 · 파라미터. `audio::kFilterOpenHz` 이상이면 거르지 않습니다. */
        void setLowPass( float32 cutoffHz );
        /** @brief 지금 로우패스 컷오프입니다. */
        float32 getLowPassHz() const { return _lowPassHz; }
        /** @brief 재생 위치(클립 프레임)를 옮깁니다. 루프면 감고, 아니면 끝을 넘을 때 끝냅니다. */
        void setPosition( float64 clipFrame );
        /** @brief 재생 속도 비입니다(클립 샘플레이트 / 출력 샘플레이트 × 피치). */
        void setPlaybackRate( float64 framesPerOutputFrame ) { _rate = framesPerOutputFrame; }
        /**
         * @brief 페이드를 겁니다. @p delayFrames 뒤에 시작해 @p durationFrames 동안 @p targetGain 으로 갑니다. 길이 0 은 바로 바꿉니다.
         * @param bStopAtEnd 목표에 닿으면 보이스를 끝냅니다(페이드아웃 정지).
         */
        void setFade( float32 targetGain, uint32 durationFrames, uint32 delayFrames, bool bStopAtEnd );
        /** @brief 일시정지합니다. 섞지도 진행하지도 않습니다. */
        void setPaused( bool bPaused ) { _bPaused = bPaused ? SW_TRUE : SW_FALSE; }

        /** @brief 클립이 있고 끝나지 않았는지입니다. */
        bool isActive() const { return _pClip != nullptr && _bFinished == SW_FALSE; }
        /** @brief 재생이 끝났는지입니다. */
        bool isFinished() const { return _bFinished == SW_TRUE; }
        /** @brief 일시정지 중인지입니다. */
        bool isPaused() const { return _bPaused == SW_TRUE; }
        /** @brief 클립 안의 재생 위치(클립 프레임)입니다. */
        float64 getPosition() const { return _position; }
        /** @brief 페이드가 가는 목표 게인입니다(들림 판정 — 페이드인 첫 블록도 들리는 것으로 본다). */
        float32 getFadeTarget() const { return _fadeTarget; }
        /** @brief 지금 걸린 페이드 게인입니다. */
        float32 getFadeGain() const { return _fadeGain; }
        /** @brief 클립입니다. */
        const AudioClipData* getClip() const { return _pClip.get(); }

    private:
        /** @brief 지연 · 페이드를 한 프레임 진행합니다. 페이드가 끝나 보이스를 멈춰야 하면 false 입니다. */
        bool stepFade();
        /** @brief 거르지 않고 @p pTarget 에 더합니다. */
        void mixInto( float32* pTarget, uint32 frameCount );
        /** @brief 위치를 @p frameCount 출력 프레임만큼 진행합니다(루프 · 끝 처리). */
        void advancePosition( uint32 frameCount );

    private:
        shared_ptr<const AudioClipData> _pClip;             /**< 재생하는 클립입니다. */
        AudioBiquadStereo               _lowPass;           /**< 보이스 로우패스입니다. */
        float64                         _position;          /**< 클립 안의 위치(프레임, 소수)입니다. */
        float64                         _rate;              /**< 출력 프레임당 진행하는 클립 프레임입니다. */
        float32                         _gainLeft;          /**< 지금 왼쪽 게인입니다(램프 중). */
        float32                         _gainRight;         /**< 지금 오른쪽 게인입니다(램프 중). */
        float32                         _targetGainLeft;    /**< 이 블록의 왼쪽 목표 게인입니다. */
        float32                         _targetGainRight;   /**< 이 블록의 오른쪽 목표 게인입니다. */
        float32                         _lowPassHz;         /**< 로우패스 컷오프(Hz)입니다. */
        float32                         _fadeGain;          /**< 페이드 게인입니다. */
        float32                         _fadeTarget;        /**< 페이드 목표입니다. */
        float32                         _fadeStep;          /**< 프레임마다 페이드 게인이 움직이는 양입니다(절댓값). */
        uint32                          _fadeDelayFrames;   /**< 페이드가 시작되기까지 남은 프레임입니다. */
        uint32                          _startDelayFrames;  /**< 소리가 시작되기까지 남은 프레임입니다. */
        uint8                           _bLoop         : 1; /**< 끝에서 처음으로 돌아갑니다. */
        uint8                           _bFinished     : 1; /**< 재생이 끝났습니다. */
        uint8                           _bPaused       : 1; /**< 일시정지 중입니다. */
        uint8                           _bStopAtFade   : 1; /**< 페이드가 목표에 닿으면 끝냅니다. */
        uint8                           _bFirstBlock   : 1; /**< 아직 한 번도 섞지 않았습니다 — 게인을 0 에서 램프합니다. */
        uint8                           _bRampIn       : 1; /**< 첫 블록에서 게인을 0 에서 램프합니다. */
        [[maybe_unused]] uint8          _reservedVoice : 2; /**< 비트필드 패딩입니다. */
    };
} // namespace sw
