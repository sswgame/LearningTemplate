#include "pch.h"

#include "Engine/Physics/PhysicsSettings.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsSettings" );

    namespace
    {
        struct PhysicsSettingsInternal
        {
            static const hashed_string& getDefaultName()
            {
                static const hashed_string s_defaultName{ "Default" };
                return s_defaultName;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PhysicsSettings::loadFromResource( string_view resourcePath )
    {
        *this = PhysicsSettings{};
        if ( XmlSerializer::loadFile( resourcePath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "Physics settings could not be read or hold unknown keys: %#", resourcePath );
            return false;
        }
        ensureDefaults();
        return validate();
    }

    bool PhysicsSettings::loadFromXmlText( string_view xmlText )
    {
        *this = PhysicsSettings{};
        if ( XmlSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "Physics settings text could not be read or holds unknown keys" );
            return false;
        }
        ensureDefaults();
        return validate();
    }

    bool PhysicsSettings::validate() const
    {
        bool bValid = true;
        if ( _listLayer.size() > kMaxLayerCount )
        {
            SW_LOG_ERROR( "Physics settings declare %# layers - at most %# are supported", _listLayer.size(), kMaxLayerCount );
            bValid = false;
        }
        if ( _subStepCount == 0 || _subStepCount2D == 0 )
        {
            SW_LOG_ERROR( "Physics settings need a positive fixed step, at least one step per frame and one 2D sub-step" );
            bValid = false;
        }
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const PhysicsLayerDef& layer = _listLayer[layerIndex];
            if ( layer._name.empty() )
            {
                SW_LOG_ERROR( "Physics layer %# has no name", layerIndex );
                bValid = false;
            }
            for ( size_t otherIndex = layerIndex + 1; otherIndex < _listLayer.size(); ++otherIndex )
            {
                if ( _listLayer[otherIndex]._name == layer._name )
                {
                    SW_LOG_ERROR( "Physics layer '%#' is declared twice", layer._name.c_str() );
                    bValid = false;
                }
            }
            for ( const hashed_string& otherName : layer._listCollidesWith )
            {
                uint8 otherLayer = 0;
                if ( findLayerIndex( otherName, otherLayer ) == false )
                {
                    SW_LOG_ERROR( "Physics layer '%#' collides with unknown layer '%#'", layer._name.c_str(), otherName.c_str() );
                    bValid = false;
                }
            }
        }
        for ( size_t materialIndex = 0; materialIndex < _listMaterial.size(); ++materialIndex )
        {
            const PhysicsMaterialDef& material = _listMaterial[materialIndex];
            if ( material._name.empty() )
            {
                SW_LOG_ERROR( "Physics material %# has no name", materialIndex );
                bValid = false;
            }
            for ( size_t otherIndex = materialIndex + 1; otherIndex < _listMaterial.size(); ++otherIndex )
            {
                if ( _listMaterial[otherIndex]._name == material._name )
                {
                    SW_LOG_ERROR( "Physics material '%#' is declared twice", material._name.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    void PhysicsSettings::ensureDefaults()
    {
        if ( _listLayer.empty() )
        {
            PhysicsLayerDef layer;
            layer._name = PhysicsSettingsInternal::getDefaultName();
            layer._listCollidesWith.push_back( PhysicsSettingsInternal::getDefaultName() );
            _listLayer.push_back( layer );
        }
        if ( _listMaterial.empty() )
        {
            PhysicsMaterialDef material;
            material._name = PhysicsSettingsInternal::getDefaultName();
            _listMaterial.push_back( material );
        }
    }

    bool PhysicsSettings::findLayerIndex( const hashed_string& name, uint8& outIndex ) const
    {
        const size_t count = _listLayer.size() < kMaxLayerCount ? _listLayer.size() : kMaxLayerCount;
        for ( size_t layerIndex = 0; layerIndex < count; ++layerIndex )
        {
            if ( _listLayer[layerIndex]._name == name )
            {
                outIndex = static_cast<uint8>( layerIndex );
                return true;
            }
        }
        return false;
    }

    const PhysicsMaterialDef* PhysicsSettings::findMaterial( const hashed_string& name ) const
    {
        for ( const PhysicsMaterialDef& material : _listMaterial )
        {
            if ( material._name == name )
                return &material;
        }
        return nullptr;
    }

    CollisionLayers PhysicsSettings::makeCollisionLayers() const
    {
        CollisionLayers layers;
        for ( uint32 layerA = 0; layerA < CollisionLayers::kLayerCount; ++layerA )
        {
            for ( uint32 layerB = layerA; layerB < CollisionLayers::kLayerCount; ++layerB )
                layers.setLayerCollision( static_cast<uint8>( layerA ), static_cast<uint8>( layerB ), false );
        }
        const size_t count = _listLayer.size() < kMaxLayerCount ? _listLayer.size() : kMaxLayerCount;
        for ( size_t layerIndex = 0; layerIndex < count; ++layerIndex )
        {
            for ( const hashed_string& otherName : _listLayer[layerIndex]._listCollidesWith )
            {
                uint8 otherLayer = 0;
                if ( findLayerIndex( otherName, otherLayer ) )
                    layers.setLayerCollision( static_cast<uint8>( layerIndex ), otherLayer, true );
            }
        }
        return layers;
    }
} // namespace sw
