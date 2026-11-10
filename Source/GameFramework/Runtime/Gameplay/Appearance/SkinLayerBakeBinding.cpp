#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayerBakeBinding.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Texture/TextureLayerBake.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

namespace sw
{
    namespace
    {
        struct SkinLayerBakeInternal
        {
            static constexpr uint64 kOwnerPathSalt = 0x534B4E4C4F574E52ull; ///< 번짐 굽기의 결과 경로를 주인마다 가르는 섞음 값("SKNLOWNR")

            /** @brief 텍스처 캐시입니다. 에셋 서비스가 내려갔으면(종료) nullptr 입니다. */
            static TextureCache* findTextureCache()
            {
                AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
                return pAssetManager != nullptr ? &pAssetManager->getAssetCache<TextureCache>() : nullptr;
            }

            static uint64 combineFloat( uint64 seed, float32 value ) { return HashUtil::combine( seed, static_cast<uint64>( BitUtil::floatToBits( value ) ) ); }

            static uint64 combineFloat4( uint64 seed, const float4& value )
            {
                seed = combineFloat( seed, value._x );
                seed = combineFloat( seed, value._y );
                seed = combineFloat( seed, value._z );
                return combineFloat( seed, value._w );
            }

            /** @brief 해석한 층 하나를 굽기 층 값으로 옮깁니다(번짐 경로는 부르는 쪽이 채운다). */
            static TextureLayerBakeLayer makeBakeLayer( const ResolvedSkinLayer& resolved )
            {
                TextureLayerBakeLayer layer{};
                layer._mask            = string( resolved._mask.c_str() );
                layer._color           = float4{ resolved._value._color._x, resolved._value._color._y, resolved._value._color._z, resolved._value._color._w * resolved._value._opacity };
                layer._offset          = resolved._value._offset;
                layer._scale           = resolved._value._scale;
                layer._rotationDegrees = resolved._value._rotationDegrees;
                layer._regionMin       = resolved._regionMin;
                layer._regionMax       = resolved._regionMax;
                layer._roughness       = resolved._roughness;
                layer._roughnessWeight = resolved._roughnessWeight;
                layer._metallic        = resolved._metallic;
                layer._metallicWeight  = resolved._metallicWeight;
                layer._blend           = static_cast<uint8>( resolved._blend );
                layer._bPlaced         = resolved._bPlaceable;
                layer._bMirror         = resolved._bPlaceable == SW_TRUE ? resolved._value._bMirror : SW_FALSE;
                return layer;
            }
        };
    } // namespace

    SkinLayerBakeBinding::~SkinLayerBakeBinding()
    {
        release( nullptr );
    }

    uint64 SkinLayerBakeBinding::computeStackKey( vector_reference<const ResolvedSkinLayer> listLayer, vector_reference<const uint32> listSmearRevision, string_view basePath, uint32 resolution, uint64 ownerKey )
    {
        using Internal = SkinLayerBakeInternal;
        uint64 key     = HashUtil::combine( hashed_string( basePath ).getHash(), static_cast<uint64>( resolution ) );
        bool   bSmear  = false;
        for ( size_t index = 0; index < listLayer.size(); ++index )
        {
            const ResolvedSkinLayer& layer = listLayer[index];
            SkinLayerValue           value = layer._value;
            value.quantize(); // 전송 정밀도 안의 차이는 같은 굽기다
            key                        = HashUtil::combine( key, layer._mask.getHash() );
            key                        = Internal::combineFloat4( key, value._color );
            key                        = Internal::combineFloat4( key, float4{ value._offset._x, value._offset._y, value._scale._x, value._scale._y } );
            key                        = Internal::combineFloat4( key, float4{ value._opacity, value._rotationDegrees, static_cast<float32>( value._bMirror ), static_cast<float32>( layer._bPlaceable ) } );
            key                        = Internal::combineFloat4( key, float4{ layer._regionMin._x, layer._regionMin._y, layer._regionMax._x, layer._regionMax._y } );
            key                        = Internal::combineFloat4( key, float4{ layer._roughness, layer._roughnessWeight, layer._metallic, layer._metallicWeight } );
            key                        = HashUtil::combine( key, static_cast<uint64>( layer._blend ) );
            const uint32 smearRevision = index < listSmearRevision.size() ? listSmearRevision[index] : 0u;
            key                        = HashUtil::combine( key, static_cast<uint64>( smearRevision ) );
            bSmear                     = bSmear || smearRevision != 0u;
        }
        // 번짐은 캐릭터마다의 일시 상태다 — 번호가 같아도 다른 캐릭터의 번짐과 내용이 다르다.
        return bSmear ? HashUtil::combine( key, ownerKey ) : key;
    }

