#include "pch.h"

#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleRenderPass" );

    bool ModuleRenderPassRegistry::registerPass( const hashed_string& modulePass )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _registryPass.addLogged( modulePass, vector<Constant>{}, "Module render pass" ) == false )
            return false;
        _uniqueMissingLogged.erase( modulePass );
        return true;
    }

    void ModuleRenderPassRegistry::unregisterPass( const hashed_string& modulePass )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registryPass.remove( modulePass ); // 없는 이름이면 할 일이 없다(등록하지 못한 객체가 풀 때도 있다)
    }

    bool ModuleRenderPassRegistry::setConstant( const hashed_string& modulePass, const hashed_string& name, const float4& value )
    {
        ScopedLock<mutex> lock{ _mutex };
        vector<Constant>* pListConstant = _registryPass.find( modulePass );
        if ( pListConstant == nullptr || name.empty() )
            return false;
        for ( Constant& constant : *pListConstant )
        {
            if ( constant._name == name )
            {
                constant._value = value;
                return true;
            }
        }
        if ( pListConstant->size() >= kMaxConstantCount )
        {
            SW_LOG_ERROR( "Module render pass '%#' already has %# constants; '%#' is rejected", modulePass.c_str(), static_cast<uint32>( kMaxConstantCount ), name.c_str() );
            return false;
        }
        pListConstant->push_back( Constant{ name, value } );
        return true;
    }

    bool ModuleRenderPassRegistry::isRegistered( const hashed_string& modulePass ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registryPass.find( modulePass ) != nullptr;
    }

    bool ModuleRenderPassRegistry::copyConstants( const hashed_string& modulePass, vector<Constant>& outListConstant ) const
    {
        outListConstant.clear();
        ScopedLock<mutex>       lock{ _mutex };
        const vector<Constant>* pListConstant = _registryPass.find( modulePass );
        if ( pListConstant == nullptr )
            return false;
        outListConstant = *pListConstant;
        return true;
    }

    bool ModuleRenderPassRegistry::noteMissingOnce( const hashed_string& modulePass )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _uniqueMissingLogged.insert( modulePass ).second;
    }

    ModuleRenderPassRegistration::ModuleRenderPassRegistration( ModuleRenderPassRegistry& registry, string_view modulePass )
        : _registry{ registry }
        , _modulePass{ modulePass }
        , _bRegistered{ SW_FALSE }
    {
        _bRegistered = _registry.registerPass( _modulePass ) ? SW_TRUE : SW_FALSE;
    }

    ModuleRenderPassRegistration::~ModuleRenderPassRegistration()
    {
        if ( _bRegistered != SW_FALSE )
            _registry.unregisterPass( _modulePass );
    }

    bool ModuleRenderPassRegistration::setConstant( string_view name, const float4& value )
    {
        if ( _bRegistered == SW_FALSE )
            return false;
        return _registry.setConstant( _modulePass, hashed_string{ name }, value );
    }
} // namespace sw
