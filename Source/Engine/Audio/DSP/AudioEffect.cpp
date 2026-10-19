#include "pch.h"

#include "Engine/Audio/DSP/AudioEffect.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/DSP/AudioBiquad.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 바이쿼드 필터 하나로 이루어진 이펙트입니다. LowPass, HighPass, BandPass, Peaking, LowShelf, HighShelf 를 지원합니다. 파라미터는 frequencyHz, q, gainDb 입니다.
         * @details LowPass 의 컷오프가 `audio::kFilterOpenHz` 이상이면 거르지 않는다. 스냅샷이 "물속"에서만 내려 쓰는 필터를 평소에는 비용 없이 둘 수 있다.
         */
        class AudioFilterEffect final : public IAudioEffect
        {
        public:
            AudioFilterEffect( const AudioEffectTypeInfo& typeInfo, AudioBiquadType type )
                : IAudioEffect{ typeInfo }
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
            AudioBiquadStereo _filter;  /**< 필터 상태입니다. */
            AudioBiquadType   _type;    /**< 필터 형태입니다. */
            bool              _bBypass; /**< 걸러도 같은 신호라 건너뜁니다. */
        };

        /**
         * @brief 피드 포워드 컴프레서입니다(스테레오 링크, 피크 검출, 소프트 니). Giannoulis, Massberg, Reiss(2012)의 "smooth decoupled peak" 검출기를 씁니다.
         * @details 파라미터는 thresholdDb, ratio, attackMs, releaseMs, kneeDb, makeupDb 입니다. 게인 감소(dB)는 정상 상태에서 (입력 - 임계값) × (1 - 1/비율)입니다.
         */
        class AudioCompressorEffect final : public IAudioEffect
        {
        public:
            explicit AudioCompressorEffect( const AudioEffectTypeInfo& typeInfo )
                : IAudioEffect{ typeInfo }
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
                float32*      pLeft     = block.getChannel( 0 );
                float32*      pRight    = block.getChannel( 1 );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    float32&      left    = pLeft[frameIndex];
                    float32&      right   = pRight[frameIndex];
                    const float32 levelDb = AudioMath::linearToDb( MathUtil::max( MathUtil::abs( left ), MathUtil::abs( right ) ) );
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
                    left *= gain;
                    right *= gain;
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
         *          창 안의 모든 최솟값이 그 샘플의 필요 게인 이하이므로 평균도 이하입니다(넘침 없음, 클릭 없음).
         */
        class AudioLimiterEffect final : public IAudioEffect
        {
        public:
            explicit AudioLimiterEffect( const AudioEffectTypeInfo& typeInfo )
                : IAudioEffect{ typeInfo }
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
                float32*      pLeft   = block.getChannel( 0 );
                float32*      pRight  = block.getChannel( 1 );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    const float32 left     = pLeft[frameIndex];
                    const float32 right    = pRight[frameIndex];
                    const float32 peak     = MathUtil::max( MathUtil::abs( left ), MathUtil::abs( right ) );
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
                    const uint32  delaySlot    = static_cast<uint32>( index % window );
                    const size_t  delayBase    = static_cast<size_t>( delaySlot ) * audio::kStereoChannelCount;
                    const float32 delayedLeft  = _listDelay[delayBase];
                    const float32 delayedRight = _listDelay[delayBase + 1];
                    _listDelay[delayBase]      = left;
                    _listDelay[delayBase + 1]  = right;
                    const bool bDirect         = window == 1;
                    pLeft[frameIndex]          = ( bDirect ? left : delayedLeft ) * _gain;
                    pRight[frameIndex]         = ( bDirect ? right : delayedRight ) * _gain;
                    ++_sampleIndex;
                }
            }

            void reset() override
            {
                const size_t window = _windowLength;
                _listDelay.assign( window * audio::kStereoChannelCount, 0.0f );
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
            vector<float32> _listDelay;          /**< 늦춘 소리(내부 상태는 스테레오 교차, 창 길이)입니다. */
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
         * @brief Freeverb(Jezar) 리버브입니다. 채널마다 병렬 콤 필터 8 개(감쇠 로우패스)와 직렬 올패스 4 개를 쓰고, 오른쪽 채널은 23 샘플 벌립니다.
         * @details 파라미터는 roomSize, damping, wet, dry, width, preDelayMs 입니다. 지연 길이는 원본(44.1 kHz) 값을 출력 샘플레이트에 맞게 늘려 씁니다.
         */
        class AudioReverbEffect final : public IAudioEffect
        {
        public:
            explicit AudioReverbEffect( const AudioEffectTypeInfo& typeInfo )
                : IAudioEffect{ typeInfo }
                , _arrComb{}
                , _arrAllpass{}
                , _listPreDelay{}
                , _preDelayCursor{ 0 }
                , _preDelayLength{ 0 }
                , _feedback{ 0.0f }
                , _damp{ 0.0f }
                , _wetMain{ 0.0f }
                , _wetCross{ 0.0f }
                , _dry{ 0.0f }
            {
                static constexpr uint32 kArrCombTuning[kCombCount]       = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
                static constexpr uint32 kArrAllpassTuning[kAllpassCount] = { 556, 441, 341, 225 };
                static constexpr uint32 kStereoSpread                    = 23;
                for ( uint32 channel = 0; channel < audio::kStereoChannelCount; ++channel )
                {
                    for ( uint32 combIndex = 0; combIndex < kCombCount; ++combIndex )
                    {
                        _arrComb[channel][combIndex]._listBuffer.assign( scaleLength( kArrCombTuning[combIndex] + channel * kStereoSpread ), 0.0f );
                    }
                    for ( uint32 allpassIndex = 0; allpassIndex < kAllpassCount; ++allpassIndex )
                    {
                        _arrAllpass[channel][allpassIndex]._listBuffer.assign( scaleLength( kArrAllpassTuning[allpassIndex] + channel * kStereoSpread ), 0.0f );
                    }
                }
                _listPreDelay.assign( static_cast<size_t>( audio::kSampleRate / 5 + 1 ) * 2, 0.0f );
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                constexpr float32 kFixedGain    = 0.015f;
                const size_t      preDelaySlots = _listPreDelay.size() / 2;
                float32*          pLeft         = block.getChannel( 0 );
                float32*          pRight        = block.getChannel( 1 );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    const float32 inLeft  = pLeft[frameIndex];
                    const float32 inRight = pRight[frameIndex];
                    float32       input   = ( inLeft + inRight ) * kFixedGain;
                    if ( _preDelayLength > 0 )
                    {
                        const size_t  readSlot         = ( _preDelayCursor + preDelaySlots - _preDelayLength ) % preDelaySlots;
                        const float32 delayed          = _listPreDelay[readSlot];
                        _listPreDelay[_preDelayCursor] = input;
                        _preDelayCursor                = ( _preDelayCursor + 1 ) % preDelaySlots;
                        input                          = delayed;
                    }

                    float32 arrChannelOutput[audio::kStereoChannelCount] = { 0.0f, 0.0f };
                    for ( uint32 channel = 0; channel < audio::kStereoChannelCount; ++channel )
                    {
                        float32 sum = 0.0f;
                        for ( Comb& comb : _arrComb[channel] )
                        {
                            const float32 output           = comb._listBuffer[comb._cursor];
                            comb._filterStore              = output * ( 1.0f - _damp ) + comb._filterStore * _damp;
                            comb._listBuffer[comb._cursor] = input + comb._filterStore * _feedback;
                            comb._cursor                   = ( comb._cursor + 1 ) % static_cast<uint32>( comb._listBuffer.size() );
                            sum += output;
                        }
                        for ( Allpass& allpass : _arrAllpass[channel] )
                        {
                            const float32 buffered               = allpass._listBuffer[allpass._cursor];
                            const float32 output                 = buffered - sum;
                            allpass._listBuffer[allpass._cursor] = sum + buffered * 0.5f;
                            allpass._cursor                      = ( allpass._cursor + 1 ) % static_cast<uint32>( allpass._listBuffer.size() );
                            sum                                  = output;
                        }
                        arrChannelOutput[channel] = sum;
                    }
                    pLeft[frameIndex]  = arrChannelOutput[0] * _wetMain + arrChannelOutput[1] * _wetCross + inLeft * _dry;
                    pRight[frameIndex] = arrChannelOutput[1] * _wetMain + arrChannelOutput[0] * _wetCross + inRight * _dry;
                }
            }

            void reset() override
            {
                for ( uint32 channel = 0; channel < audio::kStereoChannelCount; ++channel )
                {
                    for ( Comb& comb : _arrComb[channel] )
                    {
                        comb._listBuffer.assign( comb._listBuffer.size(), 0.0f );
                        comb._filterStore = 0.0f;
                    }
                    for ( Allpass& allpass : _arrAllpass[channel] )
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

            Comb            _arrComb[audio::kStereoChannelCount][kCombCount];       /**< 채널마다 콤 필터입니다. */
            Allpass         _arrAllpass[audio::kStereoChannelCount][kAllpassCount]; /**< 채널마다 올패스입니다. */
            vector<float32> _listPreDelay;                                          /**< 프리딜레이(모노 입력, 0.2 초)입니다. */
            size_t          _preDelayCursor;                                        /**< 프리딜레이 쓰기 위치입니다. */
            size_t          _preDelayLength;                                        /**< 프리딜레이 길이(샘플)입니다. */
            float32         _feedback;                                              /**< 콤 피드백입니다. */
            float32         _damp;                                                  /**< 콤 안 로우패스 계수입니다. */
            float32         _wetMain;                                               /**< 같은 쪽 wet 게인입니다. */
            float32         _wetCross;                                              /**< 반대쪽 wet 게인입니다(폭이 좁을수록 크다). */
            float32         _dry;                                                   /**< dry 게인입니다. */
        };

        /**
         * @brief 피드백 딜레이(에코)입니다. 파라미터는 timeMs, feedback, wet, dry, dampingHz 입니다. dampingHz 는 피드백 경로의 한 극 로우패스이며, 20 kHz 이상이면 쓰지 않습니다.
         * @details 첫 에코는 timeMs 뒤에 wet 크기로 오고, 다음 에코는 2·timeMs 뒤에 wet·feedback 크기로 온다.
         */
        class AudioDelayEffect final : public IAudioEffect
        {
        public:
            explicit AudioDelayEffect( const AudioEffectTypeInfo& typeInfo )
                : IAudioEffect{ typeInfo }
                , _listBuffer{}
                , _cursor{ 0 }
                , _delayFrames{ 1 }
                , _arrDampState{ 0.0f, 0.0f }
                , _dampCoefficient{ 0.0f }
            {
                _listBuffer.assign( static_cast<size_t>( audio::kSampleRate ) * 2 * audio::kStereoChannelCount, 0.0f );
                onParameterChanged();
            }

            void process( const AudioBlockView& block ) override
            {
                constexpr uint32 kChannelCount = audio::kStereoChannelCount;
                const float32    feedback      = _listParameter[1];
                const float32    wet           = _listParameter[2];
                const float32    dry           = _listParameter[3];
                const uint32     slotCount     = static_cast<uint32>( _listBuffer.size() / kChannelCount );
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    const uint32 readSlot = ( _cursor + slotCount - _delayFrames ) % slotCount;
                    for ( uint32 channel = 0; channel < kChannelCount; ++channel )
                    {
                        float32*      pChannel                         = block.getChannel( channel );
                        const float32 input                            = pChannel[frameIndex];
                        const float32 delayed                          = _listBuffer[readSlot * kChannelCount + channel];
                        _arrDampState[channel]                         = delayed + ( _arrDampState[channel] - delayed ) * _dampCoefficient;
                        _listBuffer[_cursor * kChannelCount + channel] = input + _arrDampState[channel] * feedback;
                        pChannel[frameIndex]                           = input * dry + delayed * wet;
                    }
                    _cursor = ( _cursor + 1 ) % slotCount;
                }
            }

            void reset() override
            {
                _listBuffer.assign( _listBuffer.size(), 0.0f );
                _arrDampState[0] = 0.0f;
                _arrDampState[1] = 0.0f;
            }

        protected:
            void onParameterChanged() override
            {
                const uint32 slotCount = static_cast<uint32>( _listBuffer.size() / audio::kStereoChannelCount );
                _delayFrames           = MathUtil::clamp( static_cast<uint32>( _listParameter[0] * 0.001f * static_cast<float32>( audio::kSampleRate ) ), 1u, slotCount - 1 );
                const float32 cutoff   = _listParameter[4];
                _dampCoefficient       = cutoff >= audio::kFilterOpenHz ? 0.0f
                                                                        : MathUtil::pow( MathUtil::kEuler, -2.0f * MathUtil::kPi * cutoff / static_cast<float32>( audio::kSampleRate ) );
            }

        private:
            vector<float32> _listBuffer;                               /**< 지연선(스테레오 교차, 2 초)입니다. */
            uint32          _cursor;                                   /**< 쓰기 위치입니다. */
            uint32          _delayFrames;                              /**< 지연(프레임)입니다. */
            float32         _arrDampState[audio::kStereoChannelCount]; /**< 피드백 로우패스 상태입니다. */
            float32         _dampCoefficient;                          /**< 피드백 로우패스 계수입니다(0 이면 없음). */
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

            template <AudioBiquadType TType>
            static unique_ptr<IAudioEffect> createFilter( const AudioEffectTypeInfo& typeInfo )
            {
                return make_unique<AudioFilterEffect>( typeInfo, TType );
            }

            template <typename TEffect>
            static unique_ptr<IAudioEffect> createEffect( const AudioEffectTypeInfo& typeInfo )
            {
                return make_unique<TEffect>( typeInfo );
            }

            /** @brief 이펙트 종류 표입니다. 이름은 데이터가 쓰는 철자입니다. */
            inline static const AudioEffectTypeInfo kArrEffectType[] = {
                {   "LowPass",    kArrLowPassParameter,    SW_COUNT_OF( kArrLowPassParameter ),   &createFilter<AudioBiquadType::LowPass>},
                {  "HighPass",   kArrHighPassParameter,   SW_COUNT_OF( kArrHighPassParameter ),  &createFilter<AudioBiquadType::HighPass>},
                {  "BandPass",       kArrBandParameter,       SW_COUNT_OF( kArrBandParameter ),  &createFilter<AudioBiquadType::BandPass>},
                {   "Peaking",       kArrBandParameter,       SW_COUNT_OF( kArrBandParameter ),   &createFilter<AudioBiquadType::Peaking>},
                {  "LowShelf",       kArrBandParameter,       SW_COUNT_OF( kArrBandParameter ),  &createFilter<AudioBiquadType::LowShelf>},
                { "HighShelf",       kArrBandParameter,       SW_COUNT_OF( kArrBandParameter ), &createFilter<AudioBiquadType::HighShelf>},
                {"Compressor", kArrCompressorParameter, SW_COUNT_OF( kArrCompressorParameter ),      &createEffect<AudioCompressorEffect>},
                {   "Limiter",    kArrLimiterParameter,    SW_COUNT_OF( kArrLimiterParameter ),         &createEffect<AudioLimiterEffect>},
                {    "Reverb",     kArrReverbParameter,     SW_COUNT_OF( kArrReverbParameter ),          &createEffect<AudioReverbEffect>},
                {     "Delay",      kArrDelayParameter,      SW_COUNT_OF( kArrDelayParameter ),           &createEffect<AudioDelayEffect>},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    IAudioEffect::IAudioEffect( const AudioEffectTypeInfo& typeInfo )
        : _pTypeInfo{ &typeInfo }
        , _listParameter{}
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

    unique_ptr<IAudioEffect> AudioEffectRegistry::createEffect( const hashed_string& name )
    {
        const AudioEffectTypeInfo* pTypeInfo = findType( name );
        return pTypeInfo == nullptr ? nullptr : pTypeInfo->_pCreate( *pTypeInfo );
    }

    uint32 AudioEffectRegistry::getTypeCount()
    {
        return static_cast<uint32>( sizeof( AudioEffectInternal::kArrEffectType ) / sizeof( AudioEffectInternal::kArrEffectType[0] ) );
    }

    const AudioEffectTypeInfo& AudioEffectRegistry::getType( uint32 typeIndex )
    {
        return AudioEffectInternal::kArrEffectType[typeIndex];
    }
} // namespace sw
