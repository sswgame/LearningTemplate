#include "pch.h"

#include "Engine/Environment/EnvironmentUtil.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "EnvironmentUtil" );

    /** @brief `-gv_environmentAnimate=0` — 바람 · 파도 시간을 0 에 멈춥니다(백엔드 스크린샷 비교 · 결정적 측정). */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_environmentAnimate, 1, "바람 · 파도 시간이 흐른다 (0=멈춤, 스크린샷 비교용)", SW_KEEP_IN_SHIPPING );

    bool EnvironmentUtil::isAnimationEnabled()
    {
        return gv_environmentAnimate != 0;
    }

    bool EnvironmentUtil::findViewPosition( GameObjectManager& manager, float3& outPosition )
    {
        const CameraRegistry& registry = manager.getCameraRegistry();
        CameraComponent*      pCamera  = registry.selectCamera( CameraRole::Game );
        if ( pCamera == nullptr && registry.getAll().empty() == false )
            pCamera = registry.getAll().front();
        if ( pCamera == nullptr )
            return false;
        outPosition = pCamera->getCameraPosition();
        return true;
    }

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
        if ( path.empty() || engine::areEngineServicesBound() == false )
            return false;
        const hashed_string hashedPath( string{ path }.c_str() );
        MaterialCache&      cache = engine::getAssetManager().getMaterialManager();
        _pMaterial                = cache.acquire( hashedPath.c_str(), nullptr );
        if ( _pMaterial == nullptr )
        {
            SW_LOG_WARNING( "Material '%#' could not be acquired - the scene default material is used", hashedPath.c_str() );
            return false;
        }
        cache.requestInitialize( hashedPath.c_str() );
        _acquiredPath = hashedPath;
        _instance     = MaterialInstance::create( _pMaterial );
        return true;
    }

    void EnvironmentMaterial::release()
    {
        // 인스턴스를 먼저 놓는다 — 머티리얼을 놓는 순간 캐시가 지울 수 있다.
        _instance.reset();
        _pMaterial = nullptr;
        if ( _acquiredPath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
            engine::getAssetManager().getMaterialManager().release( _acquiredPath.c_str() );
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
