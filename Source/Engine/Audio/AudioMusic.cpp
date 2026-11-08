#include "pch.h"

#include "Engine/Audio/AudioMusic.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool AudioMusicDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AudioMusic.MusicNotFound", "Music not found: %#", assetPath );
            return false;
        }
        return loadComposed( text, assetPath, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool AudioMusicDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool AudioMusicDef::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = AudioMusicDef{};
        // 변형(`_base`)은 부모를 펼친 뒤 읽는다 — 구간은 이름이 열쇠이고 전환 규칙 · 레이어 목록은 통째로 바뀐다(규칙은 문서 표 `<Document>`).
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), bComposed ? string_view{ flatText } : xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "AudioMusic.MusicCouldNotRead", "%#: music could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioMusicDef::validate( string_view sourceName ) const
    {
        bool bValid = true;
        if ( _listSegment.empty() )
        {
            SW_LOG_ERROR_ID( "AudioMusic.MusicNoSegments", "%#: music has no segments", sourceName );
            return false;
        }
        if ( _tempo <= 0.0f || _beatsPerBar == 0 )
        {
            SW_LOG_ERROR_ID( "AudioMusic.MusicNeedsPositiveTempo", "%#: music needs a positive tempo and beats per bar", sourceName );
            bValid = false;
        }
        if ( _startSegment.empty() == false && findSegmentIndex( _startSegment ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "AudioMusic.StartSegmentDoesNot", "%#: start segment '%#' does not exist", sourceName, _startSegment.c_str() );
            bValid = false;
        }
        for ( size_t segmentIndex = 0; segmentIndex < _listSegment.size(); ++segmentIndex )
        {
            const AudioMusicSegmentDef& segment = _listSegment[segmentIndex];
            const utf8*                 pName   = segment._name.c_str();
            if ( segment._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioMusic.SegmentNoName", "%#: segment %# has no name", sourceName, segmentIndex );
                bValid = false;
            }
            for ( size_t otherIndex = segmentIndex + 1; otherIndex < _listSegment.size(); ++otherIndex )
            {
                if ( _listSegment[otherIndex]._name == segment._name )
                {
                    SW_LOG_ERROR_ID( "AudioMusic.SegmentDeclaredTwice", "%#: segment '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            if ( segment._listLayer.empty() || segment._bars == 0 )
            {
                SW_LOG_ERROR_ID( "AudioMusic.SegmentNeedsLeastOne", "%#: segment '%#' needs at least one layer and one bar", sourceName, pName );
                bValid = false;
            }
            for ( const AudioMusicLayerDef& layer : segment._listLayer )
            {
                const bool bCurveMissing      = layer._parameter.empty() == false && layer._listPoint.empty();
                const bool bInvalidMusicLayer = layer._path.empty() || bCurveMissing || AudioMixerDef::isCurveSorted( layer._listPoint ) == false;
                if ( bInvalidMusicLayer )
                {
                    SW_LOG_ERROR_ID( "AudioMusic.SegmentLayerNoClip", "%#: segment '%#' has a layer with no clip, or a parameter with an empty or unordered curve", sourceName, pName );
                    bValid = false;
                }
            }
            if ( segment._next.empty() == false && findSegmentIndex( segment._next ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "AudioMusic.SegmentMovesUnknownSegment", "%#: segment '%#' moves to unknown segment '%#'", sourceName, pName, segment._next.c_str() );
                bValid = false;
            }
        }
        const hashed_string anyName( "*" );
        for ( const AudioMusicTransitionDef& transition : _listTransition )
        {
            const bool bFromKnown = transition._from.empty() || transition._from == anyName || findSegmentIndex( transition._from ).has_value();
            if ( bFromKnown == false || findSegmentIndex( transition._to ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "AudioMusic.TransitionNamesUnknownSegment", "%#: transition '%#' -> '%#' names an unknown segment", sourceName, transition._from.c_str(), transition._to.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    optional<uint32> AudioMusicDef::findSegmentIndex( const hashed_string& name ) const
    {
        for ( size_t segmentIndex = 0; segmentIndex < _listSegment.size(); ++segmentIndex )
        {
            if ( _listSegment[segmentIndex]._name == name )
                return static_cast<uint32>( segmentIndex );
        }
        return nullopt;
    }

    const AudioMusicTransitionDef* AudioMusicDef::findTransition( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string            anyName( "*" );
        const AudioMusicTransitionDef* pAny = nullptr;
        for ( const AudioMusicTransitionDef& transition : _listTransition )
        {
            if ( transition._to != to )
                continue;
            if ( transition._from == from && from.empty() == false )
                return &transition;
            const bool bAny = transition._from.empty() || transition._from == anyName;
            if ( bAny && pAny == nullptr )
                pAny = &transition;
        }
        return pAny;
    }

    float64 AudioMusicDef::computeFramesPerBeat( uint32 segmentIndex ) const
    {
        const float32 segmentTempo = _listSegment[segmentIndex]._tempo;
        const float32 tempo        = segmentTempo > 0.0f ? segmentTempo : _tempo;
        return 60.0 / static_cast<float64>( tempo ) * static_cast<float64>( audio::kSampleRate );
    }

    uint32 AudioMusicDef::getBeatsPerBar( uint32 segmentIndex ) const
    {
        const uint32 segmentBeats = _listSegment[segmentIndex]._beatsPerBar;
        return segmentBeats > 0 ? segmentBeats : _beatsPerBar;
    }

    uint64 AudioMusicDef::computeSegmentFrames( uint32 segmentIndex ) const
    {
        const float64 frames = computeFramesPerBeat( segmentIndex ) * static_cast<float64>( getBeatsPerBar( segmentIndex ) ) *
                               static_cast<float64>( _listSegment[segmentIndex]._bars );
        return static_cast<uint64>( frames + 0.5 );
    }

    /** @brief 오디오 음악 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_ASSET_TYPE( AudioMusic, AssetTypeDef::makeDataFile( "audiomusic", AssetPathRule{ {}, {}, ".music.xml" }, AssetTypeLoader::of<AudioMusicDef>() ) );
} // namespace sw
