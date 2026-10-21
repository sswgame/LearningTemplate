#include "pch.h"

#include "Engine/Animation/Facial/LipSync.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationJSONUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "LipSync" );

    namespace
    {
        struct LipSyncInternal
        {
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LipSyncSettings::parseJSON( string_view json, string_view sourceLabel )
    {
        *this = LipSyncSettings{};
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigJSON` 이 로그로 알린다).
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) && validate( sourceLabel ) )
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
        return parseJSON( text, path );
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

    bool VisemeTrack::parseJSON( string_view json, string_view sourceLabel )
    {
        *this = VisemeTrack{};
        JSONDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Viseme track '%#': malformed JSON", sourceLabel );
            return false;
        }
        const JSONValue root = document.getRoot();
        if ( AnimationJSONUtil::hasOnlyKnownKeys( root, { "frame_rate", "visemes", "frames" }, sourceLabel ) == false )
            return false;
        const JSONValue              visemes              = root.get( "visemes" );
        const JSONValue              frames               = root.get( "frames" );
        const std::optional<float32> frameRate            = root.readRequired<float32>( "frame_rate" );
        const bool                   bMissingVisemeFields = frameRate.has_value() == false || visemes.isArray() == false || visemes.size() == 0 || frames.isArray() == false;
        if ( bMissingVisemeFields )
        {
            SW_LOG_ERROR( "Viseme track '%#' needs frame_rate, visemes and frames", sourceLabel );
            return false;
        }
        _frameRate = *frameRate;
        for ( size_t visemeIndex = 0; visemeIndex < visemes.size(); ++visemeIndex )
        {
            _listViseme.push_back( hashed_string( visemes.at( visemeIndex ).asString() ) );
        }
        vector<float32> listFrame( _listViseme.size() );
        for ( size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex )
        {
            if ( AnimationJSONUtil::readFloats( frames.at( frameIndex ), listFrame.data(), static_cast<uint32>( listFrame.size() ) ) == false )
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
        return parseJSON( text, path );
    }

    string VisemeTrack::toJSON() const
    {
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        root.set( "frame_rate" ).setFloat( static_cast<float64>( _frameRate ) );
        const JSONValue visemes = root.set( "visemes" );
        visemes.setArray();
        for ( const hashed_string& name : _listViseme )
        {
            visemes.pushBack().setString( name.c_str() );
        }
        const JSONValue frames = root.set( "frames" );
        frames.setArray();
        const uint32 visemeCount = getVisemeCount();
        for ( uint32 frameIndex = 0; frameIndex < getFrameCount(); ++frameIndex )
        {
            // 소수 셋째 자리로 줄인다. 파일이 짧아지고 비교가 안정된다(가중치 정밀도로는 충분하다).
            const JSONValue frame = frames.pushBack();
            frame.setArray();
            for ( uint32 visemeIndex = 0; visemeIndex < visemeCount; ++visemeIndex )
            {
                const float32 weight = _listWeight[static_cast<size_t>( frameIndex ) * visemeCount + visemeIndex];
                frame.pushBack().setFloat( static_cast<float64>( MathUtil::round( weight * 1000.0f ) / 1000.0f ) );
            }
        }
        return document.dump( 0 );
    }

    bool VisemeTrack::saveToFile( string_view path ) const
    {
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeTextFile( path, toJSON() );
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
        vector<float32> listScore( visemeCount );
        outTrack._listWeight.assign( frameCount * visemeCount, 0.0f );
        for ( size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const size_t begin = frameIndex * frameSamples;
            const size_t end   = MathUtil::min( begin + frameSamples, listSample.size() );
            float64      energy{ 0.0 };
            float64      arrBandEnergy[3]{ 0.0, 0.0, 0.0 };
            for ( size_t index = begin; index < end; ++index )
            {
                const float32 sample = listSample[index];
                energy += static_cast<float64>( sample ) * static_cast<float64>( sample );
                for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
                {
                    const float32 filtered = arrFilter[bandIndex].process( sample );
                    arrBandEnergy[bandIndex] += static_cast<float64>( filtered ) * static_cast<float64>( filtered );
                }
            }
            const float32 rms      = static_cast<float32>( MathUtil::sqrt( energy / static_cast<float64>( MathUtil::max<size_t>( end - begin, 1u ) ) ) );
            const float32 openness = computeOpenness( rms, settings );
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
            float32       maxScore    = -MathUtil::kMaxFloat;
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
            {
                const float3& band          = settings._listViseme[visemeIndex]._bandShape;
                const float32 profileLength = MathUtil::sqrt( band._x * band._x + band._y * band._y + band._z * band._z );
                const float32 cosine        = ( shapeLength > 0.0f && profileLength > 0.0f )
                                                ? ( arrShape[0] * band._x + arrShape[1] * band._y + arrShape[2] * band._z ) / ( shapeLength * profileLength )
                                                : 0.0f;
                listScore[visemeIndex]      = cosine * settings._sharpness;
                maxScore                    = MathUtil::max( maxScore, listScore[visemeIndex] );
            }
            float32 sum = 0.0f;
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
} // namespace sw
