#include "pch.h"

#include "Engine/Gameplay/ActionRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/EnumName.h"
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
        if ( _pEntry == nullptr )
            return ActionResult::Failed;
        const ActionResult result = _pEntry->_function( context, _pParams.get() );
        if ( ActionTrace::isEnabled() )
            ActionTrace::record( *_pEntry, context, result );
        return result;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct ActionTraceInternal
        {
            static constexpr uint32 kSectionLines = 24; ///< 섹션 하나가 쓰는 줄의 상한

            struct TraceState
            {
                mutex            _mutex{};
                ActionTraceEntry _arrEntry[ActionTrace::kCapacity] SW_GUARDED_BY( _mutex ){};
                uint64 _count    SW_GUARDED_BY( _mutex ){ 0 };
                atomic<bool>     _bEnabled{ false };
            };

            static TraceState& getState()
            {
                static TraceState s_state;
                return s_state;
            }

            /** @brief `action.trace on|off|clear` — 범용 Action 추적(HUD 섹션 `action`). */
            static bool runTrace( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                if ( listArgument[0] == "clear" )
                {
                    ActionTrace::clear();
                    outReply = "action.trace cleared";
                    return true;
                }
                bool bEnabled = false;
                if ( StringUtil::tryParseBool( listArgument[0], bEnabled ) == false )
                    return false;
                ActionTrace::setEnabled( bEnabled );
                outReply = bEnabled ? "action.trace on" : "action.trace off";
                return true;
            }

            /** @brief HUD 섹션 `action` — 최근 동작 호출(늦은 것부터): 이름 · 결과 · 시작/중단. */
            static void writeSection( DebugHUDSectionWriter& writer )
            {
                if ( ActionTrace::isEnabled() == false )
                {
                    writer.addLine( "trace", "off (action.trace on)" );
                    return;
                }
                vector<ActionTraceEntry> listEntry;
                ActionTrace::collect( listEntry );
                if ( listEntry.empty() )
                    writer.addLine( "trace", "on, no call yet" );
                uint32 lineCount = 0;
                for ( auto it = listEntry.rbegin(); it != listEntry.rend() && lineCount < kSectionLines; ++it, ++lineCount )
                {
                    string value = string( it->_arrName ) + " " + getEnumName( it->_result );
                    if ( it->_bAborted == SW_TRUE )
                        value += " (aborted)";
                    else if ( it->_bJustStarted == SW_TRUE )
                        value += " (started)";
                    writer.addLine( "#" + to_string( it->_sequence ), value );
                }
            }
        };
    } // namespace

    void ActionTrace::setEnabled( bool bEnabled )
    {
        ActionTraceInternal::getState()._bEnabled.store( bEnabled, std::memory_order_relaxed );
    }

    bool ActionTrace::isEnabled()
    {
        return ActionTraceInternal::getState()._bEnabled.load( std::memory_order_relaxed );
    }

    void ActionTrace::record( const ActionEntry& entry, const ActionContext& context, ActionResult result )
    {
        ActionTraceInternal::TraceState& state = ActionTraceInternal::getState();
        ScopedLock<mutex>                lock{ state._mutex };
        ActionTraceEntry&                slot = state._arrEntry[state._count % kCapacity];
        slot                                  = ActionTraceEntry{};
        const string_view name                = entry._pName != nullptr ? string_view( entry._pName ) : string_view{};
        const size_t      length              = name.size() < ActionTraceEntry::kNameCapacity - 1 ? name.size() : ActionTraceEntry::kNameCapacity - 1;
        for ( size_t index = 0; index < length; ++index )
        {
            slot._arrName[index] = name[index];
        }
        slot._sequence     = ++state._count;
        slot._result       = result;
        slot._bJustStarted = context._bJustStarted;
        slot._bAborted     = context._bAborted;
    }

    void ActionTrace::collect( vector<ActionTraceEntry>& outListEntry )
    {
        outListEntry.clear();
        ActionTraceInternal::TraceState& state = ActionTraceInternal::getState();
        ScopedLock<mutex>                lock{ state._mutex };
        const uint64                     count = state._count < kCapacity ? state._count : kCapacity;
        for ( uint64 index = state._count - count; index < state._count; ++index )
        {
            outListEntry.push_back( state._arrEntry[index % kCapacity] );
        }
    }

    void ActionTrace::clear()
    {
        ActionTraceInternal::TraceState& state = ActionTraceInternal::getState();
        ScopedLock<mutex>                lock{ state._mutex };
        state._count = 0;
    }

    SW_DEV_COMMAND( ActionTraceCommand, "action.trace", "action.trace on|off|clear",
                    "Record every game action call (dialogue, schedule, reward, behavior tree) for the action debugger (hud.print action)", &ActionTraceInternal::runTrace );
    SW_DEBUG_HUD_SECTION( action, "action", "Action calls", 260, DebugHUDSectionFlag::kNone, &ActionTraceInternal::writeSection );
} // namespace sw
