#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPipelineAssetCache.h"

#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPipelineAssetCache" );

    RenderPipelineAssetCache::RenderPipelineAssetCache()  = default;
    RenderPipelineAssetCache::~RenderPipelineAssetCache() = default;

    bool RenderPipelineAssetCache::initialize()
    {
        SW_LOG_INFO( "Subsystem Initialized." );
        return true;
    }

    void RenderPipelineAssetCache::shutdown()
    {
        clearCache();
        SW_LOG_INFO( "Subsystem Shutdown Cleanly." );
    }

    RenderPassAsset* RenderPipelineAssetCache::loadRenderPass( string_view assetRelativePath )
    {
        if ( assetRelativePath.empty() )
            return nullptr;

        const string pathKey{ assetRelativePath };
        const auto   pathIt = _mapPathToRenderPass.find( pathKey );
        if ( pathIt != _mapPathToRenderPass.end() )
            return pathIt->second;

        unique_ptr<RenderPassAsset> res = make_unique<RenderPassAsset>();
        if ( res->loadFromXmlFile( pathKey ) == false )
            return nullptr;

        hashed_string    key( res->getDesc()._name.c_str() );
        RenderPassAsset* pExisting = findRenderPass( key );
        if ( pExisting != nullptr )
        {
            _mapPathToRenderPass[pathKey] = pExisting;
            return pExisting;
        }

        RenderPassAsset* ptr = res.get();
        _mapRenderPass.try_emplace( key, std::move( res ) );
        _mapPathToRenderPass[pathKey] = ptr;
        return ptr;
    }

    RenderPipelineAsset* RenderPipelineAssetCache::loadPipeline( string_view assetRelativePath )
    {
        if ( assetRelativePath.empty() )
            return nullptr;

        const string pathKey{ assetRelativePath };
        const auto   pathIt = _mapPathToPipeline.find( pathKey );
        if ( pathIt != _mapPathToPipeline.end() )
            return pathIt->second;

        unique_ptr<RenderPipelineAsset> res = make_unique<RenderPipelineAsset>();
        if ( res->loadFromXmlFile( pathKey ) == false )
            return nullptr;

        hashed_string        key( res->getDesc()._name.c_str() );
        RenderPipelineAsset* pExisting = findPipeline( key );
        if ( pExisting != nullptr )
        {
            _mapPathToPipeline[pathKey] = pExisting;
            return pExisting;
        }

        RenderPipelineAsset* ptr = res.get();
        _mapPipeline.try_emplace( key, std::move( res ) );
        _mapPathToPipeline[pathKey] = ptr;
        return ptr;
    }

    void RenderPipelineAssetCache::clearCache()
    {
        _mapPipeline.clear();
        _mapRenderPass.clear();
        _mapPathToPipeline.clear();
        _mapPathToRenderPass.clear();
    }

    RenderPassAsset* RenderPipelineAssetCache::findRenderPass( hashed_string name )
    {
        auto it = _mapRenderPass.find( name );
        if ( it != _mapRenderPass.end() )
            return it->second.get();
        return nullptr;
    }

    RenderPipelineAsset* RenderPipelineAssetCache::findPipeline( hashed_string name )
    {
        auto it = _mapPipeline.find( name );
        if ( it != _mapPipeline.end() )
            return it->second.get();
        return nullptr;
    }
} // namespace sw
