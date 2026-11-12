#include "pch.h"

#include "Engine/Renderer/Frame/Packet/PostProcessMaterialSet.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    PostProcessMaterialSet::PostProcessMaterialSet()
        : _listEntry{}
        , _frame{ 1 }
    {
    }

    PostProcessMaterialSet::~PostProcessMaterialSet()
    {
        releaseAll();
    }

    PostProcessMaterialSet::Entry* PostProcessMaterialSet::findOrAcquire( const hashed_string& path )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._path == path )
                return &entry;
        }
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        MaterialCache& cache     = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        Material*      pMaterial = cache.acquire( path.view() );
        if ( pMaterial == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "PostProcessMaterialSet.CouldNotAcquire", "Post process material '%#' could not be acquired - the blendable is not drawn", path.c_str() );
            return nullptr;
        }
        cache.requestInitialize( path.view() );
        _listEntry.push_back( Entry{ path, pMaterial, _frame, SW_FALSE } );
        return &_listEntry.back();
    }

    void PostProcessMaterialSet::resolve( RenderViewSettings& inoutSettings )
    {
        inoutSettings._listPostProcessMaterial.clear();
        for ( const PostProcessBlendable& blendable : inoutSettings._postProcess._listBlendable )
        {
            if ( blendable.isActive() == false )
                continue;
            Entry* pEntry = findOrAcquire( hashed_string( blendable._materialPath.c_str() ) );
            if ( pEntry == nullptr )
                continue;
            pEntry->_lastUsedFrame = _frame;
            Material& material     = *pEntry->_pMaterial;
            // 읽는 중이면 이번 프레임은 싣지 않는다(다음 프레임에 실린다). 실패는 머티리얼이 이유를 남겼다.
            if ( material.getInitState() != MaterialInitState::Ready )
                continue;
            if ( VectorUtil::contains( material.getPermutations()._listAlwaysDefine, string( kMaterialDomainPostProcessDefine ) ) == false )
            {
                if ( pEntry->_bDomainReported == SW_FALSE )
                    SW_LOG_ERROR_ID( "PostProcessMaterialSet.NotPostProcessDomain", "Blendable material '%#' is not a PostProcess domain material (no %# in _alwaysDefines) - it is not drawn", pEntry->_path.c_str(),
                                     kMaterialDomainPostProcessDefine );
                pEntry->_bDomainReported = SW_TRUE;
                continue;
            }
            PostProcessMaterialDraw draw;
            draw._binding      = material.getRenderBinding();
            draw._shaderPath   = material.getShaderPath();
            draw._materialPath = pEntry->_path;
            draw._weight       = blendable._weight;
            draw._location     = blendable._location;
            if ( draw._binding.isValid() == false || draw._binding._data->_bytes.empty() )
                continue; // 레이아웃 오류 — 머티리얼이 이유를 남겼다
            inoutSettings._listPostProcessMaterial.push_back( std::move( draw ) );
        }
    }

    void PostProcessMaterialSet::endFrame()
    {
        // 이번 프레임에 어느 뷰도 쓰지 않은 머티리얼은 놓는다 — 볼륨을 나간 뒤에도 참조를 쥐면 머티리얼이 캐시에 남는다.
        const uint64 frame = _frame;
        for ( Entry& entry : _listEntry )
        {
            if ( entry._lastUsedFrame == frame || engine::areEngineServicesBound() == false )
                continue;
            engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( entry._path.view() );
            entry._pMaterial = nullptr;
        }
        VectorUtil::removeIf( _listEntry, []( const Entry& entry )
        { return entry._pMaterial == nullptr; } );
        ++_frame;
    }

    void PostProcessMaterialSet::releaseAll()
    {
        if ( engine::areEngineServicesBound() )
        {
            MaterialCache& cache = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
            for ( const Entry& entry : _listEntry )
            {
                cache.release( entry._path.view() );
            }
        }
        _listEntry.clear();
    }
} // namespace sw
