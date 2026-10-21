#include "pch.h"

#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleRenderPass" );

    ModuleRenderPassRegistry& ModuleRenderPassRegistry::get()
    {
        // Engine 모듈 안의 하나다. 게임 · 키트 모듈이 내려가도 사라지지 않는다(모듈 쪽 정적이 아니다).
        static ModuleRenderPassRegistry s_registry;
        return s_registry;
    }

    bool ModuleRenderPassRegistry::registerPass( const hashed_string& modulePass )
    {
        if ( modulePass.empty() )
        {
            SW_LOG_ERROR( "Module render pass needs a name" );
            return false;
        }
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _mapPass.find( modulePass ) != _mapPass.end() )
        {
            SW_LOG_ERROR( "Module render pass '%#' is already registered by another module", modulePass.c_str() );
            return false;
        }
        _mapPass.emplace( modulePass, vector<Constant>{} );
        _uniqueMissingLogged.erase( modulePass );
        return true;
    }

    void ModuleRenderPassRegistry::unregisterPass( const hashed_string& modulePass )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPass.erase( modulePass );
    }

    bool ModuleRenderPassRegistry::setConstant( const hashed_string& modulePass, const hashed_string& name, const float4& value )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPass.find( modulePass );
        if ( it == _mapPass.end() || name.empty() )
            return false;
        for ( Constant& constant : it->second )
        {
            if ( constant._name == name )
            {
                constant._value = value;
                return true;
            }
        }
        if ( it->second.size() >= kMaxConstantCount )
        {
            SW_LOG_ERROR( "Module render pass '%#' already has %# constants; '%#' is rejected", modulePass.c_str(), static_cast<uint32>( kMaxConstantCount ), name.c_str() );
            return false;
        }
        it->second.push_back( Constant{ name, value } );
        return true;
    }

    bool ModuleRenderPassRegistry::isRegistered( const hashed_string& modulePass ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _mapPass.find( modulePass ) != _mapPass.end();
    }

    bool ModuleRenderPassRegistry::copyConstants( const hashed_string& modulePass, vector<Constant>& outListConstant ) const
    {
        outListConstant.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPass.find( modulePass );
        if ( it == _mapPass.end() )
            return false;
        outListConstant = it->second;
        return true;
    }

    bool ModuleRenderPassRegistry::noteMissingOnce( const hashed_string& modulePass )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _uniqueMissingLogged.insert( modulePass ).second;
    }

    ModuleRenderPassRegistration::ModuleRenderPassRegistration( string_view modulePass )
        : _modulePass{ modulePass }
        , _bRegistered{ SW_FALSE }
    {
        _bRegistered = ModuleRenderPassRegistry::get().registerPass( _modulePass ) ? SW_TRUE : SW_FALSE;
    }

    ModuleRenderPassRegistration::~ModuleRenderPassRegistration()
    {
        if ( _bRegistered != SW_FALSE )
            ModuleRenderPassRegistry::get().unregisterPass( _modulePass );
    }

    bool ModuleRenderPassRegistration::setConstant( string_view name, const float4& value )
    {
        if ( _bRegistered == SW_FALSE )
            return false;
        return ModuleRenderPassRegistry::get().setConstant( _modulePass, hashed_string{ name }, value );
    }
} // namespace sw
