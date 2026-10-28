#include "pch.h"

#include "Engine/Audio/DSP/AudioEffect.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetIO.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/DSP/AudioBiquad.h"
#include "Engine/Audio/DSP/AudioFFT.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEffect" );

    namespace
    {
        /**
         * @brief 바이쿼드 필터 하나로 이루어진 이펙트입니다. LowPass, HighPass, BandPass, Peaking, LowShelf, HighShelf 를 지원합니다. 파라미터는 frequencyHz, q, gainDb 입니다.
         * @details LowPass 의 컷오프가 `audio::kFilterOpenHz` 이상이면 거르지 않는다. 스냅샷이 "물속"에서만 내려 쓰는 필터를 평소에는 비용 없이 둘 수 있다.
         */
        class AudioFilterEffect final : public IAudioEffect
        {
        public:
            AudioFilterEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout, AudioBiquadType type )
                : IAudioEffect{ typeInfo, layout }
                , _filter{}
                , _type{ type }
                , _bBypass{ false }
            {
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                if ( _bBypass == false )
                    _filter.process( block );
            }

            void reset() override { _filter.reset(); }

        protected:
            void onParameterChanged() override
            {
                const float32 frequency = _listParameter[0];
                const bool    bOpenLow  = _type == AudioBiquadType::LowPass && frequency >= audio::kFilterOpenHz;
                const bool    bOpenHigh = _type == AudioBiquadType::HighPass && frequency <= 10.0f;
                _bBypass                = bOpenLow || bOpenHigh;
                _filter._coefficients   = AudioBiquadCoefficients::make( _type, frequency, _listParameter[1], _listParameter[2], static_cast<float32>( audio::kSampleRate ) );
            }

        private:
            AudioBiquadFilter _filter;  /**< 필터 상태입니다(채널마다). */
            AudioBiquadType   _type;    /**< 필터 형태입니다. */
            bool              _bBypass; /**< 걸러도 같은 신호라 건너뜁니다. */
        };

        /**
         * @brief 피드 포워드 컴프레서입니다(전 채널 링크, 피크 검출, 소프트 니). Giannoulis, Massberg, Reiss(2012)의 "smooth decoupled peak" 검출기를 씁니다.
         * @details 파라미터는 thresholdDb, ratio, attackMs, releaseMs, kneeDb, makeupDb 입니다. 게인 감소(dB)는 정상 상태에서 (입력 - 임계값) × (1 - 1/비율)입니다.
         *          검출은 프레임마다 모든 채널의 절댓값 최대이고, 같은 게인을 모든 채널에 곱한다(서라운드 이미지가 흔들리지 않는다).
         */
        class AudioCompressorEffect final : public IAudioEffect
        {
        public:
            AudioCompressorEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
                : IAudioEffect{ typeInfo, layout }
                , _attackCoefficient{ 0.0f }
                , _releaseCoefficient{ 0.0f }
                , _peakReductionDb{ 0.0f }
                , _smoothReductionDb{ 0.0f }
            {
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                const float32 threshold = _listParameter[0];
                const float32 ratio     = MathUtil::max( 1.0f, _listParameter[1] );
                const float32 knee      = MathUtil::max( 0.0f, _listParameter[4] );
                const float32 makeup    = _listParameter[5];
                const float32 slope     = 1.0f / ratio - 1.0f;
                SW_ASSERT( block._channelCount == _channelCount );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    float32 peak = 0.0f;
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        peak = MathUtil::max( peak, MathUtil::abs( block.getChannel( channel )[frameIndex] ) );
                    }
                    const float32 levelDb = AudioMath::linearToDb( peak );
                    const float32 over    = levelDb - threshold;
                    float32       outDb   = levelDb;
                    if ( 2.0f * over >= knee )
                        outDb = threshold + over / ratio;
                    else if ( 2.0f * over > -knee && knee > 0.0f )
                        outDb = levelDb + slope * ( over + knee * 0.5f ) * ( over + knee * 0.5f ) / ( 2.0f * knee );
                    const float32 reductionDb = levelDb - outDb;

                    // 감소량을 피크로 붙잡고(릴리스로 내려온다) 다시 어택으로 따라간다.
                    _peakReductionDb   = MathUtil::max( reductionDb, _releaseCoefficient * _peakReductionDb + ( 1.0f - _releaseCoefficient ) * reductionDb );
                    _smoothReductionDb = _attackCoefficient * _smoothReductionDb + ( 1.0f - _attackCoefficient ) * _peakReductionDb;
                    const float32 gain = AudioMath::dbToLinear( makeup - _smoothReductionDb );
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        block.getChannel( channel )[frameIndex] *= gain;
                    }
                }
            }

            void reset() override
            {
                _peakReductionDb   = 0.0f;
                _smoothReductionDb = 0.0f;
            }

        protected:
            void onParameterChanged() override
            {
                _attackCoefficient  = computeTimeCoefficient( _listParameter[2] );
                _releaseCoefficient = computeTimeCoefficient( _listParameter[3] );
            }

        private:
            /** @brief 시간 상수(ms)에 대한 한 극 계수를 반환합니다. e^(-1 / (τ·fs)) 입니다. */
            static float32 computeTimeCoefficient( float32 milliseconds )
            {
                const float32 samples = MathUtil::max( 1.0f, milliseconds * 0.001f * static_cast<float32>( audio::kSampleRate ) );
                return MathUtil::pow( MathUtil::kEuler, -1.0f / samples );
            }

            float32 _attackCoefficient;  /**< 어택 계수입니다. */
            float32 _releaseCoefficient; /**< 릴리스 계수입니다. */
            float32 _peakReductionDb;    /**< 붙잡은 감소량(dB)입니다. */
            float32 _smoothReductionDb;  /**< 어택으로 따라간 감소량(dB)입니다. */
        };

        /**
         * @brief 미리 보기(lookahead) 브릭월 리미터입니다. 출력 피크가 천장을 넘지 않습니다.
         * @details 파라미터는 ceilingDb, releaseMs, lookaheadMs 입니다. 필요한 게인(천장 / |x|)의 창 최솟값을 같은 길이의 상자 평균으로 다듬고, 소리는 그만큼 늦춥니다.
         *          창 안의 모든 최솟값이 그 샘플의 필요 게인 이하이므로 평균도 이하입니다(넘침 없음, 클릭 없음). 피크는 모든 채널의 최대이고 게인은 모든 채널에 같습니다.
         */
        class AudioLimiterEffect final : public IAudioEffect
        {
        public:
            AudioLimiterEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
                : IAudioEffect{ typeInfo, layout }
                , _listDelay{}
                , _listMinValue{}
                , _listMinIndex{}
                , _listAverage{}
                , _sampleIndex{ 0 }
                , _minHead{ 0 }
                , _minCount{ 0 }
                , _windowLength{ 1 }
                , _averageSum{ 0.0 }
                , _releaseCoefficient{ 0.0f }
                , _gain{ 1.0f }
            {
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                const float32 ceiling = AudioMath::dbToLinear( _listParameter[0] );
                const uint32  window  = _windowLength;
                SW_ASSERT( block._channelCount == _channelCount );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    float32 peak = 0.0f;
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        peak = MathUtil::max( peak, MathUtil::abs( block.getChannel( channel )[frameIndex] ) );
                    }
                    const float32 required = peak > ceiling ? ceiling / peak : 1.0f;

                    // 창(최근 window + 1 샘플)의 최솟값을 구한다(단조 덱). 늦춘 샘플(window 전)까지 창에 들어야 평균이 그 샘플의 필요 게인 이하가 된다.
                    const uint64 index = _sampleIndex;
                    while ( _minCount > 0 && _listMinValue[( _minHead + _minCount - 1 ) % _listMinValue.size()] >= required )
                    {
                        --_minCount;
                    }
                    _listMinValue[( _minHead + _minCount ) % _listMinValue.size()] = required;
                    _listMinIndex[( _minHead + _minCount ) % _listMinIndex.size()] = index;
                    ++_minCount;
                    while ( _listMinIndex[_minHead] + window + 1 <= index )
                    {
                        _minHead = ( _minHead + 1 ) % static_cast<uint32>( _listMinValue.size() );
                        --_minCount;
                    }
                    const float32 windowMin = _listMinValue[_minHead];

                    // 같은 길이의 상자 평균.
                    const uint32 averageSlot = static_cast<uint32>( index % window );
                    _averageSum += static_cast<float64>( windowMin ) - static_cast<float64>( _listAverage[averageSlot] );
                    _listAverage[averageSlot] = windowMin;
                    const float32 smoothed    = MathUtil::min( 1.0f, static_cast<float32>( _averageSum / static_cast<float64>( window ) ) );

                    // 내려갈 때는 바로 따라가고, 올라갈 때는 릴리스를 적용한다.
                    _gain = smoothed < _gain ? smoothed : _gain + ( smoothed - _gain ) * ( 1.0f - _releaseCoefficient );

                    // window 샘플 늦춘 소리에 게인을 적용한다.
                    const uint32 delaySlot = static_cast<uint32>( index % window );
                    const size_t delayBase = static_cast<size_t>( delaySlot ) * _channelCount;
                    const bool   bDirect   = window == 1;
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        float32&      sample            = block.getChannel( channel )[frameIndex];
                        const float32 input             = sample;
                        const float32 delayed           = _listDelay[delayBase + channel];
                        _listDelay[delayBase + channel] = input;
                        sample                          = ( bDirect ? input : delayed ) * _gain;
                    }
                    ++_sampleIndex;
                }
            }

            void reset() override
            {
                const size_t window = _windowLength;
                _listDelay.assign( window * _channelCount, 0.0f );
                _listMinValue.assign( window + 2, 1.0f );
                _listMinIndex.assign( window + 2, 0 );
                _listAverage.assign( window, 1.0f );
                _averageSum  = static_cast<float64>( window );
                _sampleIndex = 0;
                _minHead     = 0;
                _minCount    = 0;
                _gain        = 1.0f;
            }

        protected:
            void onParameterChanged() override
            {
                const float32 releaseSamples = MathUtil::max( 1.0f, _listParameter[1] * 0.001f * static_cast<float32>( audio::kSampleRate ) );
                _releaseCoefficient          = MathUtil::pow( MathUtil::kEuler, -1.0f / releaseSamples );
                const uint32 window          = MathUtil::max( 1u, static_cast<uint32>( _listParameter[2] * 0.001f * static_cast<float32>( audio::kSampleRate ) ) );
                if ( window != _windowLength || _listDelay.empty() )
                {
                    _windowLength = window;
                    reset();
                }
            }

        private:
            vector<float32> _listDelay;          /**< 늦춘 소리(내부 상태는 채널 교차, 창 길이)입니다. */
            vector<float32> _listMinValue;       /**< 단조 덱의 값입니다. */
            vector<uint64>  _listMinIndex;       /**< 단조 덱의 샘플 번호입니다. */
            vector<float32> _listAverage;        /**< 상자 평균의 고리 버퍼입니다. */
            uint64          _sampleIndex;        /**< 처리한 샘플 수입니다. */
            uint32          _minHead;            /**< 덱의 머리입니다. */
            uint32          _minCount;           /**< 덱의 길이입니다. */
            uint32          _windowLength;       /**< 미리 보기 창(샘플)입니다. */
            float64         _averageSum;         /**< 상자 평균의 합입니다. */
            float32         _releaseCoefficient; /**< 릴리스 계수입니다. */
            float32         _gain;               /**< 지금 게인입니다. */
        };

        /**
         * @brief Freeverb(Jezar) 리버브입니다. 젖은 채널마다 병렬 콤 필터 8 개(감쇠 로우패스)와 직렬 올패스 4 개를 쓰고, k 번째 젖은 채널은 23·k 샘플 벌립니다.
         * @details 파라미터는 roomSize, damping, wet, dry, width, preDelayMs 입니다. 지연 길이는 원본(44.1 kHz) 값을 출력 샘플레이트에 맞게 늘려 씁니다.
         *          젖은 채널은 LFE 를 뺀 모든 채널이다. 입력은 그 채널들의 합에 고정 게인 × 2 / (젖은 채널 수)를 곱해(스테레오에서 원본과 같다) 모노로 넣고,
         *          출력은 자기 채널의 젖은 소리 × wetMain + 다른 젖은 채널 평균 × wetCross + 입력 × dry 다(스테레오에서는 원본의 교차 혼합과 같다). LFE 는 dry 만 지난다.
         */
        class AudioReverbEffect final : public IAudioEffect
        {
        public:
            AudioReverbEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
                : IAudioEffect{ typeInfo, layout }
                , _arrComb{}
                , _arrAllpass{}
                , _listPreDelay{}
                , _arrWetChannel{}
                , _preDelayCursor{ 0 }
                , _preDelayLength{ 0 }
                , _wetChannelCount{ 0 }
                , _inputGain{ 0.0f }
                , _crossScale{ 0.0f }
                , _feedback{ 0.0f }
                , _damp{ 0.0f }
                , _wetMain{ 0.0f }
                , _wetCross{ 0.0f }
                , _dry{ 0.0f }
            {
                static constexpr uint32  kArrCombTuning[kCombCount]       = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
                static constexpr uint32  kArrAllpassTuning[kAllpassCount] = { 556, 441, 341, 225 };
                static constexpr uint32  kChannelSpread                   = 23;
                static constexpr float32 kFixedGain                       = 0.015f;
                for ( uint32 channel = 0; channel < _channelCount; ++channel )
                {
                    const bool bLfe = AudioChannelLayoutUtil::hasLfe( layout ) && channel == audio::kLfeChannelIndex;
                    if ( bLfe )
                        continue;
                    _arrWetChannel[_wetChannelCount] = channel;
                    ++_wetChannelCount;
                }
                for ( uint32 wetIndex = 0; wetIndex < _wetChannelCount; ++wetIndex )
                {
                    for ( uint32 combIndex = 0; combIndex < kCombCount; ++combIndex )
                    {
                        _arrComb[wetIndex][combIndex]._listBuffer.assign( scaleLength( kArrCombTuning[combIndex] + wetIndex * kChannelSpread ), 0.0f );
                    }
                    for ( uint32 allpassIndex = 0; allpassIndex < kAllpassCount; ++allpassIndex )
                    {
                        _arrAllpass[wetIndex][allpassIndex]._listBuffer.assign( scaleLength( kArrAllpassTuning[allpassIndex] + wetIndex * kChannelSpread ), 0.0f );
                    }
                }
                _inputGain  = kFixedGain * static_cast<float32>( audio::kStereoChannelCount ) / static_cast<float32>( _wetChannelCount );
                _crossScale = 1.0f / static_cast<float32>( _wetChannelCount - 1 );
                _listPreDelay.assign( static_cast<size_t>( audio::kSampleRate / 5 + 1 ) * 2, 0.0f );
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                SW_ASSERT( block._channelCount == _channelCount );
                const size_t preDelaySlots = _listPreDelay.size() / 2;
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    float32 arrInput[audio::kMaxChannelCount]{};
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        arrInput[channel] = block.getChannel( channel )[frameIndex];
                    }
                    float32 inputSum = 0.0f;
                    for ( uint32 wetIndex = 0; wetIndex < _wetChannelCount; ++wetIndex )
                    {
                        inputSum += arrInput[_arrWetChannel[wetIndex]];
                    }
                    float32 input = inputSum * _inputGain;
                    if ( _preDelayLength > 0 )
                    {
                        const size_t  readSlot         = ( _preDelayCursor + preDelaySlots - _preDelayLength ) % preDelaySlots;
                        const float32 delayed          = _listPreDelay[readSlot];
                        _listPreDelay[_preDelayCursor] = input;
                        _preDelayCursor                = ( _preDelayCursor + 1 ) % preDelaySlots;
                        input                          = delayed;
                    }

                    float32 arrWetOutput[audio::kMaxChannelCount]{};
                    for ( uint32 wetIndex = 0; wetIndex < _wetChannelCount; ++wetIndex )
                    {
                        float32 sum = 0.0f;
                        for ( Comb& comb : _arrComb[wetIndex] )
                        {
                            const float32 output           = comb._listBuffer[comb._cursor];
                            comb._filterStore              = output * ( 1.0f - _damp ) + comb._filterStore * _damp;
                            comb._listBuffer[comb._cursor] = input + comb._filterStore * _feedback;
                            comb._cursor                   = ( comb._cursor + 1 ) % static_cast<uint32>( comb._listBuffer.size() );
                            sum += output;
                        }
                        for ( Allpass& allpass : _arrAllpass[wetIndex] )
                        {
                            const float32 buffered               = allpass._listBuffer[allpass._cursor];
                            const float32 output                 = buffered - sum;
                            allpass._listBuffer[allpass._cursor] = sum + buffered * 0.5f;
                            allpass._cursor                      = ( allpass._cursor + 1 ) % static_cast<uint32>( allpass._listBuffer.size() );
                            sum                                  = output;
                        }
                        arrWetOutput[wetIndex] = sum;
                    }
                    // LFE 같은 젖지 않는 채널은 dry 만 지난다.
                    for ( uint32 channel = 0; channel < _channelCount; ++channel )
                    {
                        block.getChannel( channel )[frameIndex] = arrInput[channel] * _dry;
                    }
                    for ( uint32 wetIndex = 0; wetIndex < _wetChannelCount; ++wetIndex )
                    {
                        float32 cross = 0.0f;
                        for ( uint32 otherIndex = 0; otherIndex < _wetChannelCount; ++otherIndex )
                        {
                            if ( otherIndex != wetIndex )
                                cross += arrWetOutput[otherIndex];
                        }
                        const uint32 channel                    = _arrWetChannel[wetIndex];
                        block.getChannel( channel )[frameIndex] = arrWetOutput[wetIndex] * _wetMain + cross * _crossScale * _wetCross + arrInput[channel] * _dry;
                    }
                }
            }

            void reset() override
            {
                for ( uint32 wetIndex = 0; wetIndex < _wetChannelCount; ++wetIndex )
                {
                    for ( Comb& comb : _arrComb[wetIndex] )
                    {
                        comb._listBuffer.assign( comb._listBuffer.size(), 0.0f );
                        comb._filterStore = 0.0f;
                    }
                    for ( Allpass& allpass : _arrAllpass[wetIndex] )
                    {
                        allpass._listBuffer.assign( allpass._listBuffer.size(), 0.0f );
                    }
                }
                _listPreDelay.assign( _listPreDelay.size(), 0.0f );
            }

        protected:
            void onParameterChanged() override
            {
                // Freeverb 의 눈금: 방 크기 0..1 → 피드백 0.7..0.98, 감쇠 0..1 → 0..0.4, wet 은 ×3.
                _feedback             = _listParameter[0] * 0.28f + 0.7f;
                _damp                 = _listParameter[1] * 0.4f;
                const float32 wet     = _listParameter[2] * 3.0f;
                const float32 width   = _listParameter[4];
                _wetMain              = wet * ( width * 0.5f + 0.5f );
                _wetCross             = wet * ( ( 1.0f - width ) * 0.5f );
                _dry                  = _listParameter[3];
                const size_t maxSlots = _listPreDelay.size() / 2;
                _preDelayLength       = MathUtil::min( maxSlots - 1, static_cast<size_t>( _listParameter[5] * 0.001f * static_cast<float32>( audio::kSampleRate ) ) );
            }

        private:
            static constexpr uint32 kCombCount    = 8;
            static constexpr uint32 kAllpassCount = 4;

            /** @brief 원본 44.1 kHz 지연 길이를 출력 샘플레이트에 맞게 늘립니다. */
            static size_t scaleLength( uint32 samplesAt44k ) { return static_cast<size_t>( samplesAt44k ) * audio::kSampleRate / 44100u; }

            struct Comb
            {
                vector<float32> _listBuffer{};
                uint32          _cursor{ 0 };
                float32         _filterStore{ 0.0f };
            };

            struct Allpass
            {
                vector<float32> _listBuffer{};
                uint32          _cursor{ 0 };
            };

            Comb            _arrComb[audio::kMaxChannelCount][kCombCount];       /**< 젖은 채널마다 콤 필터입니다(쓰는 채널만 버퍼를 잡습니다). */
            Allpass         _arrAllpass[audio::kMaxChannelCount][kAllpassCount]; /**< 젖은 채널마다 올패스입니다. */
            vector<float32> _listPreDelay;                                       /**< 프리딜레이(모노 입력, 0.2 초)입니다. */
            uint32          _arrWetChannel[audio::kMaxChannelCount];             /**< 젖은 채널(LFE 를 뺀 채널)의 블록 채널 번호입니다. */
            size_t          _preDelayCursor;                                     /**< 프리딜레이 쓰기 위치입니다. */
            size_t          _preDelayLength;                                     /**< 프리딜레이 길이(샘플)입니다. */
            uint32          _wetChannelCount;                                    /**< 젖은 채널 수입니다(2 이상). */
            float32         _inputGain;                                          /**< 젖은 채널 합에 곱하는 입력 게인입니다. */
            float32         _crossScale;                                         /**< 다른 젖은 채널 합을 평균으로 바꾸는 비입니다(1 / (수 − 1)). */
            float32         _feedback;                                           /**< 콤 피드백입니다. */
            float32         _damp;                                               /**< 콤 안 로우패스 계수입니다. */
            float32         _wetMain;                                            /**< 같은 쪽 wet 게인입니다. */
            float32         _wetCross;                                           /**< 반대쪽 wet 게인입니다(폭이 좁을수록 크다). */
            float32         _dry;                                                /**< dry 게인입니다. */
        };

        /**
         * @brief 피드백 딜레이(에코)입니다. 파라미터는 timeMs, feedback, wet, dry, dampingHz 입니다. dampingHz 는 피드백 경로의 한 극 로우패스이며, 20 kHz 이상이면 쓰지 않습니다.
         * @details 첫 에코는 timeMs 뒤에 wet 크기로 오고, 다음 에코는 2·timeMs 뒤에 wet·feedback 크기로 온다.
         */
        class AudioDelayEffect final : public IAudioEffect
        {
        public:
            AudioDelayEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
                : IAudioEffect{ typeInfo, layout }
                , _listBuffer{}
                , _cursor{ 0 }
                , _delayFrames{ 1 }
                , _arrDampState{}
                , _dampCoefficient{ 0.0f }
            {
                _listBuffer.assign( static_cast<size_t>( audio::kSampleRate ) * 2 * _channelCount, 0.0f );
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                SW_ASSERT( block._channelCount == _channelCount );
                const uint32  channelCount = _channelCount;
                const float32 feedback     = _listParameter[1];
                const float32 wet          = _listParameter[2];
                const float32 dry          = _listParameter[3];
                const uint32  slotCount    = static_cast<uint32>( _listBuffer.size() / channelCount );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    const uint32 readSlot = ( _cursor + slotCount - _delayFrames ) % slotCount;
                    for ( uint32 channel = 0; channel < channelCount; ++channel )
                    {
                        float32*      pChannel                        = block.getChannel( channel );
                        const float32 input                           = pChannel[frameIndex];
                        const float32 delayed                         = _listBuffer[readSlot * channelCount + channel];
                        _arrDampState[channel]                        = delayed + ( _arrDampState[channel] - delayed ) * _dampCoefficient;
                        _listBuffer[_cursor * channelCount + channel] = input + _arrDampState[channel] * feedback;
                        pChannel[frameIndex]                          = input * dry + delayed * wet;
                    }
                    _cursor = ( _cursor + 1 ) % slotCount;
                }
            }

            void reset() override
            {
                _listBuffer.assign( _listBuffer.size(), 0.0f );
                for ( float32& state : _arrDampState )
                {
                    state = 0.0f;
                }
            }

        protected:
            void onParameterChanged() override
            {
                const uint32 slotCount = static_cast<uint32>( _listBuffer.size() / _channelCount );
                _delayFrames           = MathUtil::clamp( static_cast<uint32>( _listParameter[0] * 0.001f * static_cast<float32>( audio::kSampleRate ) ), 1u, slotCount - 1 );
                const float32 cutoff   = _listParameter[4];
                _dampCoefficient       = cutoff >= audio::kFilterOpenHz ? 0.0f
                                                                        : MathUtil::pow( MathUtil::kEuler, -2.0f * MathUtil::kPi * cutoff / static_cast<float32>( audio::kSampleRate ) );
            }

        private:
            vector<float32> _listBuffer;                            /**< 지연선(채널 교차, 2 초)입니다. */
            uint32          _cursor;                                /**< 쓰기 위치입니다. */
            uint32          _delayFrames;                           /**< 지연(프레임)입니다. */
            float32         _arrDampState[audio::kMaxChannelCount]; /**< 채널마다 피드백 로우패스 상태입니다. */
            float32         _dampCoefficient;                       /**< 피드백 로우패스 계수입니다(0 이면 없음). */
        };

        /**
         * @brief 컨볼루션 리버브입니다. 데이터의 임펄스 응답(`AudioEffectDesc::_impulse`, 실제 공간을 녹음한 클립)을 입력에 컨볼루션합니다.
         * @details 균일 분할 overlap-save 다. 임펄스 응답을 블록(256 프레임) 조각으로 나눠 조각마다 FFT(512)를 미리 해 두고, 블록마다 입력 FFT 하나를 주파수 영역 지연선에 넣어
         *          조각 수만큼 곱해 더한 뒤 역FFT 한다. 블록 하나(5.3 ms)를 모아 처리하므로 젖은 소리는 그만큼 늦는다(dry 는 늦지 않는다).
         *          젖은 채널은 LFE 를 뺀 채널이고, k 번째 젖은 채널은 임펄스 응답의 (k mod 응답 채널 수) 번째 채널을 쓴다(스테레오 응답이면 왼쪽 · 오른쪽이 번갈아).
         *          임펄스 응답은 만들 때(게임 스레드) 읽고 48 kHz 로 선형 리샘플하며, `kMaxImpulseSeconds` 보다 길면 오류다(조각 수가 곧 비용이다).
         *          파라미터는 wet, dry 입니다.
         */
        class AudioConvolutionEffect final : public IAudioEffect
        {
        public:
            /** @brief 조각 하나의 프레임 수(블록 길이)입니다. */
            static constexpr uint32 kPartitionFrames = audio::kBlockFrameCount;
            /** @brief FFT 크기(조각 둘)입니다. */
            static constexpr uint32 kFFTSize = kPartitionFrames * 2;
            /** @brief 받는 가장 긴 임펄스 응답(초)입니다. 3 초면 조각 563 개, 블록마다 젖은 채널당 복소 곱 약 29 만 번입니다. */
            static constexpr float32 kMaxImpulseSeconds = 3.0f;

            AudioConvolutionEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
                : IAudioEffect{ typeInfo, layout }
                , _fft{}
                , _arrImpulseSpectrum{}
                , _listWork{}
                , _listAccumulate{}
                , _listWet{}
                , _partitionCount{ 0 }
                , _historyCursor{ 0 }
                , _fillIndex{ 0 }
                , _wet{ 0.0f }
                , _dry{ 0.0f }
            {
                onParameterChanged();
            }

            /** @brief 임펄스 응답을 읽어 조각 스펙트럼과 채널 상태를 만듭니다. 읽기 · 디코드 실패, 빈 응답, 너무 긴 응답은 오류를 남기고 false 입니다. */
            [[nodiscard]] bool loadImpulse( const DataPath& path )
            {
                if ( path.isSet() == false )
                {
                    SW_LOG_ERROR( "ConvolutionReverb needs an impulse response clip (_impulse)" );
                    return false;
                }
                vector<uint8> bytes;
                AudioPcm      pcm;
                AudioClipData clip;
                const bool    bDecoded = AssetIO::readBinaryAsset( AssetID{ path.get() }, bytes ) && AudioClipDecoder::decode( path.get(), bytes.data(), bytes.size(), pcm ) &&
                                      AudioClipData::convertPcm( pcm, clip );
                if ( bDecoded == false || clip._frameCount == 0 )
                {
                    SW_LOG_ERROR( "ConvolutionReverb could not read the impulse response '%#'", path.get() );
                    return false;
                }
                // 48 kHz 로 선형 리샘플한다(응답 길이 기준 시간은 그대로).
                const uint32  channelCount = clip._channelCount;
                const float64 ratio        = static_cast<float64>( clip._sampleRate ) / static_cast<float64>( audio::kSampleRate );
                const uint32  frameCount   = static_cast<uint32>( static_cast<float64>( clip._frameCount ) / ratio );
                if ( static_cast<float32>( frameCount ) > kMaxImpulseSeconds * static_cast<float32>( audio::kSampleRate ) )
                {
                    SW_LOG_ERROR( "ConvolutionReverb impulse response '%#' is longer than %# s", path.get(), kMaxImpulseSeconds );
                    return false;
                }
                if ( _fft.initialize( kFFTSize ) == false )
                    return false;
                _partitionCount = MathUtil::max( 1u, ( frameCount + kPartitionFrames - 1 ) / kPartitionFrames );
                for ( uint32 impulseChannel = 0; impulseChannel < channelCount; ++impulseChannel )
                {
                    vector<AudioComplex>& spectrum = _arrImpulseSpectrum[impulseChannel];
                    spectrum.assign( static_cast<size_t>( _partitionCount ) * kFFTSize, AudioComplex{} );
                    for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                    {
                        const float64 source   = static_cast<float64>( frameIndex ) * ratio;
                        const uint32  index    = static_cast<uint32>( source );
                        const uint32  next     = MathUtil::min( index + 1, clip._frameCount - 1 );
                        const float32 fraction = static_cast<float32>( source - static_cast<float64>( index ) );
                        const float32 sampleA  = clip._listSample[static_cast<size_t>( index ) * channelCount + impulseChannel];
                        const float32 sampleB  = clip._listSample[static_cast<size_t>( next ) * channelCount + impulseChannel];
                        const uint32  part     = frameIndex / kPartitionFrames;
                        // 조각마다 앞 절반에 응답, 뒤 절반은 0(overlap-save 의 영 채우기)이다.
                        spectrum[static_cast<size_t>( part ) * kFFTSize + frameIndex % kPartitionFrames]._real = sampleA + ( sampleB - sampleA ) * fraction;
                    }
                    for ( uint32 part = 0; part < _partitionCount; ++part )
                    {
                        _fft.forward( spectrum.data() + static_cast<size_t>( part ) * kFFTSize );
                    }
                }
                for ( uint32 channel = 0; channel < _channelCount; ++channel )
                {
                    const bool bLfe = AudioChannelLayoutUtil::hasLfe( _layout ) && channel == audio::kLfeChannelIndex;
                    if ( bLfe )
                        continue;
                    WetChannel wet;
                    wet._channelIndex   = channel;
                    wet._impulseChannel = static_cast<uint32>( _listWet.size() ) % channelCount;
                    wet._listHistory.assign( static_cast<size_t>( _partitionCount ) * kFFTSize, AudioComplex{} );
                    wet._listInput.assign( kPartitionFrames, 0.0f );
                    wet._listPrevious.assign( kPartitionFrames, 0.0f );
                    wet._listOutput.assign( kPartitionFrames, 0.0f );
                    _listWet.push_back( std::move( wet ) );
                }
                _listWork.assign( kFFTSize, AudioComplex{} );
                _listAccumulate.assign( kFFTSize, AudioComplex{} );
                return true;
            }

            void process( const AudioBlockView& block ) override
            {
                SW_ASSERT( block._channelCount == _channelCount );
                const bool bHasLfe = AudioChannelLayoutUtil::hasLfe( _layout );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    for ( WetChannel& wet : _listWet )
                    {
                        float32&      sample       = block.getChannel( wet._channelIndex )[frameIndex];
                        const float32 input        = sample;
                        wet._listInput[_fillIndex] = input;
                        sample                     = input * _dry + wet._listOutput[_fillIndex] * _wet;
                    }
                    // LFE 는 젖지 않고 dry 만 지난다.
                    if ( bHasLfe )
                        block.getChannel( audio::kLfeChannelIndex )[frameIndex] *= _dry;
                    ++_fillIndex;
                    if ( _fillIndex == kPartitionFrames )
                    {
                        runPartition();
                        _fillIndex = 0;
                    }
                }
            }

            void reset() override
            {
                for ( WetChannel& wet : _listWet )
                {
                    wet._listHistory.assign( wet._listHistory.size(), AudioComplex{} );
                    wet._listInput.assign( kPartitionFrames, 0.0f );
                    wet._listPrevious.assign( kPartitionFrames, 0.0f );
                    wet._listOutput.assign( kPartitionFrames, 0.0f );
                }
                _historyCursor = 0;
                _fillIndex     = 0;
            }

        protected:
            void onParameterChanged() override
            {
                _wet = _listParameter[0];
                _dry = _listParameter[1];
            }

        private:
            /** @brief 젖은 채널 하나의 상태입니다. */
            struct WetChannel
            {
                vector<AudioComplex> _listHistory{};  ///< 주파수 영역 지연선(조각 수 × FFT 크기)이다.
                vector<float32>      _listInput{};    ///< 모으는 중인 입력 블록이다.
                vector<float32>      _listPrevious{}; ///< 지난 입력 블록(overlap-save 앞 절반)이다.
                vector<float32>      _listOutput{};   ///< 지난 블록에서 나온 젖은 소리다(한 블록 늦게 낸다).
                uint32               _channelIndex{ 0 };
                uint32               _impulseChannel{ 0 };
            };

            /** @brief 모은 입력 블록 하나를 컨볼루션해 다음 블록의 젖은 소리를 만듭니다. */
            void runPartition()
            {
                for ( WetChannel& wet : _listWet )
                {
                    for ( uint32 index = 0; index < kPartitionFrames; ++index )
                    {
                        _listWork[index]                    = AudioComplex{ wet._listPrevious[index], 0.0f };
                        _listWork[index + kPartitionFrames] = AudioComplex{ wet._listInput[index], 0.0f };
                    }
                    _fft.forward( _listWork.data() );
                    AudioComplex* pSlot = wet._listHistory.data() + static_cast<size_t>( _historyCursor ) * kFFTSize;
                    Memory::copy( pSlot, _listWork.data(), sizeof( AudioComplex ) * kFFTSize );

                    for ( AudioComplex& value : _listAccumulate )
                    {
                        value = AudioComplex{};
                    }
                    const vector<AudioComplex>& impulse = _arrImpulseSpectrum[wet._impulseChannel];
                    for ( uint32 part = 0; part < _partitionCount; ++part )
                    {
                        const uint32        slot     = ( _historyCursor + _partitionCount - part ) % _partitionCount;
                        const AudioComplex* pInput   = wet._listHistory.data() + static_cast<size_t>( slot ) * kFFTSize;
                        const AudioComplex* pImpulse = impulse.data() + static_cast<size_t>( part ) * kFFTSize;
                        for ( uint32 bin = 0; bin < kFFTSize; ++bin )
                        {
                            _listAccumulate[bin]._real += pInput[bin]._real * pImpulse[bin]._real - pInput[bin]._imaginary * pImpulse[bin]._imaginary;
                            _listAccumulate[bin]._imaginary += pInput[bin]._real * pImpulse[bin]._imaginary + pInput[bin]._imaginary * pImpulse[bin]._real;
                        }
                    }
                    _fft.inverse( _listAccumulate.data() );
                    for ( uint32 index = 0; index < kPartitionFrames; ++index )
                    {
                        wet._listOutput[index]   = _listAccumulate[index + kPartitionFrames]._real;
                        wet._listPrevious[index] = wet._listInput[index];
                    }
                }
                _historyCursor = ( _historyCursor + 1 ) % _partitionCount;
            }

            AudioFFT             _fft;                                            /**< 크기 `kFFTSize` 의 FFT 표입니다. */
            vector<AudioComplex> _arrImpulseSpectrum[audio::kStereoChannelCount]; /**< 응답 채널마다 조각 스펙트럼(조각 수 × FFT 크기)입니다. */
            vector<AudioComplex> _listWork;                                       /**< 입력 FFT 작업 버퍼입니다. */
            vector<AudioComplex> _listAccumulate;                                 /**< 조각 곱의 합입니다. */
            vector<WetChannel>   _listWet;                                        /**< 젖은 채널입니다(LFE 를 뺀 채널). */
            uint32               _partitionCount;                                 /**< 조각 수입니다. */
            uint32               _historyCursor;                                  /**< 주파수 영역 지연선에서 이번 블록의 자리입니다. */
            uint32               _fillIndex;                                      /**< 모으는 중인 블록에서 다음 프레임 자리입니다. */
            float32              _wet;                                            /**< wet 게인입니다. */
            float32              _dry;                                            /**< dry 게인입니다. */
        };

        struct AudioEffectInternal
        {
            static constexpr AudioEffectParameterInfo kArrLowPassParameter[] = {
                {"frequencyHz", 20000.0f,  10.0f, 20000.0f,  true},
                {          "q",  0.7071f,   0.1f,    20.0f, false},
                {     "gainDb",     0.0f, -24.0f,    24.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrHighPassParameter[] = {
                {"frequencyHz",   10.0f,  10.0f, 20000.0f,  true},
                {          "q", 0.7071f,   0.1f,    20.0f, false},
                {     "gainDb",    0.0f, -24.0f,    24.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrBandParameter[] = {
                {"frequencyHz", 1000.0f,  10.0f, 20000.0f,  true},
                {          "q", 0.7071f,   0.1f,    20.0f, false},
                {     "gainDb",    0.0f, -24.0f,    24.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrCompressorParameter[] = {
                {"thresholdDb", -18.0f, -60.0f,    0.0f, false},
                {      "ratio",   4.0f,   1.0f,  100.0f, false},
                {   "attackMs",  10.0f,  0.05f,  500.0f, false},
                {  "releaseMs", 120.0f,   1.0f, 5000.0f, false},
                {     "kneeDb",   6.0f,   0.0f,   24.0f, false},
                {   "makeupDb",   0.0f, -24.0f,   24.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrLimiterParameter[] = {
                {  "ceilingDb", -1.0f, -24.0f,    0.0f, false},
                {  "releaseMs", 60.0f,   1.0f, 2000.0f, false},
                {"lookaheadMs",  2.0f,   0.0f,   10.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrReverbParameter[] = {
                {  "roomSize",  0.7f, 0.0f,   1.0f, false},
                {   "damping",  0.5f, 0.0f,   1.0f, false},
                {       "wet", 0.33f, 0.0f,   1.0f, false},
                {       "dry",  0.0f, 0.0f,   1.0f, false},
                {     "width",  1.0f, 0.0f,   1.0f, false},
                {"preDelayMs",  0.0f, 0.0f, 190.0f, false},
            };
            static constexpr AudioEffectParameterInfo kArrDelayParameter[] = {
                {   "timeMs",   250.0f,   1.0f,  1990.0f, false},
                { "feedback",    0.35f,   0.0f,    0.95f, false},
                {      "wet",     0.5f,   0.0f,     1.0f, false},
                {      "dry",     1.0f,   0.0f,     1.0f, false},
                {"dampingHz", 20000.0f, 200.0f, 20000.0f,  true},
            };

            static constexpr AudioEffectParameterInfo kArrConvolutionParameter[] = {
                {"wet", 0.5f, 0.0f, 1.0f, false},
                {"dry", 0.0f, 0.0f, 1.0f, false},
            };

            template <AudioBiquadType TType>
            static unique_ptr<IAudioEffect> createFilter( const AudioEffectTypeInfo& typeInfo, const AudioEffectDesc& desc, AudioChannelLayout layout )
            {
                (void)desc;
                return make_unique<AudioFilterEffect>( typeInfo, layout, TType );
            }

            template <typename TEffect>
            static unique_ptr<IAudioEffect> createEffect( const AudioEffectTypeInfo& typeInfo, const AudioEffectDesc& desc, AudioChannelLayout layout )
            {
                (void)desc;
                return make_unique<TEffect>( typeInfo, layout );
            }

            /** @brief 컨볼루션 리버브를 만들고 데이터의 임펄스 응답을 읽습니다. 읽지 못하면 nullptr 입니다(오류는 `loadImpulse` 가 남깁니다). */
            static unique_ptr<IAudioEffect> createConvolution( const AudioEffectTypeInfo& typeInfo, const AudioEffectDesc& desc, AudioChannelLayout layout )
            {
                unique_ptr<AudioConvolutionEffect> pEffect = make_unique<AudioConvolutionEffect>( typeInfo, layout );
                if ( pEffect->loadImpulse( desc._impulse ) == false )
                    return nullptr;
                return pEffect;
            }

            /** @brief 모든 레이아웃(스테레오 · 5.1 · 7.1 · 바이노럴)입니다. 지금 종류는 모두 채널 수에 상관없이 처리합니다. */
            static constexpr uint8 kAllLayouts =
                AudioEffectTypeInfo::makeLayoutBit( AudioChannelLayout::Stereo ) | AudioEffectTypeInfo::makeLayoutBit( AudioChannelLayout::Surround51 ) |
                AudioEffectTypeInfo::makeLayoutBit( AudioChannelLayout::Surround71 ) | AudioEffectTypeInfo::makeLayoutBit( AudioChannelLayout::Binaural );

            /** @brief 이펙트 종류 표입니다. 이름은 데이터가 쓰는 철자이고, 마지막 칸은 처리할 수 있는 레이아웃입니다. */
            inline static const AudioEffectTypeInfo kArrEffectType[] = {
                {          "LowPass",     kArrLowPassParameter,     SW_COUNT_OF( kArrLowPassParameter ),   &createFilter<AudioBiquadType::LowPass>, kAllLayouts},
                {         "HighPass",    kArrHighPassParameter,    SW_COUNT_OF( kArrHighPassParameter ),  &createFilter<AudioBiquadType::HighPass>, kAllLayouts},
                {         "BandPass",        kArrBandParameter,        SW_COUNT_OF( kArrBandParameter ),  &createFilter<AudioBiquadType::BandPass>, kAllLayouts},
                {          "Peaking",        kArrBandParameter,        SW_COUNT_OF( kArrBandParameter ),   &createFilter<AudioBiquadType::Peaking>, kAllLayouts},
                {         "LowShelf",        kArrBandParameter,        SW_COUNT_OF( kArrBandParameter ),  &createFilter<AudioBiquadType::LowShelf>, kAllLayouts},
                {        "HighShelf",        kArrBandParameter,        SW_COUNT_OF( kArrBandParameter ), &createFilter<AudioBiquadType::HighShelf>, kAllLayouts},
                {       "Compressor",  kArrCompressorParameter,  SW_COUNT_OF( kArrCompressorParameter ),      &createEffect<AudioCompressorEffect>, kAllLayouts},
                {          "Limiter",     kArrLimiterParameter,     SW_COUNT_OF( kArrLimiterParameter ),         &createEffect<AudioLimiterEffect>, kAllLayouts},
                {           "Reverb",      kArrReverbParameter,      SW_COUNT_OF( kArrReverbParameter ),          &createEffect<AudioReverbEffect>, kAllLayouts},
                {            "Delay",       kArrDelayParameter,       SW_COUNT_OF( kArrDelayParameter ),           &createEffect<AudioDelayEffect>, kAllLayouts},
                {"ConvolutionReverb", kArrConvolutionParameter, SW_COUNT_OF( kArrConvolutionParameter ),                        &createConvolution, kAllLayouts},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    IAudioEffect::IAudioEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout )
        : _pTypeInfo{ &typeInfo }
        , _listParameter{}
        , _channelCount{ AudioChannelLayoutUtil::getChannelCount( layout ) }
        , _layout{ layout }
    {
        _listParameter.reserve( typeInfo._parameterCount );
        for ( uint32 parameterIndex = 0; parameterIndex < typeInfo._parameterCount; ++parameterIndex )
        {
            _listParameter.push_back( typeInfo._pParameter[parameterIndex]._defaultValue );
        }
    }

    int32 IAudioEffect::findParameterIndex( const hashed_string& name ) const
    {
        for ( uint32 parameterIndex = 0; parameterIndex < _pTypeInfo->_parameterCount; ++parameterIndex )
        {
            if ( name == hashed_string( _pTypeInfo->_pParameter[parameterIndex]._pName ) )
                return static_cast<int32>( parameterIndex );
        }
        return -1;
    }

    void IAudioEffect::setParameter( uint32 parameterIndex, float32 value )
    {
        const AudioEffectParameterInfo& info    = _pTypeInfo->_pParameter[parameterIndex];
        const float32                   clamped = MathUtil::clamp( value, info._minValue, info._maxValue );
        if ( _listParameter[parameterIndex] == clamped )
            return;
        _listParameter[parameterIndex] = clamped;
        onParameterChanged();
    }

    const AudioEffectTypeInfo* AudioEffectRegistry::findType( const hashed_string& name )
    {
        for ( const AudioEffectTypeInfo& typeInfo : AudioEffectInternal::kArrEffectType )
        {
            if ( name == hashed_string( typeInfo._pName ) )
                return &typeInfo;
        }
        return nullptr;
    }

    unique_ptr<IAudioEffect> AudioEffectRegistry::createEffect( const AudioEffectDesc& desc, AudioChannelLayout layout )
    {
        const AudioEffectTypeInfo* pTypeInfo = findType( desc._type );
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR( "Unknown audio effect type '%#'", desc._type.c_str() );
            return nullptr;
        }
        if ( pTypeInfo->supportsLayout( layout ) == false )
        {
            SW_LOG_ERROR( "Audio effect '%#' does not support %# output channels", desc._type.c_str(), AudioChannelLayoutUtil::getChannelCount( layout ) );
            return nullptr;
        }
        unique_ptr<IAudioEffect> pEffect = pTypeInfo->_pCreate( *pTypeInfo, desc, layout );
        if ( pEffect == nullptr )
            return nullptr;
        for ( const AudioEffectParameterDesc& parameter : desc._listParameter )
        {
            const int32 parameterIndex = pEffect->findParameterIndex( parameter._name );
            if ( parameterIndex < 0 )
            {
                SW_LOG_ERROR( "Audio effect '%#' has no parameter '%#'", desc._type.c_str(), parameter._name.c_str() );
                return nullptr;
            }
            pEffect->setParameter( static_cast<uint32>( parameterIndex ), parameter._value );
        }
        return pEffect;
    }

    const AudioEffectTypeInfo& AudioEffectRegistry::getType( uint32 typeIndex )
    {
        return AudioEffectInternal::kArrEffectType[typeIndex];
    }
} // namespace sw
