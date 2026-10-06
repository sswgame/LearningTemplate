#include "pch.h"

#include "Engine/Physics/PhysicsSystem.h"

#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/Box2D/Box2DPhysicsBackend.h"
#include "Engine/Physics/Jolt/JoltPhysicsBackend.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsSystem" );

    PhysicsSystem::PhysicsSystem()
        : _settings{}
        , _bInitialized{ false }
    {
        _settings.ensureDefaults();
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    bool PhysicsSystem::initialize( string_view settingsPath )
    {
        if ( _bInitialized )
            return true;
        SW_MEMORY_SCOPE( Physics );

        string text;
        if ( settingsPath.empty() == false && ResourceUtil::readTextResource( settingsPath, text ) )
        {
            PhysicsSettings settings;
            if ( settings.loadFromXmlText( text ) == false )
            {
                SW_LOG_ERROR( "Physics settings are invalid: %#", settingsPath );
                return false;
            }
            _settings = settings;
        }
        else
        {
            SW_LOG_WARNING( "Physics settings not found (%#) - using one Default layer and material", settingsPath );
            _settings = PhysicsSettings{};
            _settings.ensureDefaults();
        }

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
        return Box2DPhysicsBackend::createScene( settings );
    }

    float3 PhysicsSystem::getConfiguredGravity()
    {
        if ( engine::areEngineServicesBound() )
            return engine::getPhysicsSystem().getSettings()._gravity;
        return PhysicsSettings{}._gravity;
    }

    float32 PhysicsSystem::getConfiguredGravityMagnitude()
    {
        return getConfiguredGravity().getLength();
    }

    bool PhysicsSystem::setSettings( const PhysicsSettings& settings )
    {
        PhysicsSettings candidate = settings;
        candidate.ensureDefaults();
        if ( candidate.validate() == false )
            return false;
        _settings = candidate;
        return true;
    }
} // namespace sw
