#include "pch.h"

#include "Engine/Renderer/Canvas/CanvasTextureLeases.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

namespace sw
{
    SW_LOG_CALLER( "Canvas" );

    CanvasTextureLeases::CanvasTextureLeases()
        : _listPathTexture{}
        , _listTarget{}
    {
    }

    CanvasTextureLeases::~CanvasTextureLeases() = default;

    void CanvasTextureLeases::prepareTargets( IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache )
    {
        for ( CanvasTargetLease& state : _listTarget )
        {
            state._bSeen = SW_FALSE;
        }
        const bool bServices = engine::areEngineServicesBound();
        for ( const CanvasTargetDrawList& target : frame._listTarget )
        {
            CanvasTargetLease* pState = findTarget( target._targetPath );
            if ( pState == nullptr )
            {
                pState        = &_listTarget.emplace_back();
                pState->_path = target._targetPath;
            }
            pState->_bSeen = SW_TRUE;
            if ( bServices == false )
                continue;
            const uint32  width    = static_cast<uint32>( target._list._targetSize._x );
            const uint32  height   = static_cast<uint32>( target._list._targetSize._y );
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            if ( pState->_pTexture != nullptr )
            {
                // 위젯이 그리기 크기를 바꿨으면 텍스처를 그 크기로 다시 만든다(같은 객체, 새 GPU 자원). 다시 만든 텍스처는 비어 있으므로 다시 그린다.
                const bool bResized = width > 0 && height > 0 && ( pState->_pTexture->getWidth() != width || pState->_pTexture->getHeight() != height );
                if ( bResized && textures.resizeRenderTarget( target._targetPath.view(), width, height, &device ) )
                    pState->_drawnRevision = 0;
                continue;
            }
            // 머티리얼과 같은 경로로 캐시에서 빌린다 — 처음 만드는 크기는 위젯의 그리기 크기다.
            textures.declareRenderTarget( target._targetPath.view(), width, height );
            pState->_pTexture      = textures.acquire( target._targetPath.view(), &device );
            pState->_drawnRevision = 0;
            // 등록 때 한 번 대상 포맷을 셋업 PSO 와 대조한다 — 없으면 거절을 알리고 그리지 않는다(`CanvasRenderer::drawTargets` 는 다른 포맷의 PSO 로 대신 그리지 않는다).
            RHIPipelineStateHandle pso{ 0 };
            const bool             bHasPSO = pState->_pTexture != nullptr && psoCache.findOutputPSO( RenderPassType::Canvas, pState->_pTexture->getFormat(), pso ) && pso != 0;
            if ( pState->_pTexture != nullptr && bHasPSO == false )
                SW_LOG_ERROR( "Canvas target '%#' has format %# with no canvas PSO from setup - it is rejected and never drawn (add the format to FramePSOManager::collectOutputPSOJobs)",
                              target._targetPath.c_str(), static_cast<uint32>( pState->_pTexture->getFormat() ) );
        }

        // 이번 목록에 없는 대상(위젯 컴포넌트가 사라졌다)은 돌려준다.
        for ( size_t index = _listTarget.size(); index > 0; --index )
        {
            CanvasTargetLease& state = _listTarget[index - 1];
            if ( state._bSeen == SW_TRUE )
                continue;
            if ( state._pTexture != nullptr && bServices )
                engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().release( state._path.view(), &device );
            _listTarget.erase( _listTarget.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void CanvasTextureLeases::acquirePathTextures( IRHIDevice& device, const CanvasDrawList& list )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        for ( const CanvasBatch& batch : list._listBatch )
        {
            for ( uint32 slot = 0; slot < batch._textureCount; ++slot )
            {
                const hashed_string& path = batch._arrTexture[slot]._texturePath;
                if ( path.empty() || batch._arrTexture[slot]._texture != nullptr )
                    continue;
                bool bKnown = false;
                for ( const PathTexture& pathTexture : _listPathTexture )
                {
                    bKnown = bKnown || pathTexture._path == path;
                }
                if ( bKnown )
                    continue;
                // 렌더 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 다 읽기 전 프레임은 그 이미지를 빼고 그린다(언리얼 Slate 의 브러시도 텍스처가 스트리밍되는 동안 비어 있다).
                PathTexture& added = _listPathTexture.emplace_back();
                added._path        = path;
                added._load        = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().request( path.view(), &device, AssetLoadStart::Async );
            }
        }
    }

    CanvasTargetLease* CanvasTextureLeases::findTarget( const hashed_string& path )
    {
        return VectorUtil::findBy<&CanvasTargetLease::_path>( _listTarget, path );
    }

    RHIDescriptorIndex CanvasTextureLeases::findPathTextureSRV( const hashed_string& path ) const
    {
        for ( const PathTexture& pathTexture : _listPathTexture )
        {
            if ( pathTexture._path != path )
                continue;
            const Texture2D* pTexture = pathTexture._load.isReady() ? pathTexture._load.getBlock()->getTexture() : nullptr;
            return pTexture != nullptr && pTexture->isRHIValid() ? pTexture->getSRV() : kInvalidDescriptorIndex;
        }
        return kInvalidDescriptorIndex;
    }

    void CanvasTextureLeases::release( IRHIDevice* pDevice )
    {
        _listPathTexture.clear(); // 경로 이미지의 참조는 핸들이 쥐었다 — 놓으면 캐시에 돌려준다(`Material::releaseTextureAssets` 와 같다)
        if ( engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const CanvasTargetLease& state : _listTarget )
            {
                if ( state._pTexture != nullptr )
                    textures.release( state._path.view(), pDevice );
            }
        }
        _listTarget.clear();
    }
} // namespace sw
