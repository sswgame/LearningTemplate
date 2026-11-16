#include "pch.h"

#include "Engine/Behavior/ActionRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/Reflection.h"

namespace sw
{
    namespace
    {
        struct ActionRegistryInternal
        {
            /** @brief 등록 목록입니다(함수 정적 — 다른 TU · DLL 의 정적 등록자가 먼저 쓴다). 워커가 읽으므로 고치고 읽을 때 목록의 주인 잠금(`getOwnerMutex`)을 잡는다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const ActionEntry, "ActionRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase )

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
        RegistrationList<const ActionEntry>& registrations = ActionRegistryInternal::getRegistrations();
        ScopedLock<mutex>                    lock{ registrations.getOwnerMutex() };
        (void)registrations.addLogged( pEntry, "Game action", pEntry->_pName ); // 거절은 공용 오류 로그가 알린다
    }

    void ActionRegistry::unregisterAction( const ActionEntry* pEntry )
    {
        RegistrationList<const ActionEntry>& registrations = ActionRegistryInternal::getRegistrations();
        ScopedLock<mutex>                    lock{ registrations.getOwnerMutex() };
        (void)registrations.remove( pEntry ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    const ActionEntry* ActionRegistry::find( string_view name )
    {
        RegistrationList<const ActionEntry>& registrations = ActionRegistryInternal::getRegistrations();
        ScopedLock<mutex>                    lock{ registrations.getOwnerMutex() };
        return registrations.findByName( name );
    }

    void ActionRegistry::collectNames( vector<string>& outListName )
    {
        RegistrationList<const ActionEntry>& registrations = ActionRegistryInternal::getRegistrations();
        ScopedLock<mutex>                    lock{ registrations.getOwnerMutex() };
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            outListName.push_back( registrations.getNameAt( index ) );
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

    expected<void> GameActionCall::tryReadProperties( string_view text, const ActionEntry*& outEntry, InstancedStruct& outParams )
    {
        outEntry  = nullptr;
        outParams = InstancedStruct{};
        string_view name;
        string_view arguments;
        if ( ActionRegistryInternal::splitCall( text, name, arguments ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a call has no closing parenthesis", text } );
        const ActionEntry* pEntry = ActionRegistry::find( name );
        if ( pEntry == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "unknown action", name }.appendDetail( " (registered: " ).appendDetail( ActionRegistry::joinNames() ).appendDetail( ")" ) );
        InstancedStruct     params( *pEntry->_pParamsOps );
        vector<string_view> listToken;
        InstancedStructText::splitArguments( arguments, listToken );
        if ( expected<void> read = InstancedStructText::read( params.getInstance(), *params.getType(), listToken ); read.has_value() == false )
            return read;
        outEntry  = pEntry;
        outParams = std::move( params );
        return {};
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

    expected<void> GameActionCall::tryCompileText( string_view text, GameActionCall& outCall )
    {
        outCall                   = GameActionCall{};
        const ActionEntry* pEntry = nullptr;
        InstancedStruct    params;
        if ( expected<void> read = tryReadProperties( text, pEntry, params ); read.has_value() == false )
            return read;
        outCall._pEntry  = pEntry;
        outCall._pParams = params.share();
        return {};
    }

    bool GameActionCall::compileText( string_view text, string_view sourceName, GameActionCall& outCall )
    {
        const expected<void> compiled = tryCompileText( text, outCall );
        if ( compiled.has_value() )
            return true;
        SW_LOG_ERROR_ID( "GameAction.Invalid", "%#: invalid action '%#': %# (%#)", sourceName, text, compiled.error().message(), compiled.error().detail() );
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

            /** @brief 상태입니다 — 엔진 범위의 `ActionTraceState` 가 든다. */
            static TraceState& getState();

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

    /**
     * @class ActionTraceState
     * @brief 범용 Action 추적의 고리 버퍼 · 켜짐 — 예전 함수 정적(감춘 싱글턴)을 엔진 범위 서브시스템이 든다. 엔진을 다시 세우면 새로 선다.
     */
    class ActionTraceState final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ActionTraceState";

        ActionTraceState()
            : _state{}
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 든 상태입니다. */
        ActionTraceInternal::TraceState& getState() { return _state; }

    private:
        ActionTraceInternal::TraceState _state;

        SW_SERVICE_SLOT( ActionTraceState, Required );
    };

    ActionTraceInternal::TraceState& ActionTraceInternal::getState()
    {
        return engine::getSubsystem<ActionTraceState>().getState();
    }

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

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( ActionTraceState, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ActionTraceState ) );
} // namespace sw
