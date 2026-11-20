#include "pch.h"

#include "Engine/Physics/PhysicsSettings.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
} // namespace sw

namespace sw
{
    bool PhysicsSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        *this                       = PhysicsSettings{};
        if ( XMLSerializer::loadFromAsset( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsCouldNot", "Physics settings could not be read or hold unknown keys: %#", assetPath );
            return false;
        }
        return validate();
    }

    bool PhysicsSettings::loadFromXMLText( string_view xmlText )
    {
        *this = PhysicsSettings{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsTextCould", "Physics settings text could not be read or holds unknown keys" );
            return false;
        }
        return validate();
    }

    bool PhysicsSettings::validate() const
    {
        bool bValid = true;
        // 채널 · 프로필 · 재질 표는 데이터가 정한다. 비었다고 코드가 `Default` 를 채우지 않는다 — 빈 표는 데이터 오류다.
        if ( _listChannel.empty() || _listProfile.empty() || _listMaterial.empty() )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsDeclareLayers", "Physics settings declare %# collision channels, %# collision profiles and %# materials - at least one of each is required",
                             _listChannel.size(), _listProfile.size(), _listMaterial.size() );
            bValid = false;
        }
        if ( _listProfile.size() > kMaxLayerCount || _listChannel.size() > kMaxChannelCount )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsDeclareLayers2", "Physics settings declare %# collision profiles and %# channels - at most %# of each are supported",
                             _listProfile.size(), _listChannel.size(), kMaxLayerCount );
            bValid = false;
        }
        if ( _subStepCount == 0 || _subStepCount2D == 0 )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsNeedPositive", "Physics settings need a positive fixed step, at least one step per frame and one 2D sub-step" );
            bValid = false;
        }
        if ( _workerCount2D == 0 || _workerCount2D > kMaxWorkerCount2D )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsAskD", "Physics settings ask for %# 2D workers - use 1 to %#", _workerCount2D, kMaxWorkerCount2D );
            bValid = false;
        }
        if ( _characterChunkCount == 0 || _characterChunkCount > kMaxCharacterChunkCount || _characterChunkTempBytes == 0 )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.CharacterChunks", "Physics settings ask for %# character chunks of %# bytes - use 1 to %# chunks and a positive size",
                             _characterChunkCount, _characterChunkTempBytes, kMaxCharacterChunkCount );
            bValid = false;
        }
        // 3D 씬 용량은 데이터가 정한다 — 코드 기본값으로 채우지 않는다(감사 S-2).
        if ( _streamingFloorProbeDistance <= 0.0f )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.StreamingFloorProbeMissing", "Physics settings need a positive _streamingFloorProbeDistance (%#) - the ray that finds support under bodies over unloaded streaming cells",
                             _streamingFloorProbeDistance );
            bValid = false;
        }
        if ( _maxBodyCount == 0 || _maxBodyPairCount == 0 || _maxContactCount == 0 || _tempAllocatorBytes == 0 || _maxDebrisBodyCount == 0 )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsSettingsNeedCapacity",
                             "Physics settings need positive _maxBodyCount (%#), _maxBodyPairCount (%#), _maxContactCount (%#), _tempAllocatorBytes (%#) and _maxDebrisBodyCount (%#)",
                             _maxBodyCount, _maxBodyPairCount, _maxContactCount, _tempAllocatorBytes, _maxDebrisBodyCount );
            bValid = false;
        }
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            const CollisionChannelDef& channel = _listChannel[channelIndex];
            if ( channel._name.empty() )
            {
                SW_LOG_ERROR_ID( "PhysicsSettings.CollisionChannelNoName", "Collision channel %# has no name", channelIndex );
                bValid = false;
            }
            for ( size_t otherIndex = channelIndex + 1; otherIndex < _listChannel.size(); ++otherIndex )
            {
                if ( _listChannel[otherIndex]._name == channel._name )
                {
                    SW_LOG_ERROR_ID( "PhysicsSettings.CollisionChannelDeclaredTwice", "Collision channel '%#' is declared twice", channel._name.c_str() );
                    bValid = false;
                }
            }
        }
        for ( size_t profileIndex = 0; profileIndex < _listProfile.size(); ++profileIndex )
        {
            const CollisionProfileDef& profile = _listProfile[profileIndex];
            if ( profile._name.empty() )
            {
                SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsLayerNoName", "Collision profile %# has no name", profileIndex );
                bValid = false;
            }
            for ( size_t otherIndex = profileIndex + 1; otherIndex < _listProfile.size(); ++otherIndex )
            {
                if ( _listProfile[otherIndex]._name == profile._name )
                {
                    SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsLayerDeclaredTwice", "Collision profile '%#' is declared twice", profile._name.c_str() );
                    bValid = false;
                }
            }
            const CollisionChannelDef* pObjectChannel = findChannel( profile._objectChannel );
            if ( pObjectChannel == nullptr || pObjectChannel->_kind != CollisionChannelKind::Object )
            {
                SW_LOG_ERROR_ID( "PhysicsSettings.ProfileObjectChannel", "Collision profile '%#' names '%#' as its object channel - it must be a declared object channel", profile._name.c_str(),
                                 profile._objectChannel.c_str() );
                bValid = false;
            }
            for ( const CollisionResponseDef& response : profile._listResponse )
            {
                if ( findChannel( response._channel ) == nullptr )
                {
                    SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsLayerCollidesUnknown", "Collision profile '%#' gives a response to unknown channel '%#'", profile._name.c_str(),
                                     response._channel.c_str() );
                    bValid = false;
                }
            }
        }
        if ( _listGroundQueryLayer.empty() )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.GroundQueryLayersMissing", "PhysicsSettings _listGroundQueryLayer is empty - name the layers foot IK stands on (Default, Static)" );
            bValid = false;
        }
        for ( const hashed_string& groundName : _listGroundQueryLayer )
        {
            if ( findLayerIndex( groundName ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "PhysicsSettings.GroundQueryLayerUnknown", "PhysicsSettings _listGroundQueryLayer names unknown layer '%#'", groundName.c_str() );
                bValid = false;
            }
        }
        for ( size_t materialIndex = 0; materialIndex < _listMaterial.size(); ++materialIndex )
        {
            const PhysicsMaterialDef& material = _listMaterial[materialIndex];
            if ( material._name.empty() )
            {
                SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsMaterialNoName", "Physics material %# has no name", materialIndex );
                bValid = false;
            }
            for ( size_t otherIndex = materialIndex + 1; otherIndex < _listMaterial.size(); ++otherIndex )
            {
                if ( _listMaterial[otherIndex]._name == material._name )
                {
                    SW_LOG_ERROR_ID( "PhysicsSettings.PhysicsMaterialDeclaredTwice", "Physics material '%#' is declared twice", material._name.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    uint32 PhysicsSettings::makeGroundQueryLayerMask() const
    {
        uint32 mask = 0;
        for ( const hashed_string& groundName : _listGroundQueryLayer )
        {
            if ( const optional<uint8> layer = findLayerIndex( groundName ) )
                mask |= 1u << *layer;
        }
        return mask;
    }

    optional<uint8> PhysicsSettings::findLayerIndex( const hashed_string& name ) const
    {
        const size_t count = _listProfile.size() < kMaxLayerCount ? _listProfile.size() : kMaxLayerCount;
        for ( size_t layerIndex = 0; layerIndex < count; ++layerIndex )
        {
            if ( _listProfile[layerIndex]._name == name )
                return static_cast<uint8>( layerIndex );
        }
        return nullopt;
    }

    const CollisionChannelDef* PhysicsSettings::findChannel( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&CollisionChannelDef::_name>( _listChannel, name );
    }

    CollisionResponse PhysicsSettings::getProfileResponse( const CollisionProfileDef& profile, const hashed_string& channel ) const
    {
        if ( const CollisionResponseDef* pResponse = VectorUtil::findBy<&CollisionResponseDef::_channel>( profile._listResponse, channel ) )
            return pResponse->_response;
        const CollisionChannelDef* pChannel = findChannel( channel );
        return pChannel != nullptr ? pChannel->_defaultResponse : CollisionResponse::Ignore;
    }

    optional<uint32> PhysicsSettings::makeTraceLayerMask( const hashed_string& traceChannel ) const
    {
        const CollisionChannelDef* pChannel = findChannel( traceChannel );
        if ( pChannel == nullptr || pChannel->_kind != CollisionChannelKind::Trace )
        {
            SW_LOG_ERROR_ID( "PhysicsSettings.UnknownTraceChannel", "'%#' is not a trace channel of the physics settings", traceChannel.c_str() );
            return nullopt;
        }
        uint32       mask  = 0;
        const size_t count = _listProfile.size() < kMaxLayerCount ? _listProfile.size() : kMaxLayerCount;
        for ( size_t layerIndex = 0; layerIndex < count; ++layerIndex )
        {
            const CollisionProfileDef& profile = _listProfile[layerIndex];
            if ( profile._bQueryEnabled && getProfileResponse( profile, traceChannel ) == CollisionResponse::Block )
                mask |= 1u << layerIndex;
        }
        return mask;
    }

    const PhysicsMaterialDef* PhysicsSettings::findMaterial( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&PhysicsMaterialDef::_name>( _listMaterial, name );
    }

    CollisionLayers PhysicsSettings::makeCollisionLayers() const
    {
        CollisionLayers layers;
        for ( uint32 layerA = 0; layerA < CollisionLayers::kLayerCount; ++layerA )
        {
            for ( uint32 layerB = layerA; layerB < CollisionLayers::kLayerCount; ++layerB )
            {
                layers.setLayerCollision( static_cast<uint8>( layerA ), static_cast<uint8>( layerB ), false );
            }
        }
        // 두 프로필의 응답은 서로의 오브젝트 채널에 준 응답 중 약한 쪽이다(언리얼과 같다 — 한쪽이 무시하면 둘 다 보지 않는다).
        const size_t count = _listProfile.size() < kMaxLayerCount ? _listProfile.size() : kMaxLayerCount;
        for ( size_t layerA = 0; layerA < count; ++layerA )
        {
            const CollisionProfileDef& profileA = _listProfile[layerA];
            for ( size_t layerB = layerA; layerB < count; ++layerB )
            {
                const CollisionProfileDef& profileB = _listProfile[layerB];
                if ( profileA._bPhysicsEnabled == false || profileB._bPhysicsEnabled == false )
                    continue;
                const CollisionResponse response =
                    minResponse( getProfileResponse( profileA, profileB._objectChannel ), getProfileResponse( profileB, profileA._objectChannel ) );
                layers.setResponse( static_cast<uint8>( layerA ), static_cast<uint8>( layerB ), response );
            }
        }
        return layers;
    }

    /** @brief 물리 설정 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( PhysicsSettings, AssetTypeDef::makeDataFile( "physicssettings", AssetPathRule{ {}, {}, "physicssettings.xml" }, AssetTypeLoader::of<PhysicsSettings>() ) );
} // namespace sw
