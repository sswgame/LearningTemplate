#include "pch.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/Box2D/Box2DPhysicsBackend.h"
#include "Engine/Physics/Jolt/JoltPhysicsBackend.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsSystem" );

    PhysicsSystem::PhysicsSystem()
        : _settings{}
        , _bInitialized{ false }
    {
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    span<const utf8* const> PhysicsSystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { TaskManager::kSubsystemName };
        return kArrDependency;
    }

    bool PhysicsSystem::initialize( const ResourceID& resourceID )
    {
        const string_view settingsPath = resourceID.view();
        if ( _bInitialized )
            return true;
        SW_MEMORY_SCOPE( Physics );

        // 설정 표는 엔진 필수 데이터다. 없거나 틀리면 코드 기본 표로 대신하지 않고 기동이 실패한다.
        PhysicsSettings settings;
        if ( settings.loadFromResource( resourceID ) == false )
        {
            SW_LOG_ERROR( "Physics settings '%#' could not be loaded", settingsPath );
            return false;
        }
        _settings = settings;

        if ( JoltPhysicsBackend::initialize() == false )
            return false;
        _bInitialized = true;
        SW_LOG_INFO( "Physics ready: %# layers, %# materials, %# sub-steps per fixed step", _settings._listLayer.size(), _settings._listMaterial.size(), _settings._subStepCount );
        return true;
    }

    void PhysicsSystem::shutdown()
    {
        if ( _bInitialized == false )
            return;
        JoltPhysicsBackend::shutdown();
        _bInitialized = false;
    }

    unique_ptr<IPhysicsScene3D> PhysicsSystem::createScene3D() const
    {
        return createScene3D( _settings );
    }

    unique_ptr<IPhysicsScene3D> PhysicsSystem::createScene3D( const PhysicsSettings& settings ) const
    {
        if ( _bInitialized == false )
        {
            SW_LOG_ERROR( "createScene3D: the physics system is not initialized" );
            return nullptr;
        }
        if ( settings.validate() == false )
            return nullptr;
        return JoltPhysicsBackend::createScene( settings );
    }

    unique_ptr<IPhysicsScene2D> PhysicsSystem::createScene2D() const
    {
        return createScene2D( _settings );
    }

    unique_ptr<IPhysicsScene2D> PhysicsSystem::createScene2D( const PhysicsSettings& settings ) const
    {
        if ( _bInitialized == false )
        {
            SW_LOG_ERROR( "createScene2D: the physics system is not initialized" );
            return nullptr;
        }
        if ( settings.validate() == false )
            return nullptr;
        return Box2DPhysicsBackend::createScene( settings );
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