    string SkinLayerBakeBinding::makeTargetPath( uint64 stackKey, string_view name )
    {
        StringBuilder<constant::buffer::kMax256> pathBuilder;
        pathBuilder.appendFormat( "%#skinlayer/%#_%#", TextureCache::kRenderTargetPrefix, stackKey, name );
        return string( pathBuilder.view() );
    }

    uint32 SkinLayerBakeBinding::syncSmearTexture( TextureCache& cache, uint64 ownerKey, uint32 layerIndex, const SkinLayerSmearState& smear )
    {
        if ( smear.isClean() || smear.getResolution() == 0 )
            return 0;
        if ( _listSmearTexture.size() <= layerIndex )
            _listSmearTexture.resize( layerIndex + 1u );
        SmearTexture& texture    = _listSmearTexture[layerIndex];
        const uint32  resolution = smear.getResolution();
        if ( texture._path.empty() )
        {
            StringBuilder<constant::buffer::kMax256> stemBuilder;
            stemBuilder.appendFormat( "skinsmear/%#/%#_", ownerKey, layerIndex );
            vector<uint8> listPixel;
            smear.makeTexturePixels( TextureLayerBakeConstants::kSmearOffsetRange, listPixel );
            const string path = cache.makeUniqueCPUTexturePath( stemBuilder.view() );
            if ( cache.createCPUTexture( path, resolution, resolution, std::move( listPixel ) ) == false )
            {
                SW_LOG_ERROR_ONCE_ID( "SkinLayerBake.SmearTexture", "Skin smear texture '%#' could not be created (the path is taken or the size is wrong) - that layer bakes without smear", path.c_str() );
                return 0;
            }
            texture._path     = path;
            texture._load     = cache.request( path, AssetLoadStart::Async );
            texture._revision = smear.getRevision();
        }
        else if ( texture._revision != smear.getRevision() )
        {
            vector<uint8> listPixel;
            smear.makeTexturePixels( TextureLayerBakeConstants::kSmearOffsetRange, listPixel );
            cache.requestRegionUpdate( texture._path, 0, 0, resolution, resolution, std::move( listPixel ) );
            texture._revision = smear.getRevision();
        }
        return texture._revision;
    }

    uint64 SkinLayerBakeBinding::findResultHandle( const BakeSet& set )
    {
        if ( set._listHandle.empty() || set._listHandle.front().isReady() == false )
            return 0;
        return set._listHandle.front().getOperation()->getResidentIdentity(); // GPU 핸들 값 — 게임은 RHI · Texture2D 를 모르고 같은지만 본다
    }

    bool SkinLayerBakeBinding::areInputsReady( const BakeSet& set )
    {
        bool bReady = true;
        for ( const TextureHandle& handle : set._listHandle )
        {
            if ( handle.hasFailed() )
                SW_LOG_ERROR_ONCE_FOR_ID( "SkinLayerBake.InputFailed", handle.getPath(), "Skin layer bake input '%#' could not be loaded (%#) - the skin layers are not baked",
                                          handle.getPath().c_str(), handle.getError().c_str() );
            bReady = bReady && handle.isReady();
        }
        return bReady;
    }

    void SkinLayerBakeBinding::apply( uint64 ownerKey, vector_reference<const ResolvedSkinLayer> listLayer, vector_reference<const SkinLayerSmearState> listSmear, uint32 resolution, MaterialInstance& instance )
    {
        using Internal       = SkinLayerBakeInternal;
        TextureCache* pCache = Internal::findTextureCache();
        if ( pCache == nullptr )
            return;
        if ( listLayer.empty() )
        {
            release( &instance );
            return;
        }
        const Material*         pParent   = instance.getParent();
        const MaterialProperty* pBaseProp = pParent != nullptr ? pParent->findProperty( hashed_string( kAlbedoParameter ) ) : nullptr;
        const string            basePath  = pBaseProp != nullptr ? pBaseProp->_assetPath : string{};
        if ( basePath.empty() || resolution == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "SkinLayerBake.NoBase", "The body material has no '%#' texture (or the bake resolution is 0) - skin layers have nothing to bake onto", kAlbedoParameter );
            return;
        }

        vector<uint32> listSmearRevision( listLayer.size(), 0u );
        for ( uint32 index = 0; index < listLayer.size() && index < listSmear.size(); ++index )
            listSmearRevision[index] = syncSmearTexture( *pCache, ownerKey, index, listSmear[index] );
        const uint64 key = computeStackKey( listLayer, listSmearRevision, basePath, resolution, ownerKey );

