#include "pch.h"

#include "Engine/Audio/Graph/AudioGraph.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioClip.h"

namespace sw
{
    SW_LOG_CALLER( "AudioGraph" );

    namespace
    {
        struct AudioGraphInternal
        {
            // 값 번호는 아래 표의 순서다. 렌더 코드가 번호로 읽으므로 표와 함께 바꾼다.
            static constexpr uint32 kOscillatorFrequency = 0;
            static constexpr uint32 kOscillatorAmplitude = 1;
            static constexpr uint32 kNoiseAmplitude      = 0;
            static constexpr uint32 kGrainPosition       = 0;
            static constexpr uint32 kGrainPositionJitter = 1;
            static constexpr uint32 kGrainLength         = 2;
            static constexpr uint32 kGrainRate           = 3;
            static constexpr uint32 kGrainPitch          = 4;
            static constexpr uint32 kGrainPitchJitter    = 5;
            static constexpr uint32 kGrainAmplitude      = 6;
            static constexpr uint32 kEnvelopeAttack      = 0;
            static constexpr uint32 kEnvelopeDecay       = 1;
            static constexpr uint32 kEnvelopeSustain     = 2;
            static constexpr uint32 kEnvelopeRelease     = 3;
            static constexpr uint32 kFilterFrequency     = 0;
            static constexpr uint32 kFilterQ             = 1;
            static constexpr uint32 kGainVolume          = 0;

            /** @brief 알갱이 하나의 가장 짧은 길이(출력 프레임)입니다. 창이 0 으로 나뉘지 않게 한다. */
            static constexpr uint32 kMinGrainFrames = 2;

            static constexpr AudioGraphParameterInfo kArrOscillatorParameter[] = {
                {"frequencyHz", 440.0f, 1.0f, 20000.0f},
                {  "amplitude",   1.0f, 0.0f,     1.0f},
            };
            static constexpr AudioGraphParameterInfo kArrNoiseParameter[] = {
                { "amplitude", 1.0f, 0.0f, 1.0f },
            };
            static constexpr AudioGraphParameterInfo kArrGranularParameter[] = {
                {      "positionSeconds",  0.0f,   0.0f, 3600.0f},
                {"positionJitterSeconds",  0.0f,   0.0f,   60.0f},
                {         "grainSeconds", 0.08f, 0.005f,    1.0f},
                {      "grainsPerSecond", 20.0f,   0.5f,  400.0f},
                {       "pitchSemitones",  0.0f, -48.0f,   48.0f},
                { "pitchJitterSemitones",  0.0f,   0.0f,   24.0f},
                {            "amplitude",  1.0f,   0.0f,    4.0f},
            };
            static constexpr AudioGraphParameterInfo kArrEnvelopeParameter[] = {
                { "attackSeconds", 0.01f, 0.0f, 30.0f},
                {  "decaySeconds",  0.1f, 0.0f, 30.0f},
                {  "sustainLevel",  1.0f, 0.0f,  1.0f},
                {"releaseSeconds",  0.1f, 0.0f, 30.0f},
            };
            static constexpr AudioGraphParameterInfo kArrFilterParameter[] = {
                {"frequencyHz", 1000.0f, 20.0f, 20000.0f},
                {          "q", 0.7071f,  0.1f,    20.0f},
            };
            static constexpr AudioGraphParameterInfo kArrGainParameter[] = {
                { "volumeDb", 0.0f, -96.0f, 24.0f },
            };

