#include "pch.h"

#include "Engine/Audio/AudioMusic.h"

#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMusic" );

    bool AudioMusicDesc::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceUtil::readTextResource( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Music not found: %#", resourcePath );
            return false;
        }
        return loadFromXMLText( text, resourcePath );
    }

    bool AudioMusicDesc::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = AudioMusicDesc{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: music could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioMusicDesc::validate( string_view sourceName ) const
    {
        bool bValid = true;
        if ( _listSegment.empty() )
        {
            SW_LOG_ERROR( "%#: music has no segments", sourceName );
            return false;
        }
        if ( _tempo <= 0.0f || _beatsPerBar == 0 )
        {
            SW_LOG_ERROR( "%#: music needs a positive tempo and beats per bar", sourceName );
            bValid = false;
        }
        if ( _startSegment.empty() == false && findSegmentIndex( _startSegment ) < 0 )
        {
            SW_LOG_ERROR( "%#: start segment '%#' does not exist", sourceName, _startSegment.c_str() );
            bValid = false;
        }
        for ( size_t segmentIndex = 0; segmentIndex < _listSegment.size(); ++segmentIndex )
        {
            const AudioMusicSegmentDesc& segment = _listSegment[segmentIndex];
            const utf8*                  pName   = segment._name.c_str();
            if ( segment._name.empty() )
            {
                SW_LOG_ERROR( "%#: segment %# has no name", sourceName, segmentIndex );
                bValid = false;
            }
            for ( size_t otherIndex = segmentIndex + 1; otherIndex < _listSegment.size(); ++otherIndex )
            {
                if ( _listSegment[otherIndex]._name == segment._name )
                {
                    SW_LOG_ERROR( "%#: segment '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            if ( segment._listLayer.empty() || segment._bars == 0 )
            {
                SW_LOG_ERROR( "%#: segment '%#' needs at least one layer and one bar", sourceName, pName );
                bValid = false;
            }
            for ( const AudioMusicLayerDesc& layer : segment._listLayer )
            {
                const bool bCurveMissing = layer._parameter.empty() == false && layer._listPoint.empty();
                if ( layer._path.empty() || bCurveMissing || AudioMixerDesc::isCurveSorted( layer._listPoint ) == false )
                {
                    SW_LOG_ERROR( "%#: segment '%#' has a layer with no clip, or a parameter with an empty or unordered curve", sourceName, pName );
                    bValid = false;
                }
            }
            if ( segment._next.empty() == false && findSegmentIndex( segment._next ) < 0 )
            {
                SW_LOG_ERROR( "%#: segment '%#' moves to unknown segment '%#'", sourceName, pName, segment._next.c_str() );
                bValid = false;
            }
        }
        const hashed_string anyName( "*" );
        for ( const AudioMusicTransitionDesc& transition : _listTransition )
        {
            const bool bFromKnown = transition._from.empty() || transition._from == anyName || findSegmentIndex( transition._from ) >= 0;
            if ( bFromKnown == false || findSegmentIndex( transition._to ) < 0 )
            {
                SW_LOG_ERROR( "%#: transition '%#' -> '%#' names an unknown segment", sourceName, transition._from.c_str(), transition._to.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    int32 AudioMusicDesc::findSegmentIndex( const hashed_string& name ) const
    {
        for ( size_t segmentIndex = 0; segmentIndex < _listSegment.size(); ++segmentIndex )
        {
            if ( _listSegment[segmentIndex]._name == name )
                return static_cast<int32>( segmentIndex );
        }
        return -1;
    }

    const AudioMusicTransitionDesc* AudioMusicDesc::findTransition( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string             anyName( "*" );
        const AudioMusicTransitionDesc* pAny = nullptr;
        for ( const AudioMusicTransitionDesc& transition : _listTransition )
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

    float64 AudioMusicDesc::computeFramesPerBeat( uint32 segmentIndex ) const
    {
        const float32 segmentTempo = _listSegment[segmentIndex]._tempo;
        const float32 tempo        = segmentTempo > 0.0f ? segmentTempo : _tempo;
        return 60.0 / static_cast<float64>( tempo ) * static_cast<float64>( audio::kSampleRate );
    }

    uint32 AudioMusicDesc::getBeatsPerBar( uint32 segmentIndex ) const
    {
        const uint32 segmentBeats = _listSegment[segmentIndex]._beatsPerBar;
        return segmentBeats > 0 ? segmentBeats : _beatsPerBar;
    }

    uint64 AudioMusicDesc::computeSegmentFrames( uint32 segmentIndex ) const
    {
        const float64 frames = computeFramesPerBeat( segmentIndex ) * static_cast<float64>( getBeatsPerBar( segmentIndex ) ) *
                               static_cast<float64>( _listSegment[segmentIndex]._bars );
        return static_cast<uint64>( frames + 0.5 );
    }
} // namespace sw
