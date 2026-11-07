#include "pch.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    PhysicsSystem::PhysicsSystem()
        : _settings{}
        , _bInitialized{ SW_FALSE }
    {
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    span<const utf8* const> PhysicsSystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { TaskManager::kSubsystemName, AssetManager::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> PhysicsSystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { SceneManager::kSubsystemName };
        return kArrDependent;
    }

    bool PhysicsSystem::initialize( const AssetID& assetID )
    {
        const string_view settingsPath = assetID.view();
        if ( _bInitialized )
            return true;
        SW_MEMORY_SCOPE( Physics );

        // 설정 표는 엔진 필수 데이터다. 없거나 틀리면 코드 기본 표로 대신하지 않고 기동이 실패한다.
        PhysicsSettings settings;
        if ( settings.loadFromAsset( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsSystem.PhysicsSettingsCouldNot", "Physics settings '%#' could not be loaded", settingsPath );
            return false;
        }
        _settings = settings;

        if ( PhysicsBackend::initialize3D() == false )
            return false;
        _bInitialized = SW_TRUE;
        SW_LOG_INFO_ID( "PhysicsSystem.PhysicsReadyLayersMaterials", "Physics ready: %# layers, %# materials, %# sub-steps per fixed step", _settings._listLayer.size(), _settings._listMaterial.size(), _settings._subStepCount );
        return true;
    }

    void PhysicsSystem::shutdown()
    {
        if ( _bInitialized == false )
            return;
        PhysicsBackend::shutdown3D();
        _bInitialized = SW_FALSE;
    }

    unique_ptr<IPhysicsScene3D> PhysicsSystem::createScene3D() const
    {
        return createScene3D( _settings );
    }

    unique_ptr<IPhysicsScene3D> PhysicsSystem::createScene3D( const PhysicsSettings& settings ) const
    {
        if ( _bInitialized == false )
        {
            SW_LOG_ERROR_ID( "PhysicsSystem.CreateScene3DPhysicsSystemNot", "createScene3D: the physics system is not initialized" );
            return nullptr;
        }
        if ( settings.validate() == false )
            return nullptr;
        return PhysicsBackend::createScene3D( settings );
    }

    unique_ptr<IPhysicsScene2D> PhysicsSystem::createScene2D() const
    {
        return createScene2D( _settings );
    }

    unique_ptr<IPhysicsScene2D> PhysicsSystem::createScene2D( const PhysicsSettings& settings ) const
    {
        if ( _bInitialized == false )
        {
            SW_LOG_ERROR_ID( "PhysicsSystem.CreateScene2DPhysicsSystemNot", "createScene2D: the physics system is not initialized" );
            return nullptr;
        }
        if ( settings.validate() == false )
            return nullptr;
        return PhysicsBackend::createScene2D( settings );
    }

    float3 PhysicsSystem::getConfiguredGravity()
    {
        if ( engine::areEngineServicesBound() )
            return engine::getSubsystem<PhysicsSystem>().getSettings()._gravity;
        return PhysicsSettings{}._gravity;
    }

    float32 PhysicsSystem::getConfiguredGravityMagnitude()
    {
        return getConfiguredGravity().getLength();
    }

    bool PhysicsSystem::setSettings( const PhysicsSettings& settings )
    {
        if ( settings.validate() == false )
            return false;
        _settings = settings;
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( PhysicsSystem, GameVisible, Startup, (), SW_ENGINE_DEFAULT_FACTORY( PhysicsSystem ) );
} // namespace sw

// 물리 설정은 상수로 읽는다(이름 검증은 서비스 없이도) - 쓰는 것만 담는 팩 쿠킹의 뿌리다(`AssetCookRoot.h`).
SW_ASSET_COOK_ROOT( PhysicsSettings, ::sw::PhysicsSystem::kSettingsPath );
