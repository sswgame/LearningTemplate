#include "pch.h"

#include "Engine/Audio/AudioMixerDesc.h"

#include "Engine/Audio/Dsp/AudioEffect.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMixerDesc" );

    bool AudioMixerDesc::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceUtil::readTextResource( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Audio mixer not found: %#", resourcePath );
            return false;
        }
        return loadFromXmlText( text, resourcePath );
    }

    bool AudioMixerDesc::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        *this = AudioMixerDesc{};
        if ( XmlSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: audio mixer could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioMixerDesc::validate( string_view sourceName ) const
    {
        bool   bValid    = true;
        uint32 rootCount = 0;
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            const AudioBusDesc& bus = _listBus[busIndex];
            if ( bus._name.empty() )
            {
                SW_LOG_ERROR( "%#: bus %# has no name", sourceName, busIndex );
                bValid = false;
                continue;
            }
            for ( size_t otherIndex = busIndex + 1; otherIndex < _listBus.size(); ++otherIndex )
            {
                if ( _listBus[otherIndex]._name == bus._name )
                {
                    SW_LOG_ERROR( "%#: bus '%#' is declared twice", sourceName, bus._name.c_str() );
                    bValid = false;
                }
            }
            if ( bus._parent.empty() )
            {
                ++rootCount;
                if ( bus._name != hashed_string( AudioBusNames::kMaster ) )
                {
                    SW_LOG_ERROR( "%#: bus '%#' has no parent - only '%#' may be the root", sourceName, bus._name.c_str(), AudioBusNames::kMaster );
                    bValid = false;
                }
            }
            else if ( findBusIndex( bus._parent ) < 0 )
            {
                SW_LOG_ERROR( "%#: bus '%#' names unknown parent '%#'", sourceName, bus._name.c_str(), bus._parent.c_str() );
                bValid = false;
            }
            for ( size_t effectIndex = 0; effectIndex < bus._listEffect.size(); ++effectIndex )
            {
                const AudioEffectDesc&     effect    = bus._listEffect[effectIndex];
                const AudioEffectTypeInfo* pTypeInfo = AudioEffectRegistry::findType( effect._type );
                if ( pTypeInfo == nullptr )
                {
                    SW_LOG_ERROR( "%#: bus '%#' names unknown effect type '%#'", sourceName, bus._name.c_str(), effect._type.c_str() );
                    bValid = false;
                    continue;
                }
                for ( size_t otherIndex = effectIndex + 1; otherIndex < bus._listEffect.size(); ++otherIndex )
                {
                    if ( bus._listEffect[otherIndex].getEffectName() == effect.getEffectName() )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' has two effects named '%#' - give one a _name", sourceName, bus._name.c_str(), effect.getEffectName().c_str() );
                        bValid = false;
                    }
                }
                for ( const AudioEffectParameterDesc& parameter : effect._listParameter )
                {
                    bool bKnown = false;
                    for ( uint32 parameterIndex = 0; parameterIndex < pTypeInfo->_parameterCount; ++parameterIndex )
                        bKnown = bKnown || parameter._name == hashed_string( pTypeInfo->_pParameter[parameterIndex]._pName );
                    if ( bKnown == false )
                    {
                        SW_LOG_ERROR( "%#: effect '%#' on bus '%#' has no parameter '%#'", sourceName, effect._type.c_str(), bus._name.c_str(), parameter._name.c_str() );
                        bValid = false;
                    }
                }
            }
            for ( const AudioSendDesc& send : bus._listSend )
            {
                if ( findBusIndex( send._bus ) < 0 )
                {
                    SW_LOG_ERROR( "%#: bus '%#' sends to unknown bus '%#'", sourceName, bus._name.c_str(), send._bus.c_str() );
                    bValid = false;
                }
                else if ( send._bus == bus._name )
                {
                    SW_LOG_ERROR( "%#: bus '%#' sends to itself", sourceName, bus._name.c_str() );
                    bValid = false;
                }
            }
        }
        if ( rootCount != 1 || findBusIndex( hashed_string( AudioBusNames::kMaster ) ) < 0 )
        {
            SW_LOG_ERROR( "%#: the mixer needs exactly one root bus named '%#'", sourceName, AudioBusNames::kMaster );
            bValid = false;
        }
        if ( _maxVoiceCount == 0 || _maxRealVoiceCount == 0 )
        {
            SW_LOG_ERROR( "%#: voice limits must be at least 1", sourceName );
            bValid = false;
        }
        if ( bValid )
        {
            vector<uint32> listOrder;
            if ( makeProcessingOrder( listOrder ) == false )
            {
                SW_LOG_ERROR( "%#: bus parents and sends form a cycle", sourceName );
                bValid = false;
            }
        }
        return bValid;
    }

    int32 AudioMixerDesc::findBusIndex( const hashed_string& name ) const
    {
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            if ( _listBus[busIndex]._name == name )
                return static_cast<int32>( busIndex );
        }
        return -1;
    }

    bool AudioMixerDesc::makeProcessingOrder( vector<uint32>& outListBusIndex ) const
    {
        // 간선: 보내는 버스 → 받는 버스(부모 · 센드 대상). 받는 쪽은 들어오는 간선이 모두 처리된 뒤에 처리한다(Kahn).
        const size_t   busCount = _listBus.size();
        vector<uint32> listIncoming( busCount, 0u );
        for ( const AudioBusDesc& bus : _listBus )
        {
            const int32 parentIndex = findBusIndex( bus._parent );
            if ( parentIndex >= 0 )
                ++listIncoming[static_cast<size_t>( parentIndex )];
            for ( const AudioSendDesc& send : bus._listSend )
            {
                const int32 targetIndex = findBusIndex( send._bus );
                if ( targetIndex >= 0 )
                    ++listIncoming[static_cast<size_t>( targetIndex )];
            }
        }

        outListBusIndex.clear();
        outListBusIndex.reserve( busCount );
        vector<uint32> listReady;
        for ( size_t busIndex = 0; busIndex < busCount; ++busIndex )
        {
            if ( listIncoming[busIndex] == 0 )
                listReady.push_back( static_cast<uint32>( busIndex ) );
        }
        while ( listReady.empty() == false )
        {
            // 작은 번호부터 꺼내 순서를 데이터 순서에 묶는다(같은 파일이면 같은 순서).
            size_t pickSlot = 0;
            for ( size_t slot = 1; slot < listReady.size(); ++slot )
            {
                if ( listReady[slot] < listReady[pickSlot] )
                    pickSlot = slot;
            }
            const uint32 busIndex = listReady[pickSlot];
            listReady.erase( listReady.begin() + static_cast<ptrdiff_t>( pickSlot ) );
            outListBusIndex.push_back( busIndex );

            const AudioBusDesc& bus         = _listBus[busIndex];
            const int32         parentIndex = findBusIndex( bus._parent );
            if ( parentIndex >= 0 && --listIncoming[static_cast<size_t>( parentIndex )] == 0 )
                listReady.push_back( static_cast<uint32>( parentIndex ) );
            for ( const AudioSendDesc& send : bus._listSend )
            {
                const int32 targetIndex = findBusIndex( send._bus );
                if ( targetIndex >= 0 && --listIncoming[static_cast<size_t>( targetIndex )] == 0 )
                    listReady.push_back( static_cast<uint32>( targetIndex ) );
            }
        }
        return outListBusIndex.size() == busCount;
    }

    AudioMixerDesc AudioMixerDesc::makeDefault()
    {
        AudioMixerDesc desc;
        AudioBusDesc   master;
        master._name = hashed_string( AudioBusNames::kMaster );
        desc._listBus.push_back( master );
        const utf8* arrChildName[] = { AudioBusNames::kMusic, AudioBusNames::kSfx, AudioBusNames::kVoice, AudioBusNames::kAmbient, AudioBusNames::kUi };
        for ( const utf8* pName : arrChildName )
        {
            AudioBusDesc child;
            child._name   = hashed_string( pName );
            child._parent = master._name;
            desc._listBus.push_back( child );
        }
        return desc;
    }
} // namespace sw
