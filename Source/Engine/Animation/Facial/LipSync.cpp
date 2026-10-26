#include "pch.h"

#include "Engine/Animation/Facial/LipSync.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LipSync" );

    namespace
    {
        struct LipSyncInternal
        {
            /// 포먼트를 측정할 때 솎아 낼 목표 표본율입니다(포먼트는 4 kHz 아래라 그 두 배 남짓이면 된다).
            static constexpr uint32 kFormantTargetRate = 16000;
            /// LPC 차수의 상한입니다(표본율 kHz + 2 가 관례 — 16 kHz 면 18).
            static constexpr uint32 kMaxLpcOrder = 24;
            /// 포락선을 훑는 범위와 간격(Hz)입니다.
            static constexpr float32 kFormantMinHz  = 150.0f;
            static constexpr float32 kFormantMaxHz  = 4000.0f;
            static constexpr float32 kFormantStepHz = 20.0f;
            /// 프리엠퍼시스 계수입니다(고역을 올려 둘째 포먼트가 묻히지 않게).
            static constexpr float32 kPreEmphasis = 0.97f;
            /// 파열음 터짐을 측정하는 창(초)입니다 — 터짐은 수 ms 안에 오르고 모음의 시작은 수십 ms 에 걸쳐 오른다. 성대 한 주기(남성 약 8 ms)보다 길어야
            /// 유성음의 주기마다 출렁이는 에너지를 터짐으로 보지 않는다(2 ms 창은 합성 모음의 첫 프레임을 파열음으로 봤다).
            static constexpr float32 kOnsetWindowSeconds = 0.01f;

            /** @brief 표본 [begin, end) 의 RMS 입니다. */
            static float32 computeRangeRms( const vector<float32>& listSample, size_t begin, size_t end )
            {
                float64 energy = 0.0;
                for ( size_t index = begin; index < end; ++index )
                {
                    energy += static_cast<float64>( listSample[index] ) * static_cast<float64>( listSample[index] );
                }
                return static_cast<float32>( MathUtil::sqrt( energy / static_cast<float64>( MathUtil::max<size_t>( end - begin, 1u ) ) ) );
            }

            /** @brief 표본 [begin, end) 의 초당 영교차 수입니다(부호가 바뀐 이웃 쌍의 수). */
            static float32 computeZeroCrossingHz( const vector<float32>& listSample, size_t begin, size_t end, uint32 sampleRate )
            {
                uint32 crossingCount = 0;
                for ( size_t index = begin + 1; index < end; ++index )
                {
                    if ( ( listSample[index - 1] < 0.0f ) != ( listSample[index] < 0.0f ) )
                        ++crossingCount;
                }
                return end > begin ? static_cast<float32>( crossingCount ) * static_cast<float32>( sampleRate ) / static_cast<float32>( end - begin ) : 0.0f;
            }

            /** @brief Levinson-Durbin 으로 자기상관 @p pAutocorrelation(0..order)에서 예측 계수 @p pOutCoefficient(1..order, [0] = 1)를 구합니다. 풀 수 없으면 false 입니다. */
            static bool solveLpc( const float64* pAutocorrelation, uint32 order, float64* pOutCoefficient )
            {
                if ( pAutocorrelation[0] <= 0.0 )
                    return false;
                float64 arrPreviousCoefficient[kMaxLpcOrder + 1]{};
                pOutCoefficient[0] = 1.0;
                for ( uint32 index = 1; index <= order; ++index )
                {
                    pOutCoefficient[index] = 0.0;
                }
                float64 error = pAutocorrelation[0];
                for ( uint32 step = 1; step <= order; ++step )
                {
                    float64 accumulated = pAutocorrelation[step];
                    for ( uint32 index = 1; index < step; ++index )
                    {
                        accumulated += pOutCoefficient[index] * pAutocorrelation[step - index];
                    }
                    const float64 reflection = -accumulated / error;
                    for ( uint32 index = 0; index <= order; ++index )
                    {
                        arrPreviousCoefficient[index] = pOutCoefficient[index];
                    }
                    for ( uint32 index = 1; index < step; ++index )
                    {
                        pOutCoefficient[index] = arrPreviousCoefficient[index] + reflection * arrPreviousCoefficient[step - index];
                    }
                    pOutCoefficient[step] = reflection;
                    error *= 1.0 - reflection * reflection;
                    if ( error <= 0.0 )
                        return false;
                }
                return true;
            }

            /** @brief 예측 다항식 A(z) 의 주파수 @p hz 에서의 포락선 크기 1 / |A(e^jω)| 입니다. */
            static float64 computeEnvelope( const float64* pCoefficient, uint32 order, float64 hz, float64 sampleRate )
            {
                const float64 omega = 2.0 * MathUtil::kPi64 * hz / sampleRate;
                float64       real  = 0.0;
                float64       imag  = 0.0;
                for ( uint32 index = 0; index <= order; ++index )
                {
                    real += pCoefficient[index] * MathUtil::cos( omega * static_cast<float64>( index ) );
                    imag -= pCoefficient[index] * MathUtil::sin( omega * static_cast<float64>( index ) );
                }
                const float64 magnitude = MathUtil::sqrt( real * real + imag * imag );
                return magnitude > 0.0 ? 1.0 / magnitude : 0.0;
            }

            /** @brief RBJ 대역 통과 바이쿼드(최고 이득 0 dB)입니다. */
            struct BandPass
            {
                float32 _b0{ 0.0f };
                float32 _b2{ 0.0f };
                float32 _a1{ 0.0f };
                float32 _a2{ 0.0f };
                float32 _x1{ 0.0f };
                float32 _x2{ 0.0f };
                float32 _y1{ 0.0f };
                float32 _y2{ 0.0f };

                void configure( float32 lowHz, float32 highHz, float32 sampleRate )
                {
                    const float32 nyquist = sampleRate * 0.5f * 0.95f;
                    const float32 low     = MathUtil::clamp( lowHz, 10.0f, nyquist - 10.0f );
                    const float32 high    = MathUtil::clamp( highHz, low + 5.0f, nyquist );
                    const float32 center  = MathUtil::sqrt( low * high );
                    const float32 quality = center / ( high - low );
                    const float32 omega   = 2.0f * MathUtil::kPi * center / sampleRate;
                    const float32 alpha   = MathUtil::sin( omega ) / ( 2.0f * quality );
                    const float32 a0      = 1.0f + alpha;
                    _b0                   = alpha / a0;
                    _b2                   = -alpha / a0;
                    _a1                   = -2.0f * MathUtil::cos( omega ) / a0;
                    _a2                   = ( 1.0f - alpha ) / a0;
                }

                float32 process( float32 input )
                {
                    const float32 output = _b0 * input + _b2 * _x2 - _a1 * _y1 - _a2 * _y2;
                    _x2                  = _x1;
                    _x1                  = input;
                    _y2                  = _y1;
                    _y1                  = output;
                    return output;
                }
            };

            /**
             * @brief 프레임 [begin, end) 안에서 창 사이 에너지 오름이 가장 큰 창을 찾습니다. 오름은 앞 창 대비 비율이고, 바닥은 무음 RMS 다(무음끼리의 잡음 비율이 터짐으로 보이지 않게).
             * @param inoutPreviousOnsetRms 앞 프레임의 마지막 짧은 창의 RMS 입니다(터짐이 프레임 경계에 걸쳐도 측정한다). 이 프레임의 마지막 창으로 갱신합니다.
             * @param outRiseBegin 가장 많이 오른 창의 시작입니다.
             * @return 가장 큰 오름 비율입니다.
             */
            static float32 findMaxOnsetRise( const vector<float32>& listSample, size_t begin, size_t end, size_t onsetSamples, float32 silenceRms, float32& inoutPreviousOnsetRms, size_t& outRiseBegin )
            {
                float32 maxOnsetRise = 0.0f;
                outRiseBegin         = begin;
                for ( size_t onsetBegin = begin; onsetBegin < end; onsetBegin += onsetSamples )
                {
                    const float32 onsetRms = computeRangeRms( listSample, onsetBegin, MathUtil::min( onsetBegin + onsetSamples, end ) );
                    const float32 rise     = onsetRms / MathUtil::max( inoutPreviousOnsetRms, silenceRms );
                    if ( rise > maxOnsetRise )
                    {
                        maxOnsetRise = rise;
                        outRiseBegin = onsetBegin;
                    }
                    inoutPreviousOnsetRms = onsetRms;
                }
                return maxOnsetRise;
            }

            /**
             * @brief 대역 형태(합 1)를 비즘 테이블과 코사인으로 비교해 첫 비즘(무음)을 뺀 비즘마다의 점수를 @p inoutListScore 에 채웁니다(날카로움으로 곱한다).
             *        포먼트가 있으면 모음 비즘에 포먼트 일치 점수를, 자음 단서가 맞으면 자음 점수를 더합니다.
             * @return 점수의 최댓값입니다(softmax 의 기준).
             */
            static float32 computeVisemeScores( const LipSyncSettings& settings, const float32 ( &arrShape )[3], float32 shapeLength, bool bHasFormants, const float2& formantHz, bool bPlosive,
                                                bool bFricative, vector<float32>& inoutListScore )
            {
                const uint32 visemeCount = static_cast<uint32>( settings._listViseme.size() );
                float32      maxScore    = -MathUtil::kMaxFloat;
                for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
                {
                    const float3& band          = settings._listViseme[visemeIndex]._bandShape;
                    const float32 profileLength = MathUtil::sqrt( band._x * band._x + band._y * band._y + band._z * band._z );
                    const float32 cosine        = ( shapeLength > 0.0f && profileLength > 0.0f )
                                                    ? ( arrShape[0] * band._x + arrShape[1] * band._y + arrShape[2] * band._z ) / ( shapeLength * profileLength )
                                                    : 0.0f;
                    inoutListScore[visemeIndex] = cosine * settings._sharpness;
                    const LipSyncViseme& viseme = settings._listViseme[visemeIndex];
                    if ( bHasFormants && viseme.isVowel() )
                    {
                        // 포먼트마다 상대 거리 — 1 이면 두 배 · 절반만큼 어긋남. 맞을수록 1 에 가까운 점수를 더한다.
                        const float32 firstOffset  = ( formantHz._x - viseme._formantHz._x ) / viseme._formantHz._x;
                        const float32 secondOffset = ( formantHz._y - viseme._formantHz._y ) / viseme._formantHz._y;
                        const float32 distance     = MathUtil::sqrt( firstOffset * firstOffset + secondOffset * secondOffset );
                        inoutListScore[visemeIndex] += settings._formantWeight * ( 1.0f - MathUtil::min( distance, 1.0f ) );
                    }
                    const bool bConsonantCue = ( viseme._consonant == LipSyncConsonant::Plosive && bPlosive ) || ( viseme._consonant == LipSyncConsonant::Fricative && bFricative );
                    if ( bConsonantCue )
                        inoutListScore[visemeIndex] += settings._consonantWeight;
                    maxScore = MathUtil::max( maxScore, inoutListScore[visemeIndex] );
                }
                return maxScore;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LipSyncSettings::parseXML( string_view xml, string_view sourceLabel )
    {
        *this = LipSyncSettings{};
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigXMLElement` 가 로그로 알린다).
        if ( ConfigManager::readConfigXMLElement( this, *StaticType(), xml, label.c_str() ) && validate( sourceLabel ) )
            return true;
        *this = LipSyncSettings{};
        return false;
    }

    bool LipSyncSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Lip sync settings '%#' could not be read", path );
            *this = LipSyncSettings{};
            return false;
        }
        return parseXML( text, path );
    }

    bool LipSyncSettings::validate( string_view sourceLabel ) const
    {
        if ( _frameRate <= 0.0f || _fullRms <= _silenceRms )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': _frameRate must be positive and _fullRms above _silenceRms", sourceLabel );
            return false;
        }
        const float2      arrBandHz[3]{ _lowBandHz, _midBandHz, _highBandHz };
        const utf8* const arrBandName[3]{ "_lowBandHz", "_midBandHz", "_highBandHz" };
        for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
        {
            const float2& band = arrBandHz[bandIndex];
            if ( band._x <= 0.0f || band._y <= band._x )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': %# must be \"low,high\" Hz with 0 < low < high", sourceLabel, arrBandName[bandIndex] );
                return false;
            }
        }
        if ( _listViseme.size() < 2 )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': _listViseme needs at least two visemes (the first one is silence)", sourceLabel );
            return false;
        }
        for ( size_t visemeIndex = 0; visemeIndex < _listViseme.size(); ++visemeIndex )
        {
            const LipSyncViseme& viseme = _listViseme[visemeIndex];
            if ( viseme._name.empty() || findVisemeIndex( viseme._name ) != static_cast<int32>( visemeIndex ) )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': viseme %# needs a unique, non-empty _name", sourceLabel, visemeIndex );
                return false;
            }
            // 포먼트는 둘 다 비거나(모음 아님) 0 < F1 < F2 여야 한다.
            const bool bNoFormant      = viseme._formantHz._x == 0.0f && viseme._formantHz._y == 0.0f;
            const bool bOrderedFormant = 0.0f < viseme._formantHz._x && viseme._formantHz._x < viseme._formantHz._y;
            if ( bNoFormant == false && bOrderedFormant == false )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': viseme '%#' _formantHz must be \"0,0\" or \"F1,F2\" with 0 < F1 < F2", sourceLabel, viseme._name.c_str() );
                return false;
            }
            // 자음 단서는 자음 비즘의 것이다 — 모음(포먼트)이나 무음(첫 비즘)에 달면 두 길이 한 비즘을 두고 다툰다.
            const bool bConsonantOnVowelOrSilence = viseme._consonant != LipSyncConsonant::None && ( viseme.isVowel() || visemeIndex == 0 );
            if ( bConsonantOnVowelOrSilence )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': viseme '%#' has a _consonant cue but is a vowel or the silence viseme", sourceLabel, viseme._name.c_str() );
                return false;
            }
        }
        if ( _plosiveOnsetRatio <= 1.0f )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': _plosiveOnsetRatio must be greater than 1 (an energy rise)", sourceLabel );
            return false;
        }
        if ( findVisemeIndex( _fallbackViseme ) < 0 )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': _fallbackViseme '%#' is not a viseme", sourceLabel, _fallbackViseme.c_str() );
            return false;
        }
        return true;
    }

    int32 LipSyncSettings::findVisemeIndex( const hashed_string& name ) const
    {
        for ( size_t visemeIndex = 0; visemeIndex < _listViseme.size(); ++visemeIndex )
        {
            if ( _listViseme[visemeIndex]._name == name )
                return static_cast<int32>( visemeIndex );
        }
        return -1;
    }

    void VisemeTrack::sample( float32 time, vector<float32>& outListWeight ) const
    {
        const uint32 visemeCount = getVisemeCount();
        const uint32 frameCount  = getFrameCount();
        outListWeight.assign( visemeCount, 0.0f );
        if ( frameCount == 0 || time < 0.0f )
            return;
        const float32 position = time * _frameRate;
        if ( position >= static_cast<float32>( frameCount ) )
            return;
        const uint32  first  = static_cast<uint32>( position );
        const uint32  second = MathUtil::min( first + 1u, frameCount - 1u );
        const float32 alpha  = position - static_cast<float32>( first );
        for ( uint32 visemeIndex = 0; visemeIndex < visemeCount; ++visemeIndex )
        {
            const float32 from         = _listWeight[static_cast<size_t>( first ) * visemeCount + visemeIndex];
            const float32 to           = _listWeight[static_cast<size_t>( second ) * visemeCount + visemeIndex];
            outListWeight[visemeIndex] = from + ( to - from ) * alpha;
        }
    }

    bool VisemeTrack::parseXML( string_view xml, string_view sourceLabel )
    {
        *this = VisemeTrack{};
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "VisemeTrack", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR( "Viseme track '%#': malformed XML (expected a <VisemeTrack> root)", sourceLabel );
            return false;
        }
        if ( XMLReadUtil::hasOnlyKnownNames( root, { "_frameRate" }, { "_listViseme", "_listFrame" }, sourceLabel ) == false )
            return false;
        const XMLNode                visemes              = root.findChild( "_listViseme" );
        const XMLNode                frames               = root.findChild( "_listFrame" );
        const std::optional<float32> frameRate            = root.readRequiredAttribute<float32>( "_frameRate" );
        const bool                   bMissingVisemeFields = frameRate.has_value() == false || visemes.isValid() == false || visemes.findChild().isValid() == false || frames.isValid() == false;
        if ( bMissingVisemeFields )
        {
            SW_LOG_ERROR( "Viseme track '%#' needs _frameRate, _listViseme and _listFrame", sourceLabel );
            return false;
        }
        _frameRate = *frameRate;
        for ( XMLNode viseme = visemes.findChild(); viseme.isValid(); viseme = viseme.findNextSibling() )
        {
            _listViseme.push_back( hashed_string( string_view{ viseme.getText() } ) );
        }
        vector<float32> listFrame( _listViseme.size() );
        size_t          frameIndex{ 0 };
        for ( XMLNode frame = frames.findChild(); frame.isValid(); frame = frame.findNextSibling(), ++frameIndex )
        {
            if ( XMLReadUtil::readFloatList( frame.getText(), listFrame.data(), static_cast<uint32>( listFrame.size() ) ) == false )
            {
                SW_LOG_ERROR( "Viseme track '%#': frame %# must hold one weight per viseme", sourceLabel, frameIndex );
                *this = VisemeTrack{};
                return false;
            }
            _listWeight.insert( _listWeight.end(), listFrame.begin(), listFrame.end() );
        }
        return _frameRate > 0.0f;
    }

    bool VisemeTrack::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Viseme track '%#' could not be read", path );
            *this = VisemeTrack{};
            return false;
        }
        return parseXML( text, path );
    }

    string VisemeTrack::toXML() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "VisemeTrack" );
        root.appendAttribute( "_frameRate", _frameRate );
        const XMLNode visemes = root.appendChild( "_listViseme" );
        for ( const hashed_string& name : _listViseme )
        {
            (void)visemes.appendChild( "item", string_view{ name.c_str() } );
        }
        const XMLNode   frames      = root.appendChild( "_listFrame" );
        const uint32    visemeCount = getVisemeCount();
        vector<float32> listFrame( visemeCount );
        for ( uint32 frameIndex = 0; frameIndex < getFrameCount(); ++frameIndex )
        {
            // 소수 셋째 자리로 줄인다. 파일이 짧아지고 비교가 안정된다(가중치 정밀도로는 충분하다).
            for ( uint32 visemeIndex = 0; visemeIndex < visemeCount; ++visemeIndex )
            {
                const float32 weight   = _listWeight[static_cast<size_t>( frameIndex ) * visemeCount + visemeIndex];
                listFrame[visemeIndex] = MathUtil::round( weight * 1000.0f ) / 1000.0f;
            }
            (void)frames.appendChild( "Frame", string_view{ XMLReadUtil::makeFloatListText( listFrame.data(), visemeCount ) } );
        }
        return document.saveToString();
    }

    bool VisemeTrack::saveToFile( string_view path ) const
    {
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeTextFile( path, toXML() );
    }

    string VisemeTrack::makePathForAudio( string_view audioPath )
    {
        const size_t dot = audioPath.find_last_of( '.' );
        string       path{ dot == string_view::npos ? audioPath : audioPath.substr( 0, dot ) };
        path.append( kExtension.data(), kExtension.size() );
        return path;
    }

    float32 LipSyncAnalyzer::computeOpenness( float32 rms, const LipSyncSettings& settings )
    {
        return MathUtil::saturate( ( rms - settings._silenceRms ) / MathUtil::max( settings._fullRms - settings._silenceRms, 1e-6f ) );
    }

    float32 LipSyncAnalyzer::computeRms( const vector<float32>& listSample, uint32 sampleRate, float32 time, float32 windowSeconds )
    {
        const bool bNoRmsWindow = listSample.empty() || sampleRate == 0 || time < 0.0f;
        if ( bNoRmsWindow )
            return 0.0f;
        const size_t center = static_cast<size_t>( time * static_cast<float32>( sampleRate ) );
        const size_t half   = MathUtil::max<size_t>( static_cast<size_t>( windowSeconds * 0.5f * static_cast<float32>( sampleRate ) ), 1u );
        if ( center >= listSample.size() )
            return 0.0f;
        const size_t begin = center > half ? center - half : 0u;
        const size_t end   = MathUtil::min( center + half, listSample.size() );
        float64      sum   = 0.0;
        for ( size_t index = begin; index < end; ++index )
        {
            sum += static_cast<float64>( listSample[index] ) * static_cast<float64>( listSample[index] );
        }
        return static_cast<float32>( MathUtil::sqrt( sum / static_cast<float64>( MathUtil::max<size_t>( end - begin, 1u ) ) ) );
    }

    bool LipSyncAnalyzer::tryComputeFormants( const float32* pSample, size_t sampleCount, uint32 sampleRate, float2& outFormantHz )
    {
        outFormantHz = float2{ 0.0f, 0.0f };
        if ( pSample == nullptr || sampleRate == 0 )
            return false;
        // 솎아 내기 — 정수 배로 평균한다(평균이 낮은 통과 필터를 겸한다).
        const uint32  decimation    = MathUtil::max( sampleRate / LipSyncInternal::kFormantTargetRate, 1u );
        const float64 effectiveRate = static_cast<float64>( sampleRate ) / static_cast<float64>( decimation );
        const size_t  count         = sampleCount / decimation;
        const uint32  order         = MathUtil::min( static_cast<uint32>( effectiveRate / 1000.0 ) + 2u, LipSyncInternal::kMaxLpcOrder );
        if ( count <= static_cast<size_t>( order ) * 2u )
            return false;
        vector<float64> listFrame( count );
        float64         previous = 0.0;
        for ( size_t index = 0; index < count; ++index )
        {
            float64 sum = 0.0;
            for ( uint32 offset = 0; offset < decimation; ++offset )
            {
                sum += static_cast<float64>( pSample[index * decimation + offset] );
            }
            const float64 value = sum / static_cast<float64>( decimation );
            // 프리엠퍼시스 · 해밍 창.
            const float64 emphasized = value - static_cast<float64>( LipSyncInternal::kPreEmphasis ) * previous;
            previous                 = value;
            const float64 window     = 0.54 - 0.46 * MathUtil::cos( 2.0 * MathUtil::kPi64 * static_cast<float64>( index ) / static_cast<float64>( count - 1 ) );
            listFrame[index]         = emphasized * window;
        }
        float64 arrAutocorrelation[LipSyncInternal::kMaxLpcOrder + 1]{};
        for ( uint32 lag = 0; lag <= order; ++lag )
        {
            float64 sum = 0.0;
            for ( size_t index = lag; index < count; ++index )
            {
                sum += listFrame[index] * listFrame[index - lag];
            }
            arrAutocorrelation[lag] = sum;
        }
        float64 arrCoefficient[LipSyncInternal::kMaxLpcOrder + 1]{};
        if ( LipSyncInternal::solveLpc( arrAutocorrelation, order, arrCoefficient ) == false )
            return false;

        // 포락선을 훑어 가장 높은 봉우리 둘을 고르고 주파수 순으로 둔다 — 첫 둘을 고르면 배음 사이의 잔 봉우리에 속는다.
        float64 arrPeakHz[2]{ 0.0, 0.0 };
        float64 arrPeakHeight[2]{ 0.0, 0.0 };
        float64 before  = LipSyncInternal::computeEnvelope( arrCoefficient, order, static_cast<float64>( LipSyncInternal::kFormantMinHz - LipSyncInternal::kFormantStepHz ), effectiveRate );
        float64 current = LipSyncInternal::computeEnvelope( arrCoefficient, order, static_cast<float64>( LipSyncInternal::kFormantMinHz ), effectiveRate );
        for ( float32 hz = LipSyncInternal::kFormantMinHz; hz < LipSyncInternal::kFormantMaxHz; hz += LipSyncInternal::kFormantStepHz )
        {
            const float64 after = LipSyncInternal::computeEnvelope( arrCoefficient, order, static_cast<float64>( hz + LipSyncInternal::kFormantStepHz ), effectiveRate );
            const bool    bPeak = current > before && current >= after;
            if ( bPeak && current > arrPeakHeight[1] )
            {
                // 둘째 자리에 넣고, 첫째보다 높으면 자리를 바꾼다(높이 순 두 칸).
                arrPeakHz[1]     = static_cast<float64>( hz );
                arrPeakHeight[1] = current;
                if ( arrPeakHeight[1] > arrPeakHeight[0] )
                {
                    std::swap( arrPeakHz[0], arrPeakHz[1] );
                    std::swap( arrPeakHeight[0], arrPeakHeight[1] );
                }
            }
            before  = current;
            current = after;
        }
        if ( arrPeakHeight[1] <= 0.0 )
            return false;
        const float64 lowHz  = MathUtil::min( arrPeakHz[0], arrPeakHz[1] );
        const float64 highHz = MathUtil::max( arrPeakHz[0], arrPeakHz[1] );
        outFormantHz         = float2{ static_cast<float32>( lowHz ), static_cast<float32>( highHz ) };
        return true;
    }

    void LipSyncAnalyzer::analyze( const vector<float32>& listSample, uint32 sampleRate, const LipSyncSettings& settings, VisemeTrack& outTrack )
    {
        outTrack            = VisemeTrack{};
        outTrack._frameRate = settings._frameRate;
        for ( const LipSyncViseme& viseme : settings._listViseme )
        {
            outTrack._listViseme.push_back( viseme._name );
        }
        const uint32 visemeCount       = static_cast<uint32>( settings._listViseme.size() );
        const bool   bNothingToAnalyze = sampleRate == 0 || visemeCount < 2 || listSample.empty();
        if ( bNothingToAnalyze )
            return;

        const float2              arrBandHz[3]{ settings._lowBandHz, settings._midBandHz, settings._highBandHz };
        LipSyncInternal::BandPass arrFilter[3];
        for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
        {
            arrFilter[bandIndex].configure( arrBandHz[bandIndex]._x, arrBandHz[bandIndex]._y, static_cast<float32>( sampleRate ) );
        }
        const size_t    frameSamples = MathUtil::max<size_t>( static_cast<size_t>( static_cast<float32>( sampleRate ) / settings._frameRate ), 1u );
        const size_t    frameCount   = ( listSample.size() + frameSamples - 1 ) / frameSamples;
        const size_t    onsetSamples = MathUtil::max<size_t>( static_cast<size_t>( LipSyncInternal::kOnsetWindowSeconds * static_cast<float32>( sampleRate ) ), 1u );
        vector<float32> listScore( visemeCount );
        outTrack._listWeight.assign( frameCount * visemeCount, 0.0f );
        float32 previousFrameRms = 0.0f; // 클립 앞은 무음이다
        float32 previousOnsetRms = 0.0f; // 앞 프레임의 마지막 짧은 창(터짐이 프레임 경계에 걸쳐도 측정한다)
        for ( size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const size_t begin = frameIndex * frameSamples;
            const size_t end   = MathUtil::min( begin + frameSamples, listSample.size() );
            // 창 사이 에너지 오름이 가장 큰 창을 찾는다 — 바닥은 무음 RMS 다(무음끼리의 잡음 비율이 터짐으로 보이지 않게).
            size_t        riseBegin    = begin;
            const float32 maxOnsetRise = LipSyncInternal::findMaxOnsetRise( listSample, begin, end, onsetSamples, settings._silenceRms, previousOnsetRms, riseBegin );
            // 파열음 = 무음 뒤 갑자기 오른 창이 잡음(영교차가 잦음)인 터짐이다. 모음의 시작도 크게 오를 수 있지만 유성음이라 영교차가 드물다.
            const bool bAfterSilence = previousFrameRms < settings._silenceRms;
            const bool bSuddenRise   = bAfterSilence && maxOnsetRise >= settings._plosiveOnsetRatio;
            const bool bPlosive =
                bSuddenRise &&
                LipSyncInternal::computeZeroCrossingHz( listSample, riseBegin, MathUtil::min( riseBegin + onsetSamples, end ), sampleRate ) >= settings._fricativeZeroCrossingHz;
            // 터짐 자체도 잡음이라 영교차가 잦다 — 터짐 프레임은 파열음 단서만 갖는다.
            const bool    bFricative = bPlosive == false && LipSyncInternal::computeZeroCrossingHz( listSample, begin, end, sampleRate ) >= settings._fricativeZeroCrossingHz;
            const float32 frameRms   = LipSyncInternal::computeRangeRms( listSample, begin, end );
            previousFrameRms         = frameRms;
            float64 arrBandEnergy[3]{ 0.0, 0.0, 0.0 };
            for ( size_t index = begin; index < end; ++index )
            {
                const float32 sample = listSample[index];
                for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
                {
                    const float32 filtered = arrFilter[bandIndex].process( sample );
                    arrBandEnergy[bandIndex] += static_cast<float64>( filtered ) * static_cast<float64>( filtered );
                }
            }
            const float32 openness = computeOpenness( frameRms, settings );
            float32*      pWeight  = &outTrack._listWeight[frameIndex * visemeCount];
            // 첫 비즘이 무음이다. 열린 만큼 나머지 비즘이 나눠 갖는다.
            pWeight[0] = 1.0f - openness;
            if ( openness <= 0.0f )
                continue;
            // 대역 형태(합 1)를 비즘 테이블과 코사인으로 비교하고, 날카로움으로 softmax 한다.
            const float64 bandSum = arrBandEnergy[0] + arrBandEnergy[1] + arrBandEnergy[2];
            float32       arrShape[3]{ 0.0f, 0.0f, 0.0f };
            for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
            {
                arrShape[bandIndex] = bandSum > 0.0 ? static_cast<float32>( arrBandEnergy[bandIndex] / bandSum ) : 0.0f;
            }
            const float32 shapeLength = MathUtil::sqrt( arrShape[0] * arrShape[0] + arrShape[1] * arrShape[1] + arrShape[2] * arrShape[2] );
            // 유성 프레임(높은 대역이 주가 아닌)은 포먼트를 재 모음 비즘과 비교한다 — 대역 셋으로는 가까운 모음(EE · OO 의 낮은 첫 포먼트)을 가르기 어렵다.
            // 터짐 · 잡음 프레임의 포락선 봉우리는 성도의 공명이 아니다 — 자음 단서가 있으면 포먼트를 보지 않는다.
            float2        formantHz{ 0.0f, 0.0f };
            const bool    bVoiced      = arrShape[2] < settings._unvoicedHighBandRatio && bPlosive == false && bFricative == false;
            const bool    bHasFormants = bVoiced && tryComputeFormants( &listSample[begin], end - begin, sampleRate, formantHz );
            const float32 maxScore     = LipSyncInternal::computeVisemeScores( settings, arrShape, shapeLength, bHasFormants, formantHz, bPlosive, bFricative, listScore );
            float32       sum          = 0.0f;
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
            {
                listScore[visemeIndex] = MathUtil::exp( listScore[visemeIndex] - maxScore );
                sum += listScore[visemeIndex];
            }
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
            {
                pWeight[visemeIndex] = openness * listScore[visemeIndex] / sum;
            }
        }
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( VisemeTrack, VisemeTrack::kAssetType );

    /** @brief 립싱크 분석 표 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( LipSync, "lipsync", nullptr, nullptr, LipSyncSettings::kResourcePath.data(), &ResourceDataKindUtil::loadCatalog<LipSyncSettings> );

    /** @brief 비즘 트랙 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( Visemes, "visemes", nullptr, nullptr, VisemeTrack::kExtension.data(), &ResourceDataKindUtil::loadCatalog<VisemeTrack> );
} // namespace sw
