#include "pch.h"

#include "Engine/Audio/AudioMixerDesc.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Asset/AssetIO.h"
#include "Engine/Audio/DSP/AudioEffect.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMixerDesc" );

    bool AudioMixerDesc::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Audio mixer not found: %#", assetPath );
            return false;
        }
        return loadFromXMLText( text, assetPath );
    }

    bool AudioMixerDesc::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = AudioMixerDesc{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: audio mixer could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    namespace
    {
        struct AudioMixerDescInternal
        {
            /** @brief 버스의 이펙트를 검사합니다. 알 수 없는 종류 · 겹치는 이름 · 모르는 파라미터를 알립니다. */
            [[nodiscard]] static bool validateEffects( const AudioBusDesc& bus, string_view sourceName )
            {
                bool bValid = true;
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
                    // 컨볼루션 리버브의 임펄스 응답은 이 종류에만 필수라 PROPERTY( Required ) 로 못 쓴다(모든 이펙트가 같은 서술 형식). 읽는 단계에서 막는다.
                    if ( effect._type == hashed_string( "ConvolutionReverb" ) && effect._impulse.isSet() == false )
                    {
                        SW_LOG_ERROR( "%#: effect '%#' on bus '%#' needs an impulse response clip (_impulse)", sourceName, effect.getEffectName().c_str(), bus._name.c_str() );
                        bValid = false;
                    }
                    for ( const AudioEffectParameterDesc& parameter : effect._listParameter )
                    {
                        bool bKnown = false;
                        for ( uint32 parameterIndex = 0; parameterIndex < pTypeInfo->_parameterCount; ++parameterIndex )
                        {
                            bKnown = bKnown || parameter._name == hashed_string( pTypeInfo->_pParameter[parameterIndex]._pName );
                        }
                        if ( bKnown == false )
                        {
                            SW_LOG_ERROR( "%#: effect '%#' on bus '%#' has no parameter '%#'", sourceName, effect._type.c_str(), bus._name.c_str(), parameter._name.c_str() );
                            bValid = false;
                        }
                    }
                }
                return bValid;
            }

            /** @brief 버스의 덕킹과 센드를 검사합니다. 모르는 버스 · 자기 자신 · 잘못된 설정을 알립니다. */
            [[nodiscard]] static bool validateRouting( const AudioMixerDesc& mixer, const AudioBusDesc& bus, string_view sourceName )
            {
                bool bValid = true;
                for ( const AudioDuckDesc& duck : bus._listDuck )
                {
                    if ( mixer.findBusIndex( duck._sourceBus ) < 0 )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' is ducked by unknown bus '%#'", sourceName, bus._name.c_str(), duck._sourceBus.c_str() );
                        bValid = false;
                    }
                    else if ( duck._sourceBus == bus._name )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' ducks itself", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                    const bool bInvalidDuckSettings = 0.0f < duck._depthDb || duck._attackSeconds < 0.0f || duck._releaseSeconds < 0.0f;
                    if ( bInvalidDuckSettings )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' has a duck with a positive depth or a negative time", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                }
                for ( const AudioSendDesc& send : bus._listSend )
                {
                    if ( mixer.findBusIndex( send._bus ) < 0 )
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
                return bValid;
            }

            /** @brief 버스 하나를 검사합니다(이름, 중복, 부모, 이펙트, 덕킹 · 센드). 루트 버스면 @p inoutRootCount 를 올립니다. */
            [[nodiscard]] static bool validateBus( const AudioMixerDesc& mixer, size_t busIndex, string_view sourceName, uint32& inoutRootCount )
            {
                const AudioBusDesc& bus = mixer._listBus[busIndex];
                if ( bus._name.empty() )
                {
                    SW_LOG_ERROR( "%#: bus %# has no name", sourceName, busIndex );
                    return false;
                }
                bool bValid = true;
                for ( size_t otherIndex = busIndex + 1; otherIndex < mixer._listBus.size(); ++otherIndex )
                {
                    if ( mixer._listBus[otherIndex]._name == bus._name )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' is declared twice", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                }
                if ( bus._parent.empty() )
                {
                    ++inoutRootCount;
                    if ( bus._name != hashed_string( AudioBusNames::kMaster ) )
                    {
                        SW_LOG_ERROR( "%#: bus '%#' has no parent - only '%#' may be the root", sourceName, bus._name.c_str(), AudioBusNames::kMaster );
                        bValid = false;
                    }
                }
                else if ( mixer.findBusIndex( bus._parent ) < 0 )
                {
                    SW_LOG_ERROR( "%#: bus '%#' names unknown parent '%#'", sourceName, bus._name.c_str(), bus._parent.c_str() );
                    bValid = false;
                }
                if ( validateEffects( bus, sourceName ) == false )
                    bValid = false;
                if ( validateRouting( mixer, bus, sourceName ) == false )
                    bValid = false;
                return bValid;
            }
        };
    } // namespace

    bool AudioMixerDesc::validate( string_view sourceName ) const
    {
        bool   bValid    = true;
        uint32 rootCount = 0;
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            if ( AudioMixerDescInternal::validateBus( *this, busIndex, sourceName, rootCount ) == false )
                bValid = false;
        }
        if ( rootCount != 1 || findBusIndex( hashed_string( AudioBusNames::kMaster ) ) < 0 )
        {
            SW_LOG_ERROR( "%#: the mixer needs exactly one root bus named '%#'", sourceName, AudioBusNames::kMaster );
            bValid = false;
        }
        // master 는 장치로 나간다 — 출력 레이아웃이 아니면 장치 버퍼와 채널 수가 어긋난다.
        const int32 masterIndex = findBusIndex( hashed_string( AudioBusNames::kMaster ) );
        if ( masterIndex >= 0 && _listBus[static_cast<size_t>( masterIndex )]._channels != AudioBusChannels::Output )
        {
            SW_LOG_ERROR( "%#: the '%#' bus feeds the device and must use the output channels (_channels=\"Output\")", sourceName, AudioBusNames::kMaster );
            bValid = false;
        }
        if ( validateAttenuations( _listAttenuation, sourceName ) == false )
            bValid = false;
        if ( bValid && validateSnapshots( sourceName ) == false )
            bValid = false;
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

    const AudioAttenuationDesc* AudioMixerDesc::findAttenuation( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioAttenuationDesc::_name>( _listAttenuation, name );
    }

    bool AudioMixerDesc::validateAttenuations( const vector<AudioAttenuationDesc>& listAttenuation, string_view sourceName )
    {
        bool bValid = true;
        for ( size_t attenuationIndex = 0; attenuationIndex < listAttenuation.size(); ++attenuationIndex )
        {
            const AudioAttenuationDesc& attenuation = listAttenuation[attenuationIndex];
            if ( attenuation._name.empty() )
            {
                SW_LOG_ERROR( "%#: attenuation %# has no name", sourceName, attenuationIndex );
                bValid = false;
            }
            for ( size_t otherIndex = attenuationIndex + 1; otherIndex < listAttenuation.size(); ++otherIndex )
            {
                if ( listAttenuation[otherIndex]._name == attenuation._name )
                {
                    SW_LOG_ERROR( "%#: attenuation '%#' is declared twice", sourceName, attenuation._name.c_str() );
                    bValid = false;
                }
            }
            if ( attenuation._maxDistance < attenuation._minDistance )
            {
                SW_LOG_ERROR( "%#: attenuation '%#' has a max distance below its min distance", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
            const bool bCustomSorted  = AudioMixerDesc::isCurveSorted( attenuation._listCustomPoint );
            const bool bLowPassSorted = AudioMixerDesc::isCurveSorted( attenuation._listLowPassPoint );
            if ( bCustomSorted == false || bLowPassSorted == false )
            {
                SW_LOG_ERROR( "%#: attenuation '%#' has curve points out of order", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
            if ( attenuation._curve == AudioAttenuationCurve::Custom && attenuation._listCustomPoint.empty() )
            {
                SW_LOG_ERROR( "%#: attenuation '%#' uses the Custom curve with no points", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    const AudioSnapshotDesc* AudioMixerDesc::findSnapshot( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioSnapshotDesc::_name>( _listSnapshot, name );
    }

    const AudioEffectDesc* AudioMixerDesc::findEffect( uint32 busIndex, const hashed_string& effectName ) const
    {
        for ( const AudioEffectDesc& effect : _listBus[busIndex]._listEffect )
        {
            if ( effect.getEffectName() == effectName )
                return &effect;
        }
        return nullptr;
    }

    bool AudioMixerDesc::validateSnapshots( string_view sourceName ) const
    {
        bool bValid = true;
        for ( size_t snapshotIndex = 0; snapshotIndex < _listSnapshot.size(); ++snapshotIndex )
        {
            const AudioSnapshotDesc& snapshot = _listSnapshot[snapshotIndex];
            const utf8*              pName    = snapshot._name.c_str();
            if ( snapshot._name.empty() )
            {
                SW_LOG_ERROR( "%#: snapshot %# has no name", sourceName, snapshotIndex );
                bValid = false;
            }
            for ( size_t otherIndex = snapshotIndex + 1; otherIndex < _listSnapshot.size(); ++otherIndex )
            {
                if ( _listSnapshot[otherIndex]._name == snapshot._name )
                {
                    SW_LOG_ERROR( "%#: snapshot '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotBusDesc& busVolume : snapshot._listBusVolume )
            {
                if ( findBusIndex( busVolume._bus ) < 0 )
                {
                    SW_LOG_ERROR( "%#: snapshot '%#' names unknown bus '%#'", sourceName, pName, busVolume._bus.c_str() );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotSendDesc& send : snapshot._listSend )
            {
                const int32 busIndex = findBusIndex( send._bus );
                bool        bFound   = false;
                if ( busIndex >= 0 )
                {
                    for ( const AudioSendDesc& existing : _listBus[static_cast<size_t>( busIndex )]._listSend )
                    {
                        bFound = bFound || existing._bus == send._target;
                    }
                }
                if ( bFound == false )
                {
                    SW_LOG_ERROR( "%#: snapshot '%#' changes send '%#' -> '%#' that the graph does not have", sourceName, pName, send._bus.c_str(), send._target.c_str() );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotEffectDesc& effect : snapshot._listEffectParameter )
            {
                const int32            busIndex = findBusIndex( effect._bus );
                const AudioEffectDesc* pEffect  = busIndex >= 0 ? findEffect( static_cast<uint32>( busIndex ), effect._effect ) : nullptr;
                bool                   bKnown   = false;
                if ( pEffect != nullptr )
                {
                    const AudioEffectTypeInfo* pTypeInfo = AudioEffectRegistry::findType( pEffect->_type );
                    for ( uint32 parameterIndex = 0; pTypeInfo != nullptr && parameterIndex < pTypeInfo->_parameterCount; ++parameterIndex )
                    {
                        bKnown = bKnown || effect._parameter == hashed_string( pTypeInfo->_pParameter[parameterIndex]._pName );
                    }
                }
                if ( bKnown == false )
                {
                    SW_LOG_ERROR( "%#: snapshot '%#' changes '%#.%#.%#' that the graph does not have", sourceName, pName, effect._bus.c_str(), effect._effect.c_str(),
                                  effect._parameter.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    bool AudioMixerDesc::isCurveSorted( const vector<AudioCurvePoint>& listPoint )
    {
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            if ( listPoint[pointIndex]._x < listPoint[pointIndex - 1]._x )
                return false;
        }
        return true;
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
        // 간선은 보내는 버스에서 받는 버스(부모, 센드 대상)로 향한다. 받는 쪽은 들어오는 간선이 모두 처리된 뒤에 처리한다(Kahn).
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
        const utf8* arrChildName[] = { AudioBusNames::kMusic, AudioBusNames::kSfx, AudioBusNames::kVoice, AudioBusNames::kAmbient, AudioBusNames::kUI };
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

namespace sw
{
    SW_ASSET_TYPE( AudioMixerDesc, AudioMixerDesc::kAssetType );

    /** @brief 오디오 믹서 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_RESOURCE_DATA_KIND( AudioMixer, "audiomixer", nullptr, nullptr, ".audiomixer.xml", &AssetDataKindUtil::loadCatalog<AudioMixerDesc> );
} // namespace sw
