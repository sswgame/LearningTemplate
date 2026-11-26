#include "pch.h"

#include "Engine/Environment/EnvironmentMaterial.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"

namespace sw
{
    EnvironmentMaterial::EnvironmentMaterial()
        : _instance{}
        , _pMaterial{ nullptr }
        , _acquiredPath{}
    {
    }

    EnvironmentMaterial::~EnvironmentMaterial()
    {
        release();
    }

    bool EnvironmentMaterial::acquire( string_view path )
    {
        release();
        if ( path.empty() || engine::areEngineSubsystemsBound() == false )
            return false;
        const hashed_string hashedPath( string{ path }.c_str() );
        MaterialCache&      cache = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        _pMaterial                = cache.acquire( hashedPath.view() );
        if ( _pMaterial == nullptr )
        {
            SW_LOG_WARNING_ID( "EnvironmentMaterial.MaterialCouldNotAcquired", "Material '%#' could not be acquired - the scene default material is used", hashedPath.c_str() );
            return false;
        }
        cache.requestInitialize( hashedPath.view() );
        _acquiredPath = hashedPath;
        _instance     = MaterialInstance::create( _pMaterial );
        return true;
    }

    void EnvironmentMaterial::release()
    {
        // 인스턴스를 먼저 해제한다. 머티리얼을 해제하는 순간 캐시가 지울 수 있다.
        _instance.reset();
        _pMaterial = nullptr;
        if ( _acquiredPath.empty() )
            return;
        if ( engine::areEngineSubsystemsBound() )
            engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( _acquiredPath.view() );
        _acquiredPath = hashed_string{};
    }

    void EnvironmentMaterial::setVector( const hashed_string& name, const float4& value )
    {
        if ( _instance != nullptr )
            _instance->setVectorParameter( name, value );
    }

    void EnvironmentMaterial::setTexture( const hashed_string& name, string_view texturePath )
    {
        if ( _instance != nullptr && texturePath.empty() == false )
            _instance->setTextureParameter( name, texturePath );
    }
} // namespace sw
