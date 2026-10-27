#include "pch.h"

#include "Engine/Audio/AudioEvent.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEventLibrary" );

    bool AudioEventLibrary::loadFromResource( const ResourceID& resourceID )
    {
        const string_view resourcePath = resourceID.view();
        string            text;
        if ( ResourceIO::readTextResource( resourceID, text ) == false )
        {
            SW_LOG_ERROR( "Audio event library not found: %#", resourcePath );
            return false;
        }
        return loadFromXMLText( text, resourcePath );
    }

    bool AudioEventLibrary::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = AudioEventLibrary{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
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
            bool bHasSubtitle = false;
            for ( const AudioClipEntry& clip : event._listClip )
            {
                if ( clip._path.empty() )
                {
                    SW_LOG_ERROR( "%#: event '%#' has a clip with no path", sourceName, pName );
                    bValid = false;
                }
                bHasSubtitle = bHasSubtitle || clip._subtitleKey.empty() == false;
            }
            if ( event._subtitleSpeakerKey.empty() == false && bHasSubtitle == false )
            {
                SW_LOG_ERROR( "%#: event '%#' names a subtitle speaker but no clip has a subtitle key", sourceName, pName );
                bValid = false;
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
        return VectorUtil::findBy<&AudioEventDesc::_name>( _listEvent, name );
    }

    AudioEventDesc* AudioEventLibrary::findEvent( const hashed_string& name )
    {
        return VectorUtil::findBy<&AudioEventDesc::_name>( _listEvent, name );
    }

    string AudioEventLibrary::toXMLText() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool AudioEventLibrary::saveToResource( string_view resourcePath ) const
    {
        if ( validate( resourcePath ) == false )
        {
            SW_LOG_ERROR( "%#: audio event library not saved - it does not pass validation", resourcePath );
            return false;
        }
        const string filePath = ResourcePaths::getWritePath( resourcePath );
        if ( FileUtil::writeTextFile( filePath, toXMLText() ) == false )
        {
            SW_LOG_ERROR( "%#: audio event library could not be written to '%#'", resourcePath, filePath );
            return false;
        }
        return true;
    }

    const AudioParameterDesc* AudioEventLibrary::findParameter( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioParameterDesc::_name>( _listParameter, name );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( AudioEventLibrary, AudioEventLibrary::kAssetType );

    /** @brief 오디오 이벤트 라이브러리 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_RESOURCE_DATA_KIND( AudioEvents, "audioevents", nullptr, nullptr, ".audioevents.xml", &ResourceDataKindUtil::loadCatalog<AudioEventLibrary> );
} // namespace sw