            /** @brief 종류 표입니다. `AudioGraphNodeType` 의 순서와 같다(아래 static_assert). */
            static constexpr AudioGraphNodeTypeInfo kArrNodeType[] = {
                {    "Sine", kArrOscillatorParameter, SW_COUNT_OF( kArrOscillatorParameter ),  true, false},
                {     "Saw", kArrOscillatorParameter, SW_COUNT_OF( kArrOscillatorParameter ),  true, false},
                {  "Square", kArrOscillatorParameter, SW_COUNT_OF( kArrOscillatorParameter ),  true, false},
                {"Triangle", kArrOscillatorParameter, SW_COUNT_OF( kArrOscillatorParameter ),  true, false},
                {   "Noise",      kArrNoiseParameter,      SW_COUNT_OF( kArrNoiseParameter ),  true, false},
                {"Granular",   kArrGranularParameter,   SW_COUNT_OF( kArrGranularParameter ),  true,  true},
                {"Envelope",   kArrEnvelopeParameter,   SW_COUNT_OF( kArrEnvelopeParameter ), false, false},
                { "LowPass",     kArrFilterParameter,     SW_COUNT_OF( kArrFilterParameter ), false, false},
                {"HighPass",     kArrFilterParameter,     SW_COUNT_OF( kArrFilterParameter ), false, false},
                {"BandPass",     kArrFilterParameter,     SW_COUNT_OF( kArrFilterParameter ), false, false},
                {    "Gain",       kArrGainParameter,       SW_COUNT_OF( kArrGainParameter ), false, false},
            };
            static_assert( SW_COUNT_OF( kArrNodeType ) == static_cast<size_t>( AudioGraphNodeType::Gain ) + 1, "kArrNodeType must list every AudioGraphNodeType in order" );
            static_assert( SW_COUNT_OF( kArrGranularParameter ) <= AudioGraphRegistry::kMaxParameterCount, "a node type declares more values than a node can hold" );

            /** @brief PolyBLEP 보정입니다(위상 @p phase, 한 프레임의 위상 증가 @p step). 불연속 근처에서만 0 이 아니다. */
            static float32 computePolyBlep( float64 phase, float64 step )
            {
                if ( phase < step )
                {
                    const float64 t = phase / step;
                    return static_cast<float32>( t + t - t * t - 1.0 );
                }
                if ( phase > 1.0 - step )
                {
                    const float64 t = ( phase - 1.0 ) / step;
                    return static_cast<float32>( t * t + t + t + 1.0 );
                }
                return 0.0f;
            }

            /** @brief 오실레이터 하나의 샘플입니다(위상 [0, 1)). */
            static float32 computeOscillator( AudioGraphNodeType type, float64 phase, float64 step )
            {
                switch ( type )
                {
                    case AudioGraphNodeType::Saw:
                    {
                        return static_cast<float32>( phase + phase - 1.0 ) - computePolyBlep( phase, step );
                    }
                    case AudioGraphNodeType::Square:
                    {
                        const float64 shifted = phase + 0.5 < 1.0 ? phase + 0.5 : phase - 0.5;
                        const float32 naive   = phase < 0.5 ? 1.0f : -1.0f;
                        return naive + computePolyBlep( phase, step ) - computePolyBlep( shifted, step );
                    }
                    case AudioGraphNodeType::Triangle:
                    {
                        return static_cast<float32>( 4.0 * MathUtil::abs( phase - 0.5 ) - 1.0 );
                    }
                    default:
                    {
                        return static_cast<float32>( MathUtil::sin( 2.0 * MathUtil::kPi64 * phase ) );
                    }
                }
            }

            /** @brief 그래프 시각 @p seconds 의 ADSR 값입니다. 릴리스는 길이 @p durationSeconds 의 끝에서 0 이 되게 곱한다. */
            static float32 computeEnvelope( const float32* pParameter, float32 seconds, float32 durationSeconds )
            {
                const float32 attack  = pParameter[kEnvelopeAttack];
                const float32 decay   = pParameter[kEnvelopeDecay];
                const float32 sustain = pParameter[kEnvelopeSustain];
                const float32 release = pParameter[kEnvelopeRelease];
                float32       level   = sustain;
                if ( seconds < attack )
                    level = seconds / attack;
                else if ( seconds < attack + decay )
                    level = 1.0f - ( 1.0f - sustain ) * ( seconds - attack ) / decay;
                if ( release <= 0.0f )
                    return level;
                return level * MathUtil::saturate( ( durationSeconds - seconds ) / release );
            }

