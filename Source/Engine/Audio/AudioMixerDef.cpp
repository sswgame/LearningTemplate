#include "pch.h"

#include "Engine/Audio/AudioMixerDef.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DependencyGraph.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Audio/DSP/AudioEffect.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool AudioMixerDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AudioMixerDef.AudioMixerNotFound", "Audio mixer not found: %#", assetPath );
            return false;
        }
        return loadComposed( text, assetPath, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool AudioMixerDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool AudioMixerDef::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = AudioMixerDef{};
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), flatText.empty() ? xmlText : string_view( flatText ) ) == false )
        {
            SW_LOG_ERROR_ID( "AudioMixerDef.AudioMixerCouldNot", "%#: audio mixer could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    namespace
    {
        struct AudioMixerDescInternal
        {
            /** @brief 버스의 이펙트를 검사합니다. 알 수 없는 종류 · 겹치는 이름 · 모르는 파라미터를 알립니다. */
            [[nodiscard]] static bool validateEffects( const AudioBusDef& bus, string_view sourceName )
            {
                bool bValid = true;
                for ( size_t effectIndex = 0; effectIndex < bus._listEffect.size(); ++effectIndex )
                {
                    const AudioEffectDef&      effect    = bus._listEffect[effectIndex];
                    const AudioEffectTypeInfo* pTypeInfo = AudioEffectRegistry::findType( effect._type );
                    if ( pTypeInfo == nullptr )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusNamesUnknownEffect", "%#: bus '%#' names unknown effect type '%#'", sourceName, bus._name.c_str(), effect._type.c_str() );
                        bValid = false;
                        continue;
                    }
                    for ( size_t otherIndex = effectIndex + 1; otherIndex < bus._listEffect.size(); ++otherIndex )
                    {
                        if ( bus._listEffect[otherIndex].getEffectName() == effect.getEffectName() )
                        {
                            SW_LOG_ERROR_ID( "AudioMixerDef.BusTwoEffectsNamed", "%#: bus '%#' has two effects named '%#' - give one a _name", sourceName, bus._name.c_str(), effect.getEffectName().c_str() );
                            bValid = false;
                        }
                    }
                    // 컨볼루션 리버브의 임펄스 응답은 이 종류에만 필수라 PROPERTY( Required ) 로 못 쓴다(모든 이펙트가 같은 서술 형식). 읽는 단계에서 막는다.
                    if ( effect._type == hashed_string( "ConvolutionReverb" ) && effect._impulse.isSet() == false )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.EffectBusNeedsImpulse", "%#: effect '%#' on bus '%#' needs an impulse response clip (_impulse)", sourceName, effect.getEffectName().c_str(), bus._name.c_str() );
                        bValid = false;
                    }
                    for ( const AudioEffectParameterDef& parameter : effect._listParameter )
                    {
                        bool bKnown = false;
                        for ( uint32 parameterIndex = 0; parameterIndex < pTypeInfo->_parameterCount; ++parameterIndex )
                        {
                            bKnown = bKnown || parameter._name == hashed_string( pTypeInfo->_pParameter[parameterIndex]._pName );
                        }
                        if ( bKnown == false )
                        {
                            SW_LOG_ERROR_ID( "AudioMixerDef.EffectBusNoParameter", "%#: effect '%#' on bus '%#' has no parameter '%#'", sourceName, effect._type.c_str(), bus._name.c_str(), parameter._name.c_str() );
                            bValid = false;
                        }
                    }
                }
                return bValid;
            }

            /** @brief 버스의 덕킹과 센드를 검사합니다. 모르는 버스 · 자기 자신 · 잘못된 설정을 알립니다. */
            [[nodiscard]] static bool validateRouting( const AudioMixerDef& mixer, const AudioBusDef& bus, string_view sourceName )
            {
                bool bValid = true;
                for ( const AudioDuckDef& duck : bus._listDuck )
                {
                    if ( mixer.findBusIndex( duck._sourceBus ).has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusDuckedUnknownBus", "%#: bus '%#' is ducked by unknown bus '%#'", sourceName, bus._name.c_str(), duck._sourceBus.c_str() );
                        bValid = false;
                    }
                    else if ( duck._sourceBus == bus._name )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusDucksItself", "%#: bus '%#' ducks itself", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                    const bool bInvalidDuckSettings = 0.0f < duck._depthDb || duck._attackSeconds < 0.0f || duck._releaseSeconds < 0.0f;
                    if ( bInvalidDuckSettings )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusDuckPositiveDepth", "%#: bus '%#' has a duck with a positive depth or a negative time", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                }
                for ( const AudioSendDef& send : bus._listSend )
                {
                    if ( mixer.findBusIndex( send._bus ).has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusSendsUnknownBus", "%#: bus '%#' sends to unknown bus '%#'", sourceName, bus._name.c_str(), send._bus.c_str() );
                        bValid = false;
                    }
                    else if ( send._bus == bus._name )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusSendsItself", "%#: bus '%#' sends to itself", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                }
                return bValid;
            }

            /** @brief 버스 하나를 검사합니다(이름, 중복, 부모, 이펙트, 덕킹 · 센드). 루트 버스면 @p inoutRootCount 를 올립니다. */
            [[nodiscard]] static bool validateBus( const AudioMixerDef& mixer, size_t busIndex, string_view sourceName, uint32& inoutRootCount )
            {
                const AudioBusDef& bus = mixer._listBus[busIndex];
                if ( bus._name.empty() )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.BusNoName", "%#: bus %# has no name", sourceName, busIndex );
                    return false;
                }
                bool bValid = true;
                for ( size_t otherIndex = busIndex + 1; otherIndex < mixer._listBus.size(); ++otherIndex )
                {
                    if ( mixer._listBus[otherIndex]._name == bus._name )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusDeclaredTwice", "%#: bus '%#' is declared twice", sourceName, bus._name.c_str() );
                        bValid = false;
                    }
                }
                if ( bus._parent.empty() )
                {
                    ++inoutRootCount;
                    if ( bus._name != hashed_string( AudioBusNames::kMaster ) )
                    {
                        SW_LOG_ERROR_ID( "AudioMixerDef.BusNoParentOnly", "%#: bus '%#' has no parent - only '%#' may be the root", sourceName, bus._name.c_str(), AudioBusNames::kMaster );
                        bValid = false;
                    }
                }
                else if ( mixer.findBusIndex( bus._parent ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.BusNamesUnknownParent", "%#: bus '%#' names unknown parent '%#'", sourceName, bus._name.c_str(), bus._parent.c_str() );
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

    bool AudioMixerDef::validate( string_view sourceName ) const
    {
        bool   bValid    = true;
        uint32 rootCount = 0;
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            if ( AudioMixerDescInternal::validateBus( *this, busIndex, sourceName, rootCount ) == false )
                bValid = false;
        }
        if ( rootCount != 1 || findBusIndex( hashed_string( AudioBusNames::kMaster ) ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "AudioMixerDef.MixerNeedsExactlyOne", "%#: the mixer needs exactly one root bus named '%#'", sourceName, AudioBusNames::kMaster );
            bValid = false;
        }
        // master 는 장치로 나간다 — 출력 레이아웃이 아니면 장치 버퍼와 채널 수가 어긋난다.
        const optional<uint32> masterIndex = findBusIndex( hashed_string( AudioBusNames::kMaster ) );
        if ( masterIndex.has_value() && _listBus[*masterIndex]._channels != AudioBusChannels::Output )
        {
            SW_LOG_ERROR_ID( "AudioMixerDef.BusFeedsDeviceMust", "%#: the '%#' bus feeds the device and must use the output channels (_channels=\"Output\")", sourceName, AudioBusNames::kMaster );
            bValid = false;
        }
        if ( validateAttenuations( _listAttenuation, sourceName ) == false )
            bValid = false;
        if ( bValid && validateSnapshots( sourceName ) == false )
            bValid = false;
        if ( _maxVoiceCount == 0 || _maxRealVoiceCount == 0 )
        {
            SW_LOG_ERROR_ID( "AudioMixerDef.VoiceLimitsMustLeast", "%#: voice limits must be at least 1", sourceName );
            bValid = false;
        }
        if ( bValid )
        {
            vector<uint32> listOrder;
            if ( makeProcessingOrder( listOrder ) == false )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.BusParentsSendsForm", "%#: bus parents and sends form a cycle", sourceName );
                bValid = false;
            }
        }
        return bValid;
    }

    const AudioAttenuationDef* AudioMixerDef::findAttenuation( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioAttenuationDef::_name>( _listAttenuation, name );
    }

    bool AudioMixerDef::validateAttenuations( const vector<AudioAttenuationDef>& listAttenuation, string_view sourceName )
    {
        bool bValid = true;
        for ( size_t attenuationIndex = 0; attenuationIndex < listAttenuation.size(); ++attenuationIndex )
        {
            const AudioAttenuationDef& attenuation = listAttenuation[attenuationIndex];
            if ( attenuation._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.AttenuationNoName", "%#: attenuation %# has no name", sourceName, attenuationIndex );
                bValid = false;
            }
            for ( size_t otherIndex = attenuationIndex + 1; otherIndex < listAttenuation.size(); ++otherIndex )
            {
                if ( listAttenuation[otherIndex]._name == attenuation._name )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.AttenuationDeclaredTwice", "%#: attenuation '%#' is declared twice", sourceName, attenuation._name.c_str() );
                    bValid = false;
                }
            }
            if ( attenuation._maxDistance < attenuation._minDistance )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.AttenuationMaxDistanceBelow", "%#: attenuation '%#' has a max distance below its min distance", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
            const bool bCustomSorted  = AudioMixerDef::isCurveSorted( attenuation._listCustomPoint );
            const bool bLowPassSorted = AudioMixerDef::isCurveSorted( attenuation._listLowPassPoint );
            if ( bCustomSorted == false || bLowPassSorted == false )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.AttenuationCurvePointsOut", "%#: attenuation '%#' has curve points out of order", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
            if ( attenuation._curve == AudioAttenuationCurve::Custom && attenuation._listCustomPoint.empty() )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.AttenuationUsesCustomCurve", "%#: attenuation '%#' uses the Custom curve with no points", sourceName, attenuation._name.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    const AudioSnapshotDef* AudioMixerDef::findSnapshot( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AudioSnapshotDef::_name>( _listSnapshot, name );
    }

    const AudioEffectDef* AudioMixerDef::findEffect( uint32 busIndex, const hashed_string& effectName ) const
    {
        for ( const AudioEffectDef& effect : _listBus[busIndex]._listEffect )
        {
            if ( effect.getEffectName() == effectName )
                return &effect;
        }
        return nullptr;
    }

    bool AudioMixerDef::validateSnapshots( string_view sourceName ) const
    {
        bool bValid = true;
        for ( size_t snapshotIndex = 0; snapshotIndex < _listSnapshot.size(); ++snapshotIndex )
        {
            const AudioSnapshotDef& snapshot = _listSnapshot[snapshotIndex];
            const utf8*             pName    = snapshot._name.c_str();
            if ( snapshot._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioMixerDef.SnapshotNoName", "%#: snapshot %# has no name", sourceName, snapshotIndex );
                bValid = false;
            }
            for ( size_t otherIndex = snapshotIndex + 1; otherIndex < _listSnapshot.size(); ++otherIndex )
            {
                if ( _listSnapshot[otherIndex]._name == snapshot._name )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.SnapshotDeclaredTwice", "%#: snapshot '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotBusDef& busVolume : snapshot._listBusVolume )
            {
                if ( findBusIndex( busVolume._bus ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.SnapshotNamesUnknownBus", "%#: snapshot '%#' names unknown bus '%#'", sourceName, pName, busVolume._bus.c_str() );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotSendDef& send : snapshot._listSend )
            {
                const optional<uint32> busIndex = findBusIndex( send._bus );
                bool                   bFound   = false;
                if ( busIndex.has_value() )
                {
                    for ( const AudioSendDef& existing : _listBus[*busIndex]._listSend )
                    {
                        bFound = bFound || existing._bus == send._target;
                    }
                }
                if ( bFound == false )
                {
                    SW_LOG_ERROR_ID( "AudioMixerDef.SnapshotChangesSendGraph", "%#: snapshot '%#' changes send '%#' -> '%#' that the graph does not have", sourceName, pName, send._bus.c_str(), send._target.c_str() );
                    bValid = false;
                }
            }
            for ( const AudioSnapshotEffectDef& effect : snapshot._listEffectParameter )
            {
                const optional<uint32> busIndex = findBusIndex( effect._bus );
                const AudioEffectDef*  pEffect  = busIndex.has_value() ? findEffect( *busIndex, effect._effect ) : nullptr;
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
                    SW_LOG_ERROR_ID( "AudioMixerDef.SnapshotChangesGraphDoes", "%#: snapshot '%#' changes '%#.%#.%#' that the graph does not have", sourceName, pName, effect._bus.c_str(), effect._effect.c_str(),
                                     effect._parameter.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    bool AudioMixerDef::isCurveSorted( const vector<AudioCurvePoint>& listPoint )
    {
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            if ( listPoint[pointIndex]._x < listPoint[pointIndex - 1]._x )
                return false;
        }
        return true;
    }

    optional<uint32> AudioMixerDef::findBusIndex( const hashed_string& name ) const
    {
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            if ( _listBus[busIndex]._name == name )
                return static_cast<uint32>( busIndex );
        }
        return nullopt;
    }

    bool AudioMixerDef::makeProcessingOrder( vector<uint32>& outListBusIndex ) const
    {
        // 받는 버스(부모, 센드 대상)는 보내는 버스가 모두 처리된 뒤에 처리한다. 작은 번호부터 꺼내 순서를 데이터 순서에 묶는다(같은 파일이면 같은 합산 순서).
        DependencyGraph graph{ static_cast<uint32>( _listBus.size() ) };
        for ( size_t busIndex = 0; busIndex < _listBus.size(); ++busIndex )
        {
            const AudioBusDef&     bus         = _listBus[busIndex];
            const optional<uint32> parentIndex = findBusIndex( bus._parent );
            if ( parentIndex.has_value() )
                graph.addDependency( *parentIndex, static_cast<uint32>( busIndex ) );
            for ( const AudioSendDef& send : bus._listSend )
            {
                const optional<uint32> targetIndex = findBusIndex( send._bus );
                if ( targetIndex.has_value() )
                    graph.addDependency( *targetIndex, static_cast<uint32>( busIndex ) );
            }
        }
        DependencyOrder order = graph.sortInDataOrder();
        outListBusIndex       = std::move( order._listOrder );
        return order.isSorted();
    }

    AudioMixerDef AudioMixerDef::makeDefault()
    {
        AudioMixerDef desc;
        AudioBusDef   master;
        master._name = hashed_string( AudioBusNames::kMaster );
        desc._listBus.push_back( master );
        const utf8* arrChildName[] = { AudioBusNames::kMusic, AudioBusNames::kSFX, AudioBusNames::kVoice, AudioBusNames::kAmbient, AudioBusNames::kUI };
        for ( const utf8* pName : arrChildName )
        {
            AudioBusDef child;
            child._name   = hashed_string( pName );
            child._parent = master._name;
            desc._listBus.push_back( child );
        }
        return desc;
    }
} // namespace sw

namespace sw
{

    /** @brief 오디오 믹서 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_ASSET_TYPE( AudioMixerDef, AudioMixerDef::kAssetType.withPathRule( AssetPathRule{ {}, {}, ".audiomixer.xml" } ).withLoader( AssetTypeLoader::of<AudioMixerDef>() ) );
} // namespace sw