        // 1) 맡긴 굽기가 그려졌으면 머티리얼을 새 결과로 바꾼다 — 그 전에는 옛 결과를 그린다.
        if ( _pending._key != 0 && _pending._bSubmitted == SW_TRUE && pCache->wasLayerBakedAfter( _pending._albedoPath, _pending._submitSerial ) )
        {
            _pending._bakedHandle = findResultHandle( _pending );
            instance.setTextureParameter( hashed_string( kAlbedoParameter ), _pending._albedoPath );
            instance.setTextureParameter( hashed_string( kSurfaceParameter ), _pending._surfacePath );
            _applied = std::move( _pending ); // 옛 결과의 핸들은 여기서 놓인다(다른 캐릭터가 쥐지 않았으면 해제)
            _pending = BakeSet{};
        }
        // 2) 디바이스 교체로 결과가 다시 만들어졌으면(내용이 비었다) 같은 요청으로 다시 굽는다.
        const uint64 appliedHandle = findResultHandle( _applied );
        if ( _applied._key != 0 && appliedHandle != 0 && appliedHandle != _applied._bakedHandle )
        {
            pCache->requestLayerBake( _applied._request );
            _applied._bakedHandle = appliedHandle;
        }
        // 3) 열쇠가 바뀌었으면 새 굽기를 준비한다(돌아온 열쇠면 기다리던 것을 버린다).
        if ( key == _applied._key )
        {
            _pending = BakeSet{};
        }
        else if ( key != _pending._key )
        {
            // 번짐이 있으면 주인마다 한 벌을 계속 다시 굽는다(우는 동안 프레임마다 새 타깃을 만들지 않게), 없으면 열쇠마다 한 벌을 캐릭터끼리 나눈다.
            const bool   bSmear  = VectorUtil::containsIf( listSmearRevision, []( uint32 revision )
               { return revision != 0u; } );
            const uint64 pathKey = bSmear ? HashUtil::combine( ownerKey, Internal::kOwnerPathSalt ) : key;
            BakeSet      set{};
            set._key                     = key;
            set._albedoPath              = makeTargetPath( pathKey, "albedo" );
            set._surfacePath             = makeTargetPath( pathKey, "surface" );
            set._request._base           = basePath;
            set._request._albedoTarget   = set._albedoPath;
            set._request._surfaceTarget  = set._surfacePath;
            set._request._albedoScratch  = makeTargetPath( resolution, "scratch_albedo" ); // 작업 타깃은 같은 크기끼리 나눈다(렌더 스레드가 차례로 굽는다)
            set._request._surfaceScratch = makeTargetPath( resolution, "scratch_surface" );
            const string arrTarget[]     = { set._albedoPath, set._surfacePath, set._request._albedoScratch, set._request._surfaceScratch };
            for ( const string& target : arrTarget )
            {
                pCache->declareRenderTarget( target, resolution, resolution );
                set._listHandle.push_back( pCache->request( target, AssetLoadStart::Async ) ); // 결과가 맨 앞이다(`findResultHandle`)
            }
            set._listHandle.push_back( pCache->request( basePath, AssetLoadStart::Async ) );
            for ( uint32 index = 0; index < listLayer.size(); ++index )
            {
                TextureLayerBakeLayer layer = Internal::makeBakeLayer( listLayer[index] );
                if ( listSmearRevision[index] != 0u )
                    layer._smear = _listSmearTexture[index]._path; // 핸들은 번짐 텍스처가 쥔다
                set._listHandle.push_back( pCache->request( layer._mask, AssetLoadStart::Async ) );
                set._request._listLayer.push_back( std::move( layer ) );
            }
            _pending = std::move( set );
        }
        // 4) 입력이 모두 준비되면 맡긴다(렌더 스레드는 `find` 로만 읽는다).
        if ( _pending._key != 0 && _pending._bSubmitted == SW_FALSE && areInputsReady( _pending ) )
        {
            _pending._submitSerial = pCache->getLayerBakeSerial();
            pCache->requestLayerBake( _pending._request );
            _pending._bSubmitted = SW_TRUE;
        }
    }

    void SkinLayerBakeBinding::release( MaterialInstance* pInstance )
    {
        if ( pInstance != nullptr && _applied._key != 0 )
        {
            pInstance->setTextureParameter( hashed_string( kAlbedoParameter ), string_view{} ); // 빈 경로 = 덮어쓰기를 지운다(부모의 알베도로 돌아간다)
            pInstance->setTextureParameter( hashed_string( kSurfaceParameter ), string_view{} );
        }
        _applied             = BakeSet{};
        _pending             = BakeSet{};
        TextureCache* pCache = SkinLayerBakeInternal::findTextureCache();
        for ( SmearTexture& texture : _listSmearTexture )
        {
            texture._load = TextureHandle{};
            if ( pCache != nullptr && texture._path.empty() == false )
                pCache->releaseCPUTexture( texture._path );
        }
        _listSmearTexture.clear();
    }
} // namespace sw
