#include "pch.h"

#include "Engine/Utility/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Log/Logger.h"
    #include "Core/String/StringUtil.h"

namespace sw
{
    SW_LOG_CALLER( "DevCommandRegistry" );

    DevCommandRegistry& DevCommandRegistry::get()
    {
        static DevCommandRegistry s_registry;
        return s_registry;
    }

    const utf8* DevCommandRegistry::getImageMarker()
    {
        return "sw-dev-command-registry-image-marker";
    }

    bool DevCommandRegistry::registerCommand( const DevCommandRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || pRegistration->_pFunc == nullptr )
            return false;
        if ( findCommand( pRegistration->_pName ) != nullptr )
        {
            SW_LOG_WARNING( "Dev command '%#' is already registered - the second registration is ignored", pRegistration->_pName );
            return false;
        }
        _listRegistration.push_back( pRegistration );
        return true;
    }

    void DevCommandRegistry::unregisterCommand( const DevCommandRegistration* pRegistration )
    {
        for ( size_t index = 0; index < _listRegistration.size(); ++index )
        {
            if ( _listRegistration[index] != pRegistration )
                continue;
            _listRegistration.erase( _listRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    const DevCommandRegistration* DevCommandRegistry::findCommand( string_view name ) const
    {
        for ( const DevCommandRegistration* pRegistration : _listRegistration )
        {
            if ( StringUtil::equals( pRegistration->_pName, name, true ) )
                return pRegistration;
        }
        return nullptr;
    }

    void DevCommandRegistry::collectNames( string_view prefix, vector<string>& outListName ) const
    {
        outListName.clear();
        for ( const DevCommandRegistration* pRegistration : _listRegistration )
        {
            if ( StringUtil::startsWith( pRegistration->_pName, prefix, true ) )
                outListName.push_back( pRegistration->_pName );
        }
        std::sort( outListName.begin(), outListName.end() );
    }

    DevCommandRegistrar::DevCommandRegistrar( const DevCommandRegistration* pRegistration )
        : _pRegistration{ pRegistration }
        , _bRegistered{ DevCommandRegistry::get().registerCommand( pRegistration ) }
    {
    }

    DevCommandRegistrar::~DevCommandRegistrar()
    {
        if ( _bRegistered )
            DevCommandRegistry::get().unregisterCommand( _pRegistration );
    }
} // namespace sw

#endif
