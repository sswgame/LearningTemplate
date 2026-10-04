#include "pch.h"

#include "Engine/Audio/AudioEvent.h"

#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEventLibrary" );

    bool AudioEventLibrary::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceUtil::readTextResource( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Audio event library not found: %#", resourcePath );
            return false;
        }
        return loadFromXmlText( text, resourcePath );
    }

    bool AudioEventLibrary::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        *this = AudioEventLibrary{};
        if ( XmlSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: audio event library could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioEventLibrary::validate( string_view sourceName ) const
    {
        bool bValid = AudioMixerDesc::validateAttenuations( _listAttenuation, sourceName );
        for ( size_t parameterIndex = 0; parameterIndex < _listParameter.size(); ++parameterIndex )
        {
            const AudioParameterDesc& parameter = _listParameter[parameterIndex];
            if ( parameter._name.empty() )
            {
                SW_LOG_ERROR( "%#: parameter %# has no name", sourceName, parameterIndex );
                bValid = false;
            }
            for ( size_t otherIndex = parameterIndex + 1; otherIndex < _listParameter.size(); ++otherIndex )
            {
                if ( _listParameter[otherIndex]._name == parameter._name )
                {
                    SW_LOG_ERROR( "%#: parameter '%#' is declared twice", sourceName, parameter._name.c_str() );
                    bValid = false;
                }
            }
            const bool bRangeOrdered = parameter._minValue <= parameter._defaultValue && parameter._defaultValue <= parameter._maxValue;
            if ( bRangeOrdered == false )
            {
                SW_LOG_ERROR( "%#: parameter '%#' needs min <= default <= max", sourceName, parameter._name.c_str() );
                bValid = false;
            }
        }

        for ( size_t eventIndex = 0; eventIndex < _listEvent.size(); ++eventIndex )
        {
            const AudioEventDesc& event = _listEvent[eventIndex];
            if ( event._name.empty() )
            {
                SW_LOG_ERROR( "%#: event %# has no name", sourceName, eventIndex );
                bValid = false;
                continue;
            }
            const utf8* pName = event._name.c_str();
            for ( size_t otherIndex = eventIndex + 1; otherIndex < _listEvent.size(); ++otherIndex )
            {
                if ( _listEvent[otherIndex]._name == event._name )
                {
                    SW_LOG_ERROR( "%#: event '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            if ( event._listClip.empty() )
            {
                SW_LOG_ERROR( "%#: event '%#' has no clips", sourceName, pName );
                bValid = false;
            }
            for ( const AudioClipEntry& clip : event._listClip )
            {
                if ( clip._path.empty() )
                {
                    SW_LOG_ERROR( "%#: event '%#' has a clip with no path", sourceName, pName );
                    bValid = false;
                }
            }
            if ( event._volumeDbMax < event._volumeDbMin || event._pitchMax < event._pitchMin )
            {
                SW_LOG_ERROR( "%#: event '%#' has a volume or pitch range with max below min", sourceName, pName );
                bValid = false;
            }
            for ( const AudioParameterMapping& mapping : event._listParameterMap )
            {
                if ( findParameter( mapping._parameter ) == nullptr )
                {
                    SW_LOG_ERROR( "%#: event '%#' maps unknown parameter '%#'", sourceName, pName, mapping._parameter.c_str() );
                    bValid = false;
                }
                if ( mapping._listPoint.empty() || AudioMixerDesc::isCurveSorted( mapping._listPoint ) == false )
                {
                    SW_LOG_ERROR( "%#: event '%#' parameter curve '%#' is empty or out of order", sourceName, pName, mapping._parameter.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    const AudioEventDesc* AudioEventLibrary::findEvent( const hashed_string& name ) const
    {
        for ( const AudioEventDesc& event : _listEvent )
        {
            if ( event._name == name )
                return &event;
        }
        return nullptr;
    }

    const AudioParameterDesc* AudioEventLibrary::findParameter( const hashed_string& name ) const
    {
        for ( const AudioParameterDesc& parameter : _listParameter )
        {
            if ( parameter._name == name )
                return &parameter;
        }
        return nullptr;
    }
} // namespace sw