            /** @brief 필터 노드 종류의 바이쿼드 형태입니다. */
            static AudioBiquadType getBiquadType( AudioGraphNodeType type )
            {
                if ( type == AudioGraphNodeType::HighPass )
                    return AudioBiquadType::HighPass;
                if ( type == AudioGraphNodeType::BandPass )
                    return AudioBiquadType::BandPass;
                return AudioBiquadType::LowPass;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const AudioGraphNodeTypeInfo& AudioGraphRegistry::getNodeType( AudioGraphNodeType type )
    {
        return AudioGraphInternal::kArrNodeType[static_cast<size_t>( type )];
    }

    int32 AudioGraphRegistry::findParameterIndex( const AudioGraphNodeTypeInfo& typeInfo, const hashed_string& name )
    {
        for ( uint32 parameterIndex = 0; parameterIndex < typeInfo._parameterCount; ++parameterIndex )
        {
            if ( name == hashed_string( typeInfo._pParameter[parameterIndex]._pName ) )
                return static_cast<int32>( parameterIndex );
        }
        return -1;
    }
} // namespace sw

namespace sw
{
    shared_ptr<const AudioGraph> AudioGraph::create( const AudioGraphDef& def, const vector<shared_ptr<const AudioClipData>>& listNodeClip, string_view sourceName )
    {
        vector<uint32> listOrder;
        const bool     bReady = def.validate( sourceName ) && listNodeClip.size() == def._listNode.size() && def.makeProcessingOrder( listOrder );
        if ( bReady == false )
        {
            SW_LOG_ERROR( "%#: audio graph cannot be built", sourceName );
            return nullptr;
        }

        // 데이터 번호 → 처리 순서 번호.
        vector<uint32> listDataToOrder( def._listNode.size(), 0 );
        for ( uint32 orderIndex = 0; orderIndex < listOrder.size(); ++orderIndex )
        {
            listDataToOrder[listOrder[orderIndex]] = orderIndex;
        }

        shared_ptr<AudioGraph> pGraph = make_shared<AudioGraph>();
        pGraph->_frameCount           = static_cast<uint32>( def._durationSeconds * static_cast<float32>( audio::kSampleRate ) );
        pGraph->_outputIndex          = listDataToOrder[static_cast<size_t>( def.findNodeIndex( def._output ) )];
        pGraph->_listNode.resize( listOrder.size() );
        for ( uint32 orderIndex = 0; orderIndex < listOrder.size(); ++orderIndex )
        {
            const uint32                  dataIndex = listOrder[orderIndex];
            const AudioGraphNodeDesc&     desc      = def._listNode[dataIndex];
            const AudioGraphNodeTypeInfo& typeInfo  = AudioGraphRegistry::getNodeType( desc._type );
            Node&                         node      = pGraph->_listNode[orderIndex];
            node._type                              = desc._type;
            for ( uint32 parameterIndex = 0; parameterIndex < typeInfo._parameterCount; ++parameterIndex )
            {
                node._arrParameter[parameterIndex] = typeInfo._pParameter[parameterIndex]._defaultValue;
            }
            for ( const hashed_string& input : desc._listInput )
            {
                node._listInput.push_back( listDataToOrder[static_cast<size_t>( def.findNodeIndex( input ) )] );
            }
            for ( const AudioGraphParameterDesc& parameterDesc : desc._listParameter )
            {
                const uint32                   parameterIndex = static_cast<uint32>( AudioGraphRegistry::findParameterIndex( typeInfo, parameterDesc._name ) );
                const AudioGraphParameterInfo& info           = typeInfo._pParameter[parameterIndex];
                node._arrParameter[parameterIndex]            = MathUtil::clamp( parameterDesc._value, info._minValue, info._maxValue );
                if ( parameterDesc._parameter.empty() )
                    continue;
                Binding binding;
                binding._listPoint      = parameterDesc._listPoint;
                binding._nodeIndex      = orderIndex;
                binding._parameterIndex = parameterIndex;
                binding._inputIndex     = static_cast<uint32>( pGraph->_listParameterName.size() );
                for ( uint32 nameIndex = 0; nameIndex < pGraph->_listParameterName.size(); ++nameIndex )
                {
                    if ( pGraph->_listParameterName[nameIndex] == parameterDesc._parameter )
                        binding._inputIndex = nameIndex;
                }
                if ( binding._inputIndex == pGraph->_listParameterName.size() )
                    pGraph->_listParameterName.push_back( parameterDesc._parameter );
                pGraph->_listBinding.push_back( std::move( binding ) );
            }
            if ( typeInfo._bNeedsClip == false )
                continue;
            // 그래뉼러는 아무 자리나 바로 읽어야 하므로 메모리에 푼 클립만 받는다(스트리밍 · 그래프 클립은 오류).
            const shared_ptr<const AudioClipData>& pClip       = listNodeClip[dataIndex];
            const bool                             bUsableClip = pClip != nullptr && pClip->isStreamed() == false && pClip->isGraph() == false && pClip->_frameCount > 1;
            if ( bUsableClip == false )
            {
                SW_LOG_ERROR( "%#: granular node '%#' needs an in-memory clip of two frames or more ('%#')", sourceName, desc._name.c_str(), desc._clip.c_str() );
                return nullptr;
            }
            pClip->copyMonoSamples( node._listSourceSample );
            node._sourceRate = static_cast<float64>( pClip->_sampleRate ) / static_cast<float64>( audio::kSampleRate );
        }
        return pGraph;
    }
} // namespace sw

namespace sw
{
    AudioGraphInstance::AudioGraphInstance( shared_ptr<const AudioGraph> pGraph, uint64 randomSeed )
        : _pGraph{ std::move( pGraph ) }
        , _listBuffer{}
        , _listBlockParameter{}
        , _listParameterValue{}
        , _listState{}
        , _random{}
        , _randomSeed{ randomSeed }
        , _frame{ 0 }
    {
        const size_t nodeCount = _pGraph->getNodes().size();
        _listBuffer.assign( nodeCount * audio::kBlockFrameCount, 0.0f );
        _listBlockParameter.assign( nodeCount * AudioGraphRegistry::kMaxParameterCount, 0.0f );
        _listParameterValue.assign( _pGraph->getParameterNames().size(), 0.0f );
        _listState.resize( nodeCount );
        resetState();
    }

