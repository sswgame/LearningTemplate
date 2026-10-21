#include "pch.h"

#include "Engine/Renderer/Light/Light2DCookieAtlas.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "Light2DCookieAtlas" );

    namespace
    {
        struct Light2DCookieAtlasInternal
        {
            /** @brief 아틀라스 렌더 타깃의 이름입니다(텍스처 캐시 밖 — 렌더러가 소유한다). */
            static constexpr string_view kAtlasName = "light2dcookieatlas";

            static TextureCache* findTextureCache()
            {
                if ( engine::areEngineServicesBound() == false )
                    return nullptr;
                return &engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            }
        };
    } // namespace

    Light2DCookieAtlas::Light2DCookieAtlas() = default;

    Light2DCookieAtlas::~Light2DCookieAtlas() = default;

    void Light2DCookieAtlas::computeCellRect( uint32 cell, uint32& outX, uint32& outY, uint32& outSize )
    {
        outSize = shaderslot::kLight2DCookieAtlasSize / shaderslot::kLight2DCookieCellsPerRow;
        outX    = ( cell % shaderslot::kLight2DCookieCellsPerRow ) * outSize;
        outY    = ( cell / shaderslot::kLight2DCookieCellsPerRow ) * outSize;
    }

    uint32 Light2DCookieAtlas::findFreeCell() const
    {
        for ( uint32 cell = 0; cell < shaderslot::kLight2DCookieCellCount; ++cell )
        {
            bool bTaken = false;
            for ( const Entry& entry : _listEntry )
            {
                bTaken = bTaken || entry._cell == cell;
            }
            if ( bTaken == false )
                return cell;
        }
        return shaderslot::kLight2DCookieCellCount;
    }

    const Texture2D* Light2DCookieAtlas::findAtlas() const
    {
        return ( _atlas != nullptr && _atlas->isRHIValid() ) ? _atlas.get() : nullptr;
    }

    void Light2DCookieAtlas::assignCells( IRHIDevice* pDevice, const vector<GPULightCookie>& listCookie, vector<GPULight>& inoutListLight )
    {
        using Internal       = Light2DCookieAtlasInternal;
        _bHasCookies         = SW_FALSE;
        TextureCache* pCache = Internal::findTextureCache();
        if ( pDevice == nullptr || pCache == nullptr )
            return;
        for ( Entry& entry : _listEntry )
        {
            entry._bUsed = SW_FALSE;
        }

        if ( listCookie.empty() == false && _atlas == nullptr )
        {
            _atlas = make_unique<Texture2D>();
            if ( _atlas->createRenderTarget( pDevice, Internal::kAtlasName, shaderslot::kLight2DCookieAtlasSize, shaderslot::kLight2DCookieAtlasSize, RHIFormat::R8G8B8A8_UNORM ) ==
                 false )
            {
                SW_LOG_ERROR( "The 2D light cookie atlas render target could not be created - cookie lights draw without cookies" );
                _atlas.reset();
            }
        }
        const Texture2D* pAtlas = findAtlas();
        // 디바이스가 바뀌어 아틀라스를 새로 만들었으면 내용이 비었다 — 모든 칸을 다시 그린다.
        const RHITextureHandle atlasHandle  = pAtlas != nullptr ? pAtlas->getHandle() : RHITextureHandle{ 0 };
        const bool             bAtlasReborn = atlasHandle != _drawnAtlasHandle;
        _drawnAtlasHandle                   = atlasHandle;
        // 텍스처 캐시가 다시 읽었으면(핫 리로드) 같은 객체에 새 내용이다 — 모든 칸을 다시 그린다.
        const uint32 reloadGeneration = pCache->getReloadGeneration();
        const bool   bReloaded        = reloadGeneration != _reloadGeneration;
        _reloadGeneration             = reloadGeneration;

        for ( const GPULightCookie& cookie : listCookie )
        {
            if ( pAtlas == nullptr || cookie._lightIndex >= inoutListLight.size() )
                continue;
            Entry* pEntry = nullptr;
            for ( Entry& entry : _listEntry )
            {
                if ( entry._path == cookie._texturePath )
                    pEntry = &entry;
            }
            if ( pEntry == nullptr )
            {
                const uint32 cell = findFreeCell();
                if ( cell >= shaderslot::kLight2DCookieCellCount )
                {
                    if ( _bOverflowReported == SW_FALSE )
                        SW_LOG_ERROR( "2D light cookies need more than %# atlas cells ('%#' has none) - that light draws without its cookie",
                                      shaderslot::kLight2DCookieCellCount, cookie._texturePath.c_str() );
                    _bOverflowReported = SW_TRUE;
                    continue;
                }
                const Texture2D* pTexture = pCache->acquire( cookie._texturePath.c_str(), pDevice );
                if ( pTexture == nullptr )
                {
                    SW_LOG_ERROR( "2D light cookie '%#' could not be loaded - that light draws without its cookie", cookie._texturePath.c_str() );
                    continue;
                }
                _listEntry.push_back( Entry{ cookie._texturePath, pTexture, RHITextureHandle{ 0 }, cell, SW_FALSE } );
                pEntry = &_listEntry.back();
            }
            pEntry->_bUsed                               = SW_TRUE;
            inoutListLight[cookie._lightIndex]._extra._w = static_cast<float32>( pEntry->_cell );
            _bHasCookies                                 = SW_TRUE;
        }

        // 안 쓰인 쿠키는 칸과 텍스처를 돌려준다(다음 프레임에 다른 쿠키가 그 칸을 받는다).
        for ( size_t index = 0; index < _listEntry.size(); )
        {
            if ( _listEntry[index]._bUsed == SW_TRUE )
            {
                ++index;
                continue;
            }
            pCache->release( _listEntry[index]._path.c_str(), pDevice );
            _listEntry.erase( _listEntry.begin() + static_cast<std::ptrdiff_t>( index ) );
        }
        // 그릴 칸 — 새 칸, 텍스처가 다시 읽힌 칸(핸들이 바뀜), 아틀라스가 새로 만들어진 프레임의 모든 칸.
        for ( Entry& entry : _listEntry )
        {
            const RHITextureHandle handle  = entry._pTexture->getHandle();
            const bool             bRedraw = bAtlasReborn || bReloaded || handle != entry._drawnHandle;
            if ( bRedraw && handle != 0 )
            {
                _listPendingDraw.push_back( CellDraw{ entry._cell, entry._pTexture } );
                entry._drawnHandle = handle;
            }
        }
    }

    void Light2DCookieAtlas::release( IRHIDevice* pDevice )
    {
        TextureCache* pCache = Light2DCookieAtlasInternal::findTextureCache();
        for ( const Entry& entry : _listEntry )
        {
            if ( pCache != nullptr )
                pCache->release( entry._path.c_str(), pDevice );
        }
        _listEntry.clear();
        _listPendingDraw.clear();
        if ( _atlas != nullptr && pDevice != nullptr )
            _atlas->releaseRHI( pDevice );
        _atlas.reset();
        _drawnAtlasHandle = 0;
        _bHasCookies      = SW_FALSE;
    }
} // namespace sw
