#include "pch.h"

#include "Engine/Environment/SceneEnvironment.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneEnvironment, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SceneEnvironment::SceneEnvironment( GameObjectManager& manager )
        : _manager{ manager }
        , _surfaceDeform{}
        , _environment{}
        , _globalParameters{}
        , _bHasSurfaceDeform{ SW_FALSE }
        , _bGlobalParametersRequested{ SW_FALSE }
    {
    }

    void SceneEnvironment::update( float32 deltaTime )
    {
        const bool bFrozen = engine::areEngineServicesBound() && engine::getSubsystem<DeterministicRun>().getPolicy()._bFreezeEnvironmentTime == SW_TRUE;
        if ( bFrozen == false ) // 결정적 사진은 바람 시간을 0 에 멈춘다
            _environment._w += deltaTime;
        float3 viewPosition{};
        if ( EnvironmentUtil::findViewPosition( _manager, viewPosition ) )
            _environment = float4{ viewPosition._x, viewPosition._y, viewPosition._z, _environment._w };
    }

    void SceneEnvironment::onSceneCleared()
    {
        clearSurfaceDeform();
        _environment = float4{};
        _globalParameters.reset();
        _bGlobalParametersRequested = SW_FALSE;
    }

    void SceneEnvironment::setSurfaceDeform( const SurfaceDeformBinding& binding )
    {
        _surfaceDeform     = binding;
        _bHasSurfaceDeform = SW_TRUE;
    }

    void SceneEnvironment::clearSurfaceDeform()
    {
        _surfaceDeform     = SurfaceDeformBinding{};
        _bHasSurfaceDeform = SW_FALSE;
    }

    bool SceneEnvironment::findSky( SkyGradient& outGradient ) const
    {
        return SkyComponent::findSky( _manager, outGradient );
    }

    bool SceneEnvironment::findFog( const float3& viewPosition, FogSettings& outSettings ) const
    {
        return FogComponent::findFog( _manager, viewPosition, outSettings );
    }

    bool SceneEnvironment::findSurfaceDeform( SurfaceDeformBinding& outBinding ) const
    {
        outBinding = _surfaceDeform;
        return _bHasSurfaceDeform == SW_TRUE;
    }

    MaterialGlobals SceneEnvironment::collectMaterialGlobals()
    {
        if ( _bGlobalParametersRequested == SW_FALSE && engine::areEngineServicesBound() )
        {
            _bGlobalParametersRequested = SW_TRUE;
            _globalParameters           = MaterialParameterCollection::acquire( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._materialGlobals.get() } ); // 읽지 못하면 받기가 오류를 남긴다
        }
        MaterialGlobals globals;
        globals._environment = _environment;
        if ( _globalParameters != nullptr && _globalParameters->writeGlobals( globals ) == false )
            SW_LOG_ERROR_ONCE_ID( "SceneEnvironment.MaterialGlobalMissing", "The material globals collection (EngineDefaultAssets _materialGlobals) does not feed every MaterialGlobalSlot - the missing globals stay zero" );
        return globals;
    }
} // namespace sw
