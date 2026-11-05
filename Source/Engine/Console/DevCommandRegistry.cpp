#include "pch.h"

#include "Engine/Console/DevCommandRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    bool DevCommandRegistry::readDiskPathArgument( string_view argument, string& outAbsolutePath, string& outReply )
    {
        const std::optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( argument );
        if ( absolutePath.has_value() == false )
        {
            outReply = "needs an absolute path (use {out}/...): '";
            outReply.append( argument.data(), argument.size() );
            outReply.push_back( '\'' );
            return false;
        }
        outAbsolutePath = absolutePath->str();
        return true;
    }

    bool DevCommandRegistry::readCountArgument( string_view argument, uint32& outValue )
    {
        if ( argument.empty() )
            return false;
        uint64 value = 0;
        for ( const utf8 ch : argument )
        {
            const bool bInvalidCountDigit = ch < '0' || '9' < ch || value > 1000000;
            if ( bInvalidCountDigit )
                return false;
            value = value * 10 + static_cast<uint64>( ch - '0' );
        }
        outValue = static_cast<uint32>( value );
        return true;
    }

    DevCommandRegistry::DevCommandRegistry()
        : _mutex{}
        , _registration{ "DevCommandRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    bool DevCommandRegistry::registerCommand( const DevCommandRegistration* pRegistration )
    {
        const bool bInvalidRegistration = pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || pRegistration->_pFunc == nullptr;
        if ( bInvalidRegistration )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _registration.add( pRegistration, pRegistration->_pName ); // 잠금은 잎이다 — 거절 로그는 놓은 뒤에 쓴다
        }
        reportRegistrationRejected( result, "Dev command", pRegistration->_pName );
        return result == RegistrationResult::Added;
    }

    void DevCommandRegistry::unregisterCommand( const DevCommandRegistration* pRegistration )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pRegistration ); // 등록되어 있지 않은 명령을 빼는 것은 할 일이 없다
    }

    const DevCommandRegistration* DevCommandRegistry::findCommand( string_view name ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.findByName( name );
    }

    uint32 DevCommandRegistry::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.getCount();
    }

    void DevCommandRegistry::collectNames( string_view prefix, vector<string>& outListName ) const
    {
        outListName.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const DevCommandRegistration* pRegistration : _registration.getItems() )
        {
            if ( StringUtil::startsWith( pRegistration->_pName, prefix, true ) )
                outListName.push_back( pRegistration->_pName );
        }
        VectorUtil::sort( outListName );
    }

    DevCommandRegistrar::DevCommandRegistrar( const DevCommandRegistration* pRegistration )
        : _pRegistration{ pRegistration }
        , _bRegistered{ SW_FALSE }
    {
    }

    void DevCommandRegistrar::attach()
    {
        _bRegistered = engine::getSubsystem<DevCommandRegistry>().registerCommand( _pRegistration ) ? SW_TRUE : SW_FALSE;
    }

    void DevCommandRegistrar::detach()
    {
        if ( _bRegistered == SW_TRUE )
            engine::getSubsystem<DevCommandRegistry>().unregisterCommand( _pRegistration );
        _bRegistered = SW_FALSE;
    }

    SW_ENGINE_SUBSYSTEM_DEFAULT( DevCommandRegistry, DevCommandRegistry );
} // namespace sw
