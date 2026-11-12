#include "pch.h"

#include "Engine/Console/DevCommandRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    bool DevCommandRegistry::readDiskPathArgument( string_view argument, string& outAbsolutePath, string& outReply )
    {
        const optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( argument );
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
        constexpr uint64 kMaxCountArgument = 1000000;
        if ( argument.empty() )
            return false;
        uint64 value = 0;
        for ( const utf8 ch : argument )
        {
            if ( ch < '0' || '9' < ch )
                return false;
            value = value * 10 + static_cast<uint64>( ch - '0' );
            if ( value > kMaxCountArgument ) // 더한 뒤에 본다 - 앞에서 보면 상한의 열 배까지 지나간다
                return false;
        }
        outValue = static_cast<uint32>( value );
        return true;
    }

    DevCommandRegistry::DevCommandRegistry()
        : _mutex{}
        , _registration{ "DevCommandRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    bool DevCommandRegistry::run( const DevCommandRegistration& command, const vector<string>& listArgument, string& outReply )
    {
        return command._pSceneFunc != nullptr ? command._pSceneFunc( SceneWalkKey{}, listArgument, outReply ) : command._pFunc( listArgument, outReply );
    }

    bool DevCommandRegistry::registerEntry( const DevCommandRegistration* pRegistration )
    {
        const bool bInvalidRegistration =
            pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || ( pRegistration->_pFunc == nullptr ) == ( pRegistration->_pSceneFunc == nullptr );
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

    void DevCommandRegistry::unregisterEntry( const DevCommandRegistration* pRegistration )
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

    SW_ENGINE_SERVICE_SUBSYSTEM( DevCommandRegistry, GameVisible, Services, (), SW_ENGINE_DEFAULT_FACTORY( DevCommandRegistry ) );
} // namespace sw
