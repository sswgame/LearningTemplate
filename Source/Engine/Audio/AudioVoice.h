/**
 * @file AudioVoice.h
 * @brief 클립 하나를 버스 입력에 섞는 보이스입니다. 재생 위치, 피치 리샘플, 게인 램프, 팬, 페이드, 루프, 가상 진행을 처리합니다.
 * @details 보이스는 오디오 스레드만 만진다. 블록마다 엔진이 목표(볼륨, 팬, 피치)를 정하면, 보이스는 블록 안에서 샘플마다 게인을 목표로 램프한다.
 *          스트리밍 클립(`AudioClipData::_pStream`)은 시작할 때 보이스가 디코더를 열고(`AudioStreamWindow`), 믹스하면서 필요한 만큼만 앞으로 풀어 읽는다.
 *          **가상(virtual)** 보이스는 섞지 않고 재생 위치만 진행한다. 다시 들리게 되면 그 시각의 위치에서 이어서 재생한다(Wwise 의 "virtual voice").
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioBinaural.h"
#include "Engine/Audio/AudioBlock.h"
#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioPanner.h"
#include "Engine/Audio/AudioStream.h"
#include "Engine/Audio/DSP/AudioBiquad.h"

namespace sw
{
    class AudioGraphInstance;

    /**
     * @class AudioVoice
     * @brief 재생 중인 소리 하나입니다.
     * @details 팬 법칙: 모노 소스는 방향을 `AudioPanner` 가 레이아웃의 채널별 게인으로 바꾼다(스테레오는 등전력, 가운데에서 채널마다 -3 dB).
     *          스테레오 소스는 어느 레이아웃에서나 앞 두 채널(FL FR)의 밸런스다. 가운데에서 두 채널이 0 dB 이고, 한쪽으로 갈수록 반대쪽이 줄어든다.
     *          로우패스는 패닝 전에 소스 채널(1 · 2)에 건다. 그래서 필터 비용이 출력 채널 수와 상관없다.
     *          바이노럴 레이아웃의 3D 모노 소리는 패너 게인 대신 HRTF(`AudioBinauralFilter`)가 두 귀 신호를 만들고, 볼륨만 두 채널에 같게 곱한다.
     */
    class SW_API AudioVoice
    {
    public:
        AudioVoice();

        /**
         * @brief 클립으로 재생을 시작합니다. 게인은 0 에서 첫 블록의 목표로 램프합니다(클릭 없음).
         * @param startDelayFrames 이 블록의 시작에서 몇 프레임 뒤에 소리를 낼지 정합니다(박자에 맞춘 시작). 그동안 위치는 진행하지 않습니다.
         * @param bRampIn false 면 첫 블록에서 게인을 0 에서 램프하지 않고 바로 적용합니다. 박에 맞춰 시작하는 음악 레이어는 첫 샘플이 정확해야 하기 때문입니다.
         * @param graphSeed 소리 그래프 클립의 잡음 · 그래뉼러 난수 시드입니다(파일 클립은 쓰지 않는다). 엔진이 자기 난수에서 뽑아 결정성을 지킵니다.
         * @details 소리 그래프 클립은 여기서 실행 상태(`AudioGraphInstance`)를 만들어 스트리밍 창에 디코더로 연다 — 스트리밍 클립이 디코더를 여는 것과 같은 자리의 할당이다.
         */
        void start( shared_ptr<const AudioClipData> pClip, bool bLoop, uint32 startDelayFrames, bool bRampIn, uint64 graphSeed );
        /** @brief 보이스를 비웁니다(클립을 놓습니다). */
        void reset();

        /**
         * @brief 블록 하나만큼 @p bus(평면, 채널 수는 마지막 `setTarget` 의 레이아웃, 길이는 `_frameCount`)에 더하고 위치를 진행합니다.
         *        끝나면(루프가 아니고 끝에 닿았거나 페이드아웃이 0 에 닿았을 때) 끝남으로 표시합니다.
         */
        void mix( const AudioBlockView& bus );
        /** @brief 섞지 않고 위치만 진행합니다(가상 보이스). 게인 램프 상태는 0 으로 두며, 다시 섞일 때 0 에서 오릅니다. */
        void advanceVirtual( uint32 frameCount );

        /** @brief 블록의 목표 볼륨(선형)과 방향을 정합니다. 채널별 목표 게인은 @p layout 으로 구합니다(다음 `mix` 의 버스 채널 수와 같아야 합니다). */
        void setTarget( float32 volume, const AudioPanDirection& direction, AudioChannelLayout layout );
        /** @brief 보이스 로우패스 컷오프(Hz)입니다. 가림, 공기 흡수, 파라미터가 정합니다. `audio::kFilterOpenHz` 이상이면 거르지 않습니다. */
        void setLowPass( float32 cutoffHz );
        /** @brief 지금 로우패스 컷오프입니다. */
        float32 getLowPassHz() const { return _lowPassHz; }
        /** @brief 재생 위치(클립 프레임)를 옮깁니다. 루프면 감고, 아니면 끝을 넘을 때 끝냅니다. */
        void setPosition( float64 clipFrame );
        /** @brief 재생 속도 비입니다(클립 샘플레이트 / 출력 샘플레이트 × 피치). */
        void setPlaybackRate( float64 framesPerOutputFrame ) { _rate = framesPerOutputFrame; }
        /**
         * @brief 페이드를 겁니다. @p delayFrames 뒤에 시작해 @p durationFrames 동안 @p targetGain 으로 이동합니다. 길이가 0 이면 바로 바꿉니다.
         * @param bStopAtEnd 목표에 닿으면 보이스를 끝냅니다(페이드아웃 정지).
         */
        void setFade( float32 targetGain, uint32 durationFrames, uint32 delayFrames, bool bStopAtEnd );
        /** @brief 일시정지합니다. 섞지도 진행하지도 않습니다. */
        void setPaused( bool bPaused ) { _bPaused = bPaused ? SW_TRUE : SW_FALSE; }
        /** @brief 소리 그래프의 게임 파라미터 @p inputIndex(`AudioGraph::getParameterNames` 의 번호)에 이번 블록 값을 넣습니다. 그래프 클립이 아니면 어서션입니다. */
        void setGraphParameter( uint32 inputIndex, float32 value );

        /** @brief 클립이 있고 끝나지 않았는지입니다. */
        bool isActive() const { return _pClip != nullptr && _bFinished == SW_FALSE; }
        /** @brief 재생이 끝났는지입니다. */
        bool isFinished() const { return _bFinished == SW_TRUE; }
        /** @brief 일시정지 중인지입니다. */
        bool isPaused() const { return _bPaused == SW_TRUE; }
        /** @brief 클립 안의 재생 위치(클립 프레임)입니다. */
        float64 getPosition() const { return _position; }
        /** @brief 페이드가 향하는 목표 게인입니다. 들림 판정에 쓰며, 페이드인 첫 블록도 들리는 것으로 봅니다. */
        float32 getFadeTarget() const { return _fadeTarget; }
        /** @brief 스트리밍 창입니다(스트리밍 · 그래프 클립이 아니면 닫혀 있다). 진단 · 테스트가 되감은 횟수를 읽는다. */
        const AudioStreamWindow& getStreamWindow() const { return _stream; }
        /** @brief 현재 적용 중인 페이드 게인입니다. */
        float32 getFadeGain() const { return _fadeGain; }
        /** @brief 클립입니다. */
        const AudioClipData* getClip() const { return _pClip.get(); }

    private:
        /** @brief 지연과 페이드를 한 프레임 진행합니다. 페이드가 끝나 보이스를 멈춰야 하면 false 를 반환합니다. */
        bool stepFade();
        /**
         * @brief 페이드 · 위치 진행을 하며 @p sampler 가 주는 보간 샘플을 @p source(평면 2 채널: 왼쪽 · 오른쪽)와 @p pFade 에 적습니다. 메모리 클립과 스트리밍 클립이 같은 본문을 쓴다.
         * @param outActiveBegin 소리가 난 첫 프레임입니다(시작 지연 뒤). @param outActiveEnd 소리가 난 마지막 프레임 + 1 입니다(끝났으면 그 자리).
         */
        template <typename TSampler>
        void renderSource( TSampler& sampler, const AudioBlockView& source, float32* pFade, uint32& outActiveBegin, uint32& outActiveEnd );
        /** @brief 위치를 @p frameCount 출력 프레임만큼 진행합니다(루프와 끝 처리). */
        void advancePosition( uint32 frameCount );

    private:
        shared_ptr<const AudioClipData> _pClip;                                  /**< 재생하는 클립입니다. */
        AudioBiquadFilter               _lowPass;                                /**< 보이스 로우패스입니다(소스 채널에 겁니다). */
        AudioBinauralFilter             _binaural;                               /**< 바이노럴 레이아웃에서 3D 모노 소리를 두 귀로 바꾸는 상태입니다. */
        AudioStreamWindow               _stream;                                 /**< 스트리밍 클립의 디코더와 창입니다. 소리 그래프도 이 창으로 읽습니다. */
        AudioGraphInstance*             _pGraph;                                 /**< `_stream` 이 가진 그래프 실행 상태입니다(빌린 포인터, 그래프 클립이 아니면 nullptr). */
        float32                         _firstFrameLeft;                         /**< 스트리밍 루프 이음매 보간에 쓰는 클립 첫 프레임(왼쪽)입니다. */
        float32                         _firstFrameRight;                        /**< 같은 첫 프레임의 오른쪽입니다. */
        bool                            _bHaveFirstFrame;                        /**< 첫 프레임을 읽어 두었는지입니다. */
        float64                         _position;                               /**< 클립 안의 위치(프레임, 소수)입니다. */
        float64                         _rate;                                   /**< 출력 프레임당 진행하는 클립 프레임입니다. */
        float32                         _arrGain[audio::kMaxChannelCount];       /**< 출력 채널마다 지금 게인입니다(램프 중). */
        float32                         _arrTargetGain[audio::kMaxChannelCount]; /**< 출력 채널마다 이 블록의 목표 게인입니다. */
        uint32                          _gainChannelCount;                       /**< 목표 게인을 구한 레이아웃의 채널 수입니다(0 = 아직 없음). */
        float32                         _lowPassHz;                              /**< 로우패스 컷오프(Hz)입니다. */
        float32                         _fadeGain;                               /**< 페이드 게인입니다. */
        float32                         _fadeTarget;                             /**< 페이드 목표입니다. */
        float32                         _fadeStep;                               /**< 프레임마다 페이드 게인이 움직이는 양입니다(절댓값). */
        uint32                          _fadeDelayFrames;                        /**< 페이드가 시작되기까지 남은 프레임입니다. */
        uint32                          _startDelayFrames;                       /**< 소리가 시작되기까지 남은 프레임입니다. */
        uint8                           _bLoop         : 1;                      /**< 끝에서 처음으로 돌아갑니다. */
        uint8                           _bFinished     : 1;                      /**< 재생이 끝났습니다. */
        uint8                           _bPaused       : 1;                      /**< 일시정지 중입니다. */
        uint8                           _bStopAtFade   : 1;                      /**< 페이드가 목표에 닿으면 끝냅니다. */
        uint8                           _bFirstBlock   : 1;                      /**< 아직 한 번도 섞지 않았습니다. 게인을 0 에서 램프합니다. */
        uint8                           _bRampIn       : 1;                      /**< 첫 블록에서 게인을 0 에서 램프합니다. */
        uint8                           _bBinaural     : 1;                      /**< 이번 블록은 바이노럴(HRTF)로 낸다(바이노럴 레이아웃 · 모노 · 3D 방향). */
        [[maybe_unused]] uint8          _reservedVoice : 1;                      /**< 비트필드 패딩입니다. */
    };
} // namespace sw
