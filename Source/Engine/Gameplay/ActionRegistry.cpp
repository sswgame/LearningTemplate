#include "pch.h"

#include "Engine/Gameplay/ActionRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Reflection/Reflection.h"

namespace sw
{
    namespace
    {
        struct ActionRegistryInternal
        {
            struct RegistryState
            {
                mutex                               _mutex{};
                RegistrationList<const ActionEntry> _registration{ "ActionRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase };
            };

            static RegistryState& getRegistry()
            {
                static RegistryState s_state;
                return s_state;
            }

            /** @brief 글을 이름과 괄호 안으로 나눕니다. 괄호가 틀리면 false 입니다. */
            [[nodiscard]] static bool splitCall( string_view text, string_view& outName, string_view& outArguments )
            {
                const string_view trimmed = StringUtil::trim( text );
                const size_t      openPos = trimmed.find( '(' );
                outName                   = StringUtil::trim( trimmed.substr( 0, openPos ) );
                outArguments              = {};
                if ( openPos == string_view::npos )
                    return true;
                if ( trimmed.back() != ')' )
                    return false;
                outArguments = trimmed.substr( openPos + 1, trimmed.size() - openPos - 2 );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ActionRegistry::registerAction( const ActionEntry* pEntry )
    {
        ActionRegistryInternal::RegistryState& state = ActionRegistryInternal::getRegistry();
        ScopedLock<mutex>                      lock{ state._mutex };
        (void)state._registration.addLogged( pEntry, "Game action", pEntry->_pName ); // 거절은 공용 오류 로그가 알린다
    }

    void ActionRegistry::unregisterAction( const ActionEntry* pEntry )
    {
        ActionRegistryInternal::RegistryState& state = ActionRegistryInternal::getRegistry();
        ScopedLock<mutex>                      lock{ state._mutex };
        (void)state._registration.remove( pEntry ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    const ActionEntry* ActionRegistry::find( string_view name )
    {
        ActionRegistryInternal::RegistryState& state = ActionRegistryInternal::getRegistry();
        ScopedLock<mutex>                      lock{ state._mutex };
        return state._registration.findByName( name );
    }

    void ActionRegistry::collectNames( vector<string>& outListName )
    {
        ActionRegistryInternal::RegistryState& state = ActionRegistryInternal::getRegistry();
        ScopedLock<mutex>                      lock{ state._mutex };
        for ( uint32 index = 0; index < state._registration.getCount(); ++index )
        {
            outListName.push_back( state._registration.getNameAt( index ) );
        }
    }

    string ActionRegistry::joinNames()
    {
        vector<string> listName;
        collectNames( listName );
        string joined;
        for ( const string& name : listName )
        {
            joined += joined.empty() ? "" : ", ";
            joined += name;
        }
        return joined;
    }

    ActionRegistrar::ActionRegistrar( const ActionEntry* pEntry )
        : _pEntry{ pEntry }
    {
        ActionRegistry::registerAction( pEntry );
    }

    ActionRegistrar::~ActionRegistrar()
    {
        ActionRegistry::unregisterAction( _pEntry );
    }
} // namespace sw

namespace sw
{
    GameActionCall::GameActionCall()
        : _pEntry{ nullptr }
        , _pParams{}
    {
    }

    string_view GameActionCall::getCallName( string_view text )
    {
        string_view name;
        string_view arguments;
        (void)ActionRegistryInternal::splitCall( text, name, arguments ); // 이름은 괄호가 틀려도 나온다
        return name;
    }

    bool GameActionCall::tryReadFields( string_view text, const ActionEntry*& outEntry, InstancedStruct& outParams, string& outError )
    {
        outEntry  = nullptr;
        outParams = InstancedStruct{};
        string_view name;
        string_view arguments;
        if ( ActionRegistryInternal::splitCall( text, name, arguments ) == false )
        {
            outError = "a call has no closing parenthesis";
            return false;
        }
        const ActionEntry* pEntry = ActionRegistry::find( name );
        if ( pEntry == nullptr )
        {
            outError = "unknown action '" + string( name ) + "' (registered: " + ActionRegistry::joinNames() + ")";
            return false;
        }
        InstancedStruct     params( *pEntry->_pParamsOps );
        vector<string_view> listToken;
        InstancedStructText::splitArguments( arguments, listToken );
        if ( InstancedStructText::read( params.getInstance(), *params.getType(), listToken, outError ) == false )
            return false;
        outEntry  = pEntry;
        outParams = std::move( params );
        return true;
    }

    string GameActionCall::formatText( const ActionEntry& entry, const InstancedStruct& params )
    {
        const InstancedStruct defaults( *entry._pParamsOps );
        const string          arguments = InstancedStructText::write( params.getInstance(), defaults.getInstance(), *params.getType() );
        string                text( entry._pName );
        if ( arguments.empty() == false )
            text += "(" + arguments + ")";
        return text;
    }

    bool GameActionCall::tryCompileText( string_view text, GameActionCall& outCall, string& outError )
    {
        outCall                   = GameActionCall{};
        const ActionEntry* pEntry = nullptr;
        InstancedStruct    params;
        if ( tryReadFields( text, pEntry, params, outError ) == false )
            return false;
        outCall._pEntry  = pEntry;
        outCall._pParams = params.share();
        return true;
    }

    bool GameActionCall::compileText( string_view text, string_view sourceName, GameActionCall& outCall )
    {
        string error;
        if ( tryCompileText( text, outCall, error ) )
            return true;
        SW_LOG_ERROR_ID( "GameAction.Invalid", "%#: invalid action '%#': %#", sourceName, text, error.c_str() );
        return false;
    }

    GameActionCall GameActionCall::make( const ActionEntry& entry, const InstancedStruct& params )
    {
        GameActionCall call;
        call._pEntry  = &entry;
        call._pParams = InstancedStruct( params ).share(); // 편집 값과 나눠 들지 않게 복사한다
        return call;
    }

    ActionResult GameActionCall::invoke( ActionContext& context ) const
    {
        return _pEntry != nullptr ? _pEntry->_function( context, _pParams.get() ) : ActionResult::Failed;
    }
} // namespace sw
