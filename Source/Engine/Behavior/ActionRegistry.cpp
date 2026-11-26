#include "pch.h"

#include "Engine/Behavior/ActionRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct ActionRegistryInternal
        {
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
    SW_DEFINE_LOCKED_REGISTRATION_LIST( ActionEntry::getEntries, const ActionEntry, "ActionRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase )
} // namespace sw

namespace sw
{
    GameActionCall::GameActionCall()
        : _pEntry{ nullptr }
        , _params{}
    {
    }

    const utf8* GameActionCall::getName() const
    {
        return _pEntry != nullptr ? _pEntry->_pName : "";
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
        const InstancedStruct                    defaults( *entry._pParamsOps );
        const string                             arguments = InstancedStructText::write( params.getInstance(), defaults.getInstance(), *params.getType() );
        StringBuilder<constant::buffer::kMax256> text;
        text.append( entry._pName );
        if ( arguments.empty() == false )
            text.appendFormat( "(%#)", arguments );
        return string( text.view() );
    }

    expected<void> GameActionCall::tryCompileText( string_view text, GameActionCall& outCall )
    {
        outCall                       = GameActionCall{};
        const uint32       generation = ActionRegistry::getRemovalGeneration(); // 찾기 전에 읽는다 — 사이에 빠지면 다음 부르기가 본다
        const ActionEntry* pEntry     = nullptr;
        InstancedStruct    params;
        if ( expected<void> read = tryReadProperties( text, pEntry, params ); read.has_value() == false )
            return read;
        outCall._pEntry = pEntry;
        outCall._params = RegisteredParams{ params.share(), pEntry, ActionEntry::getEntries(), generation };
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
        call._pEntry = &entry;
        call._params = RegisteredParams{ InstancedStruct( params ).share(), &entry, ActionEntry::getEntries(), ActionRegistry::getRemovalGeneration() }; // 편집 값과 나눠 들지 않게 복사한다
        return call;
    }

    ActionResult GameActionCall::invoke( ActionContext& context ) const
    {
        if ( _pEntry == nullptr )
            return ActionResult::Failed;
        // 읽은 뒤 줄이 빠졌으면(핫 리로드) 줄이 아직 있는지 본다 — 없으면 내린 이미지를 가리킨다. 이름도 그 이미지에 있어 적지 않는다.
        if ( _params.isEntryRegistered() == false )
        {
            SW_LOG_ERROR_ID( "GameAction.Stale", "An action call compiled before its module was unloaded was invoked - compile it again after a hot reload" );
            return ActionResult::Failed;
        }
        const ActionResult result = _pEntry->_function( context, _params.get() );
        if ( ActionTrace::isEnabled() )
            ActionTrace::record( *_pEntry, context, result );
        return result;
    }
} // namespace sw

namespace sw
{
    /** @brief 범용 Action 추적을 켭니다(개발 명령 `action.trace`). 켬 스위치는 전역 변수다 — 자동화 단계가 시작 값으로 되돌린다. */
    SW_GLOBAL_VARIABLE( bool, gv_actionTrace, false, "게임 동작(Action) 호출을 모두 기록한다 (개발 명령 action.trace 와 같다)" );

    namespace
    {
        struct ActionTraceInternal
        {
            static constexpr uint32 kSectionLines = 24; ///< 섹션 하나가 쓰는 줄의 상한

            struct TraceState
            {
                mutable mutex    _mutex{};
                ActionTraceEntry _arrEntry[ActionTrace::kCapacity] SW_GUARDED_BY( _mutex ){};
                uint64 _count    SW_GUARDED_BY( _mutex ){ 0 };
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
                    StringBuilder<constant::buffer::kMax128> value;
                    value.appendFormat( "%# %#", it->_arrName, getEnumName( it->_result ) );
                    if ( it->_bAborted == SW_TRUE )
                        value.append( " (aborted)" );
                    else if ( it->_bJustStarted == SW_TRUE )
                        value.append( " (started)" );
                    fixed_string<constant::buffer::kMax32> label;
                    formatstring( label.data(), label.capacity(), "#%#", it->_sequence );
                    writer.addLine( label, value.view() );
                }
            }
        };
    } // namespace

    /**
     * @class ActionTraceState
     * @brief 범용 Action 추적의 고리 버퍼 — 예전 함수 정적(감춘 싱글턴)을 엔진 범위 서브시스템이 든다. 켜짐은 전역 변수 `gv_actionTrace` 다.
     */
    class ActionTraceState final : public Subsystem
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
        void                resetGameInstanceState( GameInstanceBoundary boundary ) override
        {
            if ( boundary == GameInstanceBoundary::Starting )
                ActionTrace::clear(); // 기록은 게임이 끝난 뒤에 읽는다
        }
        const utf8* findGameInstanceState() const override
        {
            ScopedLock<mutex> lock{ _state._mutex };
            return _state._count == 0 ? nullptr : "action trace entries";
        }

        /** @brief 든 상태입니다. */
        ActionTraceInternal::TraceState& getState() { return _state; }

    private:
        ActionTraceInternal::TraceState _state;

        SW_SUBSYSTEM_SLOT( ActionTraceState, Required );
    };

    ActionTraceInternal::TraceState& ActionTraceInternal::getState()
    {
        return engine::getSubsystem<ActionTraceState>().getState();
    }

    void ActionTrace::setEnabled( bool bEnabled )
    {
        gv_actionTrace = bEnabled;
    }

    bool ActionTrace::isEnabled()
    {
        return gv_actionTrace;
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
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( ActionTraceState, HostOnly, GameInstance, Shared, (), SW_ENGINE_DEFAULT_FACTORY( ActionTraceState ) );
} // namespace sw
