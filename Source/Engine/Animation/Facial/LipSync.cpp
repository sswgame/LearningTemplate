#include "pch.h"

#include "Engine/Animation/Facial/LipSync.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

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
                    const float32 omega   = 2.0f * MathUtil::Pi * center / sampleRate;
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
    bool LipSyncSettings::parseJson( string_view json, string_view sourceLabel )
    {
        *this = LipSyncSettings{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        *this = LipSyncSettings{};
        return false;
    }

    bool LipSyncSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Lip sync settings '%#' could not be read", path );
            *this = LipSyncSettings{};
            return false;
        }
        return parseJson( text, path );
    }

    bool LipSyncSettings::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "frame_rate", "silence_rms", "full_rms", "sharpness", "fallback_viseme", "bands", "visemes" }, sourceLabel ) == false )
            return false;
        const JsonValue bands    = root.get( "bands" );
        const JsonValue visemes  = root.get( "visemes" );
        const bool      bScalars = root.get( "frame_rate" ).isNumber() && root.get( "silence_rms" ).isNumber() && root.get( "full_rms" ).isNumber() &&
                              root.get( "sharpness" ).isNumber() && root.get( "fallback_viseme" ).isString();
        if ( bScalars == false || bands.isArray() == false || bands.size() != 3 || visemes.isArray() == false || visemes.size() < 2 )
        {
            SW_LOG_ERROR( "Lip sync settings '%#' needs frame_rate, silence_rms, full_rms, sharpness, fallback_viseme, three bands and at least two visemes",
                          sourceLabel );
            return false;
        }
        _frameRate  = static_cast<float32>( root.get( "frame_rate" ).asFloat() );
        _silenceRms = static_cast<float32>( root.get( "silence_rms" ).asFloat() );
        _fullRms    = static_cast<float32>( root.get( "full_rms" ).asFloat() );
        _sharpness  = static_cast<float32>( root.get( "sharpness" ).asFloat() );
        if ( _frameRate <= 0.0f || _fullRms <= _silenceRms )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': frame_rate must be positive and full_rms above silence_rms", sourceLabel );
            return false;
        }
        for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
        {
            if ( AnimJsonUtil::readFloats( bands.at( bandIndex ), _arrBandRange[bandIndex], 2 ) == false || _arrBandRange[bandIndex][1] <= _arrBandRange[bandIndex][0] )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': band %# must be [low Hz, high Hz]", sourceLabel, bandIndex );
                return false;
            }
        }
        for ( size_t visemeIndex = 0; visemeIndex < visemes.size(); ++visemeIndex )
        {
            const JsonValue viseme = visemes.at( visemeIndex );
            if ( AnimJsonUtil::hasOnlyKnownKeys( viseme, { "name", "bands" }, sourceLabel ) == false )
                return false;
            LipSyncViseme entry{};
            if ( viseme.get( "name" ).isString() == false || AnimJsonUtil::readFloats( viseme.get( "bands" ), entry._arrBand, 3 ) == false )
            {
                SW_LOG_ERROR( "Lip sync settings '%#': viseme %# needs a name and three band weights", sourceLabel, visemeIndex );
                return false;
            }
            entry._name = hashed_string( viseme.get( "name" ).asString() );
            _listViseme.push_back( entry );
        }
        _fallbackViseme = hashed_string( root.get( "fallback_viseme" ).asString() );
        if ( findVisemeIndex( _fallbackViseme ) < 0 )
        {
            SW_LOG_ERROR( "Lip sync settings '%#': fallback_viseme '%#' is not a viseme", sourceLabel, _fallbackViseme.c_str() );
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

    bool VisemeTrack::parseJson( string_view json, string_view sourceLabel )
    {
        *this = VisemeTrack{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Viseme track '%#': malformed JSON", sourceLabel );
            return false;
        }
        const JsonValue root = document.getRoot();
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "frame_rate", "visemes", "frames" }, sourceLabel ) == false )
            return false;
        const JsonValue visemes = root.get( "visemes" );
        const JsonValue frames  = root.get( "frames" );
        if ( root.get( "frame_rate" ).isNumber() == false || visemes.isArray() == false || visemes.size() == 0 || frames.isArray() == false )
        {
            SW_LOG_ERROR( "Viseme track '%#' needs frame_rate, visemes and frames", sourceLabel );
            return false;
        }
        _frameRate = static_cast<float32>( root.get( "frame_rate" ).asFloat() );
        for ( size_t visemeIndex = 0; visemeIndex < visemes.size(); ++visemeIndex )
            _listViseme.push_back( hashed_string( visemes.at( visemeIndex ).asString() ) );
        vector<float32> listFrame( _listViseme.size() );
        for ( size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex )
        {
            if ( AnimJsonUtil::readFloats( frames.at( frameIndex ), listFrame.data(), static_cast<uint32>( listFrame.size() ) ) == false )
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
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Viseme track '%#' could not be read", path );
            *this = VisemeTrack{};
            return false;
        }
        return parseJson( text, path );
    }

    string VisemeTrack::toJson() const
    {
        JsonDocument    document;
        const JsonValue root = document.makeObject();
        root.set( "frame_rate" ).setFloat( static_cast<float64>( _frameRate ) );
        const JsonValue visemes = root.set( "visemes" );
        visemes.setArray();
        for ( const hashed_string& name : _listViseme )
            visemes.pushBack().setString( name.c_str() );
        const JsonValue frames = root.set( "frames" );
        frames.setArray();
        const uint32 visemeCount = getVisemeCount();
        for ( uint32 frameIndex = 0; frameIndex < getFrameCount(); ++frameIndex )
        {
            // 소수 셋째 자리로 줄인다 — 파일이 짧고 비교가 안정된다(가중치 정밀도로는 충분하다).
            const JsonValue frame = frames.pushBack();
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
        return FileUtil::writeTextFile( path, toJson() );
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
        if ( listSample.empty() || sampleRate == 0 || time < 0.0f )
            return 0.0f;
        const size_t center = static_cast<size_t>( time * static_cast<float32>( sampleRate ) );
        const size_t half   = MathUtil::max<size_t>( static_cast<size_t>( windowSeconds * 0.5f * static_cast<float32>( sampleRate ) ), 1u );
        if ( center >= listSample.size() )
            return 0.0f;
        const size_t begin = center > half ? center - half : 0u;
        const size_t end   = MathUtil::min( center + half, listSample.size() );
        float64      sum   = 0.0;
        for ( size_t index = begin; index < end; ++index )
            sum += static_cast<float64>( listSample[index] ) * static_cast<float64>( listSample[index] );
        return static_cast<float32>( MathUtil::sqrt( sum / static_cast<float64>( MathUtil::max<size_t>( end - begin, 1u ) ) ) );
    }

    void LipSyncAnalyzer::analyze( const vector<float32>& listSample, uint32 sampleRate, const LipSyncSettings& settings, VisemeTrack& outTrack )
    {
        outTrack            = VisemeTrack{};
        outTrack._frameRate = settings._frameRate;
        for ( const LipSyncViseme& viseme : settings._listViseme )
            outTrack._listViseme.push_back( viseme._name );
        const uint32 visemeCount = static_cast<uint32>( settings._listViseme.size() );
        if ( sampleRate == 0 || visemeCount < 2 || listSample.empty() )
            return;

        LipSyncInternal::BandPass arrFilter[3];
        for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
            arrFilter[bandIndex].configure( settings._arrBandRange[bandIndex][0], settings._arrBandRange[bandIndex][1], static_cast<float32>( sampleRate ) );
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
            // 첫 비즘이 무음이다 — 열린 만큼 나머지 비즘이 나눠 갖는다.
            pWeight[0] = 1.0f - openness;
            if ( openness <= 0.0f )
                continue;
            // 대역 모양(합 1)을 비즘 표와 코사인으로 견주고 날카로움으로 softmax 한다.
            const float64 bandSum = arrBandEnergy[0] + arrBandEnergy[1] + arrBandEnergy[2];
            float32       arrShape[3]{ 0.0f, 0.0f, 0.0f };
            for ( uint32 bandIndex = 0; bandIndex < 3; ++bandIndex )
                arrShape[bandIndex] = bandSum > 0.0 ? static_cast<float32>( arrBandEnergy[bandIndex] / bandSum ) : 0.0f;
            const float32 shapeLength = MathUtil::sqrt( arrShape[0] * arrShape[0] + arrShape[1] * arrShape[1] + arrShape[2] * arrShape[2] );
            float32       maxScore    = -MathUtil::MaxFloat;
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
            {
                const float32* pBand         = settings._listViseme[visemeIndex]._arrBand;
                const float32  profileLength = MathUtil::sqrt( pBand[0] * pBand[0] + pBand[1] * pBand[1] + pBand[2] * pBand[2] );
                const float32  cosine        = ( shapeLength > 0.0f && profileLength > 0.0f )
                                                 ? ( arrShape[0] * pBand[0] + arrShape[1] * pBand[1] + arrShape[2] * pBand[2] ) / ( shapeLength * profileLength )
                                                 : 0.0f;
                listScore[visemeIndex]       = cosine * settings._sharpness;
                maxScore                     = MathUtil::max( maxScore, listScore[visemeIndex] );
            }
            float32 sum = 0.0f;
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
            {
                listScore[visemeIndex] = MathUtil::exp( listScore[visemeIndex] - maxScore );
                sum += listScore[visemeIndex];
            }
            for ( uint32 visemeIndex = 1; visemeIndex < visemeCount; ++visemeIndex )
                pWeight[visemeIndex] = openness * listScore[visemeIndex] / sum;
        }
    }
} // namespace sw
