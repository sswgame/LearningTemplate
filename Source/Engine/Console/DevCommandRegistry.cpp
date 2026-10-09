#include "pch.h"

#include "Engine/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Log/Logger.h"
    #include "Core/Container/StringUtil.h"

namespace sw
{
    SW_LOG_CALLER( "DevCommandRegistry" );

    DevCommandRegistry& DevCommandRegistry::get()
    {
        static DevCommandRegistry s_registry;
        return s_registry;
    }

    DevCommandRegistry::DevCommandRegistry()
        : _registration{ RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    const utf8* DevCommandRegistry::getImageMarker()
    {
        return "sw-dev-command-registry-image-marker";
    }

    bool DevCommandRegistry::registerCommand( const DevCommandRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || pRegistration->_pFunc == nullptr )
            return false;
        const RegistrationResult result = _registration.add( pRegistration, pRegistration->_pName );
        if ( result == RegistrationResult::DuplicateName || result == RegistrationResult::AlreadyPresent )
        {
            SW_LOG_WARNING( "Dev command '%#' is already registered - the second registration is ignored", pRegistration->_pName );
            return false;
        }
        return result == RegistrationResult::Added;
    }

    void DevCommandRegistry::unregisterCommand( const DevCommandRegistration* pRegistration )
    {
        (void)_registration.remove( pRegistration ); // 올라 있지 않은 명령을 빼는 것은 할 일이 없는 것이다
    }

    const DevCommandRegistration* DevCommandRegistry::findCommand( string_view name ) const
    {
        return _registration.findByName( name );
    }

    void DevCommandRegistry::collectNames( string_view prefix, vector<string>& outListName ) const
    {
        outListName.clear();
        for ( const DevCommandRegistration* pRegistration : _registration.getItems() )
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
