#include "pch.h"

#include "Engine/Graphics/RenderPass/RenderPassManager.h"

#include "Engine/Graphics/RenderPass/RenderPassResource.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPassManager" );

    RenderPassManager::RenderPassManager()  = default;
    RenderPassManager::~RenderPassManager() = default;

    bool RenderPassManager::initialize()
    {
        SW_LOG_INFO( "Subsystem Initialized." );
        return true;
    }

    void RenderPassManager::shutdown()
    {
        clearCache();
        SW_LOG_INFO( "Subsystem Shutdown Cleanly." );
    }

    RenderPassResource* RenderPassManager::loadRenderPass( string_view assetRelativePath )
    {
        if ( assetRelativePath.empty() )
            return nullptr;

        const string pathKey{ assetRelativePath };
        const auto   pathIt = _mapPathToRenderPass.find( pathKey );
        if ( pathIt != _mapPathToRenderPass.end() )
            return pathIt->second;

        unique_ptr<RenderPassResource> res = make_unique<RenderPassResource>();
        if ( res->loadFromXmlFile( pathKey ) == false )
            return nullptr;

        hashed_string       key( res->getDesc()._name.c_str() );
        RenderPassResource* pExisting = findRenderPass( key );
        if ( pExisting != nullptr )
        {
            _mapPathToRenderPass[pathKey] = pExisting;
            return pExisting;
        }

        RenderPassResource* ptr = res.get();
        _mapRenderPass.try_emplace( key, std::move( res ) );
        _mapPathToRenderPass[pathKey] = ptr;
        return ptr;
    }

    RenderPipelineResource* RenderPassManager::loadPipeline( string_view assetRelativePath )
    {
        if ( assetRelativePath.empty() )
            return nullptr;

        const string pathKey{ assetRelativePath };
        const auto   pathIt = _mapPathToPipeline.find( pathKey );
        if ( pathIt != _mapPathToPipeline.end() )
            return pathIt->second;

        unique_ptr<RenderPipelineResource> res = make_unique<RenderPipelineResource>();
        if ( res->loadFromXmlFile( pathKey ) == false )
            return nullptr;

        hashed_string           key( res->getDesc()._name.c_str() );
        RenderPipelineResource* pExisting = findPipeline( key );
        if ( pExisting != nullptr )
        {
            _mapPathToPipeline[pathKey] = pExisting;
            return pExisting;
        }

        RenderPipelineResource* ptr = res.get();
        _mapPipeline.try_emplace( key, std::move( res ) );
        _mapPathToPipeline[pathKey] = ptr;
        return ptr;
    }

    void RenderPassManager::clearCache()
    {
        _mapPipeline.clear();
        _mapRenderPass.clear();
        _mapPathToPipeline.clear();
        _mapPathToRenderPass.clear();
    }

    RenderPassResource* RenderPassManager::findRenderPass( hashed_string name )
    {
        auto it = _mapRenderPass.find( name );
        if ( it != _mapRenderPass.end() )
            return it->second.get();
        return nullptr;
    }

    RenderPipelineResource* RenderPassManager::findPipeline( hashed_string name )
    {
        auto it = _mapPipeline.find( name );
        if ( it != _mapPipeline.end() )
            return it->second.get();
        return nullptr;
    }
} // namespace sw