    void AudioGraphInstance::setParameterValue( uint32 inputIndex, float32 value )
    {
        SW_ASSERT( inputIndex < _listParameterValue.size() );
        _listParameterValue[inputIndex] = value;
    }

    void AudioGraphInstance::resetState()
    {
        for ( NodeState& state : _listState )
        {
            state = NodeState{};
        }
        _random._state = _randomSeed;
    }

    bool AudioGraphInstance::seekToFrame( uint32 frame )
    {
        if ( frame >= _pGraph->getFrameCount() )
            return false;
        resetState();
        _frame = frame;
        return true;
    }

    uint32 AudioGraphInstance::readFrames( float32* pOut, uint32 frameCount )
    {
        const uint32 remaining = _pGraph->getFrameCount() - _frame;
        const uint32 total     = MathUtil::min( frameCount, remaining );
        uint32       written   = 0;
        while ( written < total )
        {
            const uint32 chunk = MathUtil::min( total - written, audio::kBlockFrameCount );
            renderBlock( pOut + written, chunk );
            written += chunk;
        }
        return total;
    }

    void AudioGraphInstance::sumInputs( uint32 nodeIndex, float32* pOut, uint32 frameCount ) const
    {
        Memory::set( pOut, 0, static_cast<size_t>( frameCount ) * sizeof( float32 ) );
        for ( const uint32 inputIndex : _pGraph->getNodes()[nodeIndex]._listInput )
        {
            const float32* pInput = _listBuffer.data() + static_cast<size_t>( inputIndex ) * audio::kBlockFrameCount;
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                pOut[frameIndex] += pInput[frameIndex];
            }
        }
    }

    void AudioGraphInstance::updateBlockParameters()
    {
        const vector<AudioGraph::Node>& listNode = _pGraph->getNodes();
        for ( uint32 nodeIndex = 0; nodeIndex < listNode.size(); ++nodeIndex )
        {
            Memory::copy( getBlockParameters( nodeIndex ), listNode[nodeIndex]._arrParameter, sizeof( listNode[nodeIndex]._arrParameter ) );
        }
        for ( const AudioGraph::Binding& binding : _pGraph->getBindings() )
        {
            const AudioGraph::Node&        node                               = listNode[binding._nodeIndex];
            const AudioGraphParameterInfo& info                               = AudioGraphRegistry::getNodeType( node._type )._pParameter[binding._parameterIndex];
            const float32                  value                              = AudioCurvePoint::evaluate( binding._listPoint, _listParameterValue[binding._inputIndex], info._defaultValue );
            getBlockParameters( binding._nodeIndex )[binding._parameterIndex] = MathUtil::clamp( value, info._minValue, info._maxValue );
        }
    }

