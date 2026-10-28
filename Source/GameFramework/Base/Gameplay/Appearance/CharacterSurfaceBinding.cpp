#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterSurfaceBinding.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/3D/MeshComponent.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterSurfaceBinding" );

    namespace
    {
        /** @brief 텍스처 캐시입니다. 에셋 서비스가 내려갔으면(종료) nullptr 입니다. */
        TextureCache* findTextureCache()
        {
            AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
            return pAssetManager != nullptr ? &pAssetManager->getAssetCache<TextureCache>() : nullptr;
        }
    } // namespace
} // namespace sw

namespace sw
{
    hashed_string SurfaceParameterNames::makeChannel( const hashed_string& channel )
    {
        return hashed_string( ( string( "_surface" ) + channel.c_str() ).c_str() );
    }

    hashed_string SurfaceParameterNames::makeMask( const hashed_string& channel )
    {
        return hashed_string( ( string( "_surface" ) + channel.c_str() + "Mask" ).c_str() );
    }

    hashed_string SurfaceParameterNames::makeRegion( uint32 region, uint32 chunk )
    {
        return hashed_string( ( string( "_surfaceRegion" ) + to_string( region ) + "Chunk" + to_string( chunk ) ).c_str() );
    }

    string SurfaceParameterNames::makeMaskTexturePath( uint64 ownerKey, uint32 part, const hashed_string& channel )
    {
        return string( "cpu/surface/" ) + to_string( ownerKey ) + "/" + to_string( part ) + "/" + channel.c_str();
    }
} // namespace sw

namespace sw
{
    CharacterSurfaceBinding::~CharacterSurfaceBinding()
    {
        releaseTextures();
    }

    MaterialInstance* CharacterSurfaceBinding::acquireInstance( MeshComponent& mesh )
    {
        shared_ptr<MaterialInstance> instance = mesh.getMaterialInstance();
        if ( instance == nullptr )
        {
            if ( mesh.getMaterial() == nullptr )
                return nullptr;
            instance = MaterialInstance::create( mesh.getMaterial() );
            if ( instance == nullptr )
                return nullptr;
            mesh.setMaterialInstance( instance );
        }
        return instance.get(); // 메시가 쥐고 있다
    }

    void CharacterSurfaceBinding::applyValues( const CharacterSurfaceState& state, MaterialInstance& instance )
    {
        const vector<SurfaceChannelDef>& listChannel  = state.getChannels();
        const uint32                     channelCount = static_cast<uint32>( listChannel.size() );
        vector<float32>                  listValue;
        vector<float32>                  listMax( channelCount, 0.0f );
        for ( uint32 region = 0; region < state.getRegionCount(); ++region )
        {
            state.getRegionParameters( region, listValue );
            for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
            {
                listMax[channelIndex] = MathUtil::max( listMax[channelIndex], listValue[channelIndex] );
            }
            // 영역 벡터는 채널 순서로 네 개씩 묶는다.
            for ( uint32 chunk = 0; chunk * SurfaceParameterNames::kChannelsPerVector < channelCount; ++chunk )
            {
                float32 arrComponent[SurfaceParameterNames::kChannelsPerVector] = {};
                for ( uint32 lane = 0; lane < SurfaceParameterNames::kChannelsPerVector; ++lane )
                {
                    const uint32 channelIndex = chunk * SurfaceParameterNames::kChannelsPerVector + lane;
                    arrComponent[lane]        = channelIndex < channelCount ? listValue[channelIndex] : 0.0f;
                }
                instance.setVectorParameter( SurfaceParameterNames::makeRegion( region, chunk ), float4{ arrComponent[0], arrComponent[1], arrComponent[2], arrComponent[3] } );
            }
        }
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            instance.setScalarParameter( SurfaceParameterNames::makeChannel( listChannel[channelIndex]._name ), listMax[channelIndex] );
        }
    }

    void CharacterSurfaceBinding::makeMaskPixels( const SurfaceMask& mask, float32 maxValue, vector<uint8>& outListPixel )
    {
        const vector<float32>& listTexel = mask.getTexels();
        outListPixel.assign( listTexel.size() * 4, 0 );
        const float32 scale = maxValue > 0.0f ? 255.0f / maxValue : 0.0f;
        for ( size_t texelIndex = 0; texelIndex < listTexel.size(); ++texelIndex )
        {
            const float32 scaled             = MathUtil::clamp( listTexel[texelIndex] * scale, 0.0f, 255.0f );
            outListPixel[texelIndex * 4]     = static_cast<uint8>( MathUtil::round( scaled ) );
            outListPixel[texelIndex * 4 + 3] = 255;
        }
    }

    void CharacterSurfaceBinding::applyMasks( uint64 ownerKey, const CharacterSurfaceState& state, uint32 part, MaterialInstance& instance )
    {
        TextureCache* pCache = findTextureCache();
        if ( pCache == nullptr )
            return;
        vector<uint8> listPixel;
        for ( const SurfaceChannelDef& channel : state.getChannels() )
        {
            const SurfaceMask* pMask = state.findMask( part, channel._name );
            if ( pMask == nullptr )
                continue;
            const uint32 resolution = pMask->getResolution();
            MaskTexture* pEntry     = nullptr;
            for ( MaskTexture& known : _listMaskTexture )
            {
                if ( known._part == part && known._channel == channel._name )
                    pEntry = &known;
            }
            if ( pEntry == nullptr )
            {
                MaskTexture created;
                created._path    = SurfaceParameterNames::makeMaskTexturePath( ownerKey, part, channel._name );
                created._channel = channel._name;
                created._part    = part;
                makeMaskPixels( *pMask, channel._maxValue, listPixel );
                if ( pCache->createCPUTexture( created._path, resolution, resolution, listPixel ) == false )
                {
                    SW_LOG_ERROR( "Surface mask texture '%#' could not be created (the path is taken or the size is wrong)", created._path.c_str() );
                    continue;
                }
                created._revision = pMask->getRevision();
                _listMaskTexture.push_back( std::move( created ) );
                pEntry = &_listMaskTexture.back();
            }
            else if ( pEntry->_revision != pMask->getRevision() )
            {
                makeMaskPixels( *pMask, channel._maxValue, listPixel );
                pCache->requestRegionUpdate( pEntry->_path, 0, 0, resolution, resolution, listPixel );
                pEntry->_revision = pMask->getRevision();
            }
            instance.setTextureParameter( SurfaceParameterNames::makeMask( channel._name ), pEntry->_path );
        }
    }

    void CharacterSurfaceBinding::releaseTextures()
    {
        TextureCache* pCache = findTextureCache();
        if ( pCache != nullptr )
        {
            for ( const MaskTexture& texture : _listMaskTexture )
            {
                pCache->releaseCPUTexture( texture._path );
            }
        }
        _listMaskTexture.clear();
    }
} // namespace sw