    void AudioGraphInstance::renderOscillator( const AudioGraph::Node& node, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount )
    {
        using Internal          = AudioGraphInternal;
        const float64 step      = static_cast<float64>( pParameter[Internal::kOscillatorFrequency] ) / static_cast<float64>( audio::kSampleRate );
        const float32 amplitude = pParameter[Internal::kOscillatorAmplitude];
        float64       phase     = inoutState._phase;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            pOut[frameIndex] = amplitude * Internal::computeOscillator( node._type, phase, step );
            phase += step;
            if ( phase >= 1.0 )
                phase -= MathUtil::floor( phase );
        }
        inoutState._phase = phase;
    }

    void AudioGraphInstance::renderEnvelope( uint32 nodeIndex, const float32* pParameter, float32* pOut, uint32 frameCount )
    {
        sumInputs( nodeIndex, pOut, frameCount );
        const float32 sampleRate      = static_cast<float32>( audio::kSampleRate );
        const float32 durationSeconds = static_cast<float32>( _pGraph->getFrameCount() ) / sampleRate;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const float32 seconds = static_cast<float32>( _frame + frameIndex ) / sampleRate;
            pOut[frameIndex] *= AudioGraphInternal::computeEnvelope( pParameter, seconds, durationSeconds );
        }
    }

    void AudioGraphInstance::renderFilter( uint32 nodeIndex, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount )
    {
        using Internal = AudioGraphInternal;
        sumInputs( nodeIndex, pOut, frameCount );
        const float32 frequencyHz      = pParameter[Internal::kFilterFrequency];
        const float32 q                = pParameter[Internal::kFilterQ];
        const bool    bCoefficientsOld = inoutState._filterHz != frequencyHz || inoutState._filterQ != q;
        if ( bCoefficientsOld )
        {
            inoutState._filter._coefficients = AudioBiquadCoefficients::make( Internal::getBiquadType( _pGraph->getNodes()[nodeIndex]._type ), frequencyHz, q, 0.0f, static_cast<float32>( audio::kSampleRate ) );
            inoutState._filterHz             = frequencyHz;
            inoutState._filterQ              = q;
        }
        AudioBlockView view;
        view._pData        = pOut;
        view._channelCount = 1;
        view._frameCount   = frameCount;
        inoutState._filter.process( view );
    }

    void AudioGraphInstance::renderGain( uint32 nodeIndex, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount )
    {
        sumInputs( nodeIndex, pOut, frameCount );
        const float32 targetGain = AudioMath::dbToLinear( pParameter[AudioGraphInternal::kGainVolume] );
        const float32 startGain  = inoutState._bFirstBlock ? targetGain : inoutState._gain;
        const float32 step       = ( targetGain - startGain ) / static_cast<float32>( frameCount );
        float32       gain       = startGain;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            gain += step;
            pOut[frameIndex] *= gain;
        }
        inoutState._gain = targetGain;
    }

    void AudioGraphInstance::renderBlock( float32* pOut, uint32 frameCount )
    {
        const vector<AudioGraph::Node>& listNode = _pGraph->getNodes();
        updateBlockParameters();

        for ( uint32 nodeIndex = 0; nodeIndex < listNode.size(); ++nodeIndex )
        {
            const AudioGraph::Node& node       = listNode[nodeIndex];
            NodeState&              state      = _listState[nodeIndex];
            float32*                pParameter = getBlockParameters( nodeIndex );
            float32*                pBuffer    = getNodeBuffer( nodeIndex );
            switch ( node._type )
            {
                case AudioGraphNodeType::Sine:
                case AudioGraphNodeType::Saw:
                case AudioGraphNodeType::Square:
                case AudioGraphNodeType::Triangle:
                {
                    renderOscillator( node, state, pParameter, pBuffer, frameCount );
                    break;
                }
                case AudioGraphNodeType::Noise:
                {
                    const float32 amplitude = pParameter[AudioGraphInternal::kNoiseAmplitude];
                    for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                    {
                        pBuffer[frameIndex] = amplitude * ( _random.nextUnit() * 2.0f - 1.0f );
                    }
                    break;
                }
                case AudioGraphNodeType::Granular:
                {
                    renderGranular( nodeIndex, pParameter, pBuffer, frameCount );
                    break;
                }
                case AudioGraphNodeType::Envelope:
                {
                    renderEnvelope( nodeIndex, pParameter, pBuffer, frameCount );
                    break;
                }
                case AudioGraphNodeType::LowPass:
                case AudioGraphNodeType::HighPass:
                case AudioGraphNodeType::BandPass:
                {
                    renderFilter( nodeIndex, state, pParameter, pBuffer, frameCount );
                    break;
                }
                case AudioGraphNodeType::Gain:
                {
                    renderGain( nodeIndex, state, pParameter, pBuffer, frameCount );
                    break;
                }
            }
            state._bFirstBlock = false;
        }

        Memory::copy( pOut, getNodeBuffer( _pGraph->getOutputIndex() ), static_cast<size_t>( frameCount ) * sizeof( float32 ) );
        _frame += frameCount;
    }

    void AudioGraphInstance::renderGranular( uint32 nodeIndex, const float32* pParameter, float32* pOut, uint32 frameCount )
    {
        using Internal                           = AudioGraphInternal;
        const AudioGraph::Node& node             = _pGraph->getNodes()[nodeIndex];
        NodeState&              state            = _listState[nodeIndex];
        const vector<float32>&  listSample       = node._listSourceSample;
        const float64           lastFrame        = static_cast<float64>( listSample.size() - 1 );
        const float64           sourceSampleRate = node._sourceRate * static_cast<float64>( audio::kSampleRate );
        const float64           spawnInterval    = static_cast<float64>( audio::kSampleRate ) / static_cast<float64>( pParameter[Internal::kGrainRate] );
        const uint32            lengthFrames     = MathUtil::max( Internal::kMinGrainFrames, static_cast<uint32>( pParameter[Internal::kGrainLength] * static_cast<float32>( audio::kSampleRate ) ) );
        const float32           amplitude        = pParameter[Internal::kGrainAmplitude];
        const float32           invLength        = 1.0f / static_cast<float32>( lengthFrames );
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            // 알갱이를 낼 때가 되면 빈 칸 하나에 위치 · 피치를 흔들어 띄운다. 빈 칸이 없으면 이번 알갱이는 건너뛴다(상한 `kMaxGrainCount`).
            state._framesToNextGrain -= 1.0;
            if ( state._framesToNextGrain <= 0.0 )
            {
                state._framesToNextGrain += spawnInterval;
                const float32 positionSeconds = pParameter[Internal::kGrainPosition] + pParameter[Internal::kGrainPositionJitter] * ( _random.nextUnit() * 2.0f - 1.0f );
                const float32 pitchSemitones  = pParameter[Internal::kGrainPitch] + pParameter[Internal::kGrainPitchJitter] * ( _random.nextUnit() * 2.0f - 1.0f );
                for ( Grain& grain : state._arrGrain )
                {
                    if ( grain._lengthFrames != 0 )
                        continue;
                    grain._position     = MathUtil::clamp( static_cast<float64>( positionSeconds ) * sourceSampleRate, 0.0, lastFrame );
                    grain._step         = node._sourceRate * static_cast<float64>( AudioMath::semitonesToRatio( pitchSemitones ) );
                    grain._ageFrames    = 0;
                    grain._lengthFrames = lengthFrames;
                    break;
                }
            }

            float32 sum = 0.0f;
            for ( Grain& grain : state._arrGrain )
            {
                if ( grain._lengthFrames == 0 )
                    continue;
                // 소스 끝에 닿은 알갱이는 끝난다(보간이 다음 프레임을 읽으므로 마지막 프레임 앞까지만 읽는다).
                if ( grain._position >= lastFrame )
                {
                    grain._lengthFrames = 0;
                    continue;
                }
                // 한 창(Hann)으로 알갱이 앞뒤를 0 에서 오르내린다.
                const uint32  index    = static_cast<uint32>( grain._position );
                const float32 fraction = static_cast<float32>( grain._position - static_cast<float64>( index ) );
                const float32 sample   = listSample[index] + ( listSample[index + 1] - listSample[index] ) * fraction;
                const float32 window   = 0.5f - 0.5f * MathUtil::cos( MathUtil::kTwoPi * static_cast<float32>( grain._ageFrames ) * invLength );
                sum += sample * window;
                grain._position += grain._step;
                ++grain._ageFrames;
                if ( grain._ageFrames >= grain._lengthFrames )
                    grain._lengthFrames = 0;
            }
            pOut[frameIndex] = amplitude * sum;
        }
    }
} // namespace sw
