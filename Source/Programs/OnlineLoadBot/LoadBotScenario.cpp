#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotScenario.h"

#include "Core/Container/EnumArray.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include <iterator>

namespace sw
{
    namespace
    {
        struct LoadBotScenarioInternal
        {
            /** @brief 동작 표 한 줄 — 시나리오 `_action` 이름 · 보고의 동작 이름, 그 동작이 받는 칸(`_action` 밖, 셋까지), 첫 칸이 받는 값(비면 아무 값). */
            struct ActionRow
            {
                LoadBotAction      _action;
                string_view        _name;
                string_view        _arrKey[3];
                const string_view* _pAllowedText;
                uint32             _allowedTextCount;
            };

            /** @brief 동작마다 받는 값 — 로그인 방식 · 접속 상태(봇이 같은 이름을 쓴다). 빈 값은 첫 값(기본). */
            static constexpr string_view kArrLoginMode[]      = { "guest", "password" };
            static constexpr string_view kArrPresenceStatus[] = { "online", "away", "busy", "in_game", "offline" };

            /** @brief 동작 표 — 이름 찾기 · 칸 검사 · 값 검사가 이 표 하나를 쓴다. 열거형 순서와 같다. */
            static constexpr auto kArrAction = makeEnumTable<LoadBotAction, ActionRow>( {
                {             LoadBotAction::Login,                "login",                           { "_mode" },      kArrLoginMode,      SW_COUNT_OF( kArrLoginMode )},
                {            LoadBotAction::Logout,               "logout",                                    {},            nullptr,                                 0},
                {        LoadBotAction::Disconnect,           "disconnect",                                    {},            nullptr,                                 0},
                {         LoadBotAction::Reconnect,            "reconnect",                                    {},            nullptr,                                 0},
                {              LoadBotAction::Wait,                 "wait",                { "_minMs", "_maxMs" },            nullptr,                                 0},
                {            LoadBotAction::Repeat,               "repeat",             { "_count", "_listStep" },            nullptr,                                 0},
                {   LoadBotAction::DirectoryStatus,     "directory_status",                                    {},            nullptr,                                 0},
                {   LoadBotAction::DirectoryAssign,     "directory_assign",                           { "_kind" },            nullptr,                                 0},
                {          LoadBotAction::ChatJoin,            "chat_join",                        { "_channel" },            nullptr,                                 0},
                {          LoadBotAction::ChatSend,            "chat_send",               { "_channel", "_text" },            nullptr,                                 0},
                {       LoadBotAction::ChatHistory,         "chat_history",              { "_channel", "_count" },            nullptr,                                 0},
                {    LoadBotAction::SocialPresence,      "social_presence",            { "_status", "_activity" }, kArrPresenceStatus, SW_COUNT_OF( kArrPresenceStatus )},
                {LoadBotAction::SocialFriendRandom, "social_friend_random",                                    {},            nullptr,                                 0},
                {    LoadBotAction::LeaderboardTop,      "leaderboard_top",                { "_board", "_count" },            nullptr,                                 0},
                { LoadBotAction::LeaderboardAround,   "leaderboard_around",                { "_board", "_count" },            nullptr,                                 0},
                {  LoadBotAction::MatchmakingQueue,    "matchmaking_queue",                           { "_mode" },            nullptr,                                 0},
                {         LoadBotAction::WaitMatch,           "wait_match",                      { "_timeoutMs" },            nullptr,                                 0},
                {       LoadBotAction::PartyCreate,         "party_create",                                    {},            nullptr,                                 0},
                {      LoadBotAction::LiveOpsState,        "liveops_state",                                    {},            nullptr,                                 0},
                {       LoadBotAction::GameConnect,         "game_connect",                                    {},            nullptr,                                 0},
                {    LoadBotAction::GameDisconnect,      "game_disconnect",                                    {},            nullptr,                                 0},
                {              LoadBotAction::Pick,                 "pick",                       { "_listStep" },            nullptr,                                 0},
                {     LoadBotAction::EconomyWallet,       "economy_wallet",                                    {},            nullptr,                                 0},
                {   LoadBotAction::EconomyPurchase,     "economy_purchase",                          { "_offer" },            nullptr,                                 0},
                {     LoadBotAction::EconomyRedeem,       "economy_redeem",                        { "_product" },            nullptr,                                 0},
                {   LoadBotAction::MailboxClaimAll,    "mailbox_claim_all",                                    {},            nullptr,                                 0},
                {             LoadBotAction::Trade,                "trade", { "_asset", "_amount", "_timeoutMs" },            nullptr,                                 0},
                {       LoadBotAction::GuildCreate,         "guild_create",                                    {},            nullptr,                                 0},
                {      LoadBotAction::GuildDeposit,        "guild_deposit",               { "_asset", "_amount" },            nullptr,                                 0},
                {     LoadBotAction::GuildWithdraw,       "guild_withdraw",               { "_asset", "_amount" },            nullptr,                                 0},
                { LoadBotAction::LeaderboardSubmit,   "leaderboard_submit",               { "_board", "_amount" },            nullptr,                                 0},
            } ); // LoadBotAction 을 늘리고 줄을 잊으면 makeEnumTable 이 컴파일을 멈춘다

            /** @brief 표가 열거형 순서인지 봅니다 — `getRow` 가 열거자를 인덱스로 쓴다. */
            static constexpr bool isInEnumOrder()
            {
                for ( const LoadBotAction action : EnumRange<LoadBotAction>{} )
                {
                    if ( kArrAction[action]._action != action )
                        return false;
                }
                return true;
            }

            static const ActionRow& getRow( LoadBotAction action ) { return kArrAction[action]; }

            /** @brief 첫 칸(`_text`)으로 읽는 키 — 동작마다 많아야 하나다. */
            static constexpr string_view kArrTextKey[] = { "_channel", "_board", "_mode", "_kind", "_status", "_offer", "_product", "_asset" };

            /** @brief 동작이 받으면 반드시 적어야 하는 키 — 횟수 · 시간은 시나리오가 정한다(읽는 코드에 숨은 기본값이 없다). `_maxMs` 는 비면 `_minMs` 다(고정 대기). */
            static constexpr string_view kArrRequiredKey[] = { "_count", "_timeoutMs", "_minMs", "_amount" };

            static constexpr int32 kMaxRepeatDepth = 8;

            /** @brief 동작마다 받는 칸입니다(`kArrAction`) — 그 밖의 칸은 오류. `_weight` 는 `pick` 의 자식만 받는다(`readStep` 이 가른다). */
            static bool isKnownKey( LoadBotAction action, string_view key )
            {
                if ( key == "_action" || key == "_weight" )
                    return true;
                for ( const string_view known : getRow( action )._arrKey )
                {
                    if ( known.empty() == false && known == key )
                        return true;
                }
                return false;
            }

            static bool isKnownRootKey( string_view key )
            {
                return key == "_name" || key == "_seed" || key == "_region" || key == "_pacing" || key == "_executor" || key == "_arrivalRatePerSecond" ||
                       key == "_gracefulStopMs" || key == "_listStep";
            }

            /** @brief 첫 칸 값이 그 동작이 받는 값인지 봅니다. 허용 목록이 없거나 값이 비면(기본) 참입니다. */
            static bool isAllowedText( const ActionRow& row, string_view value )
            {
                if ( row._pAllowedText == nullptr || value.empty() )
                    return true;
                for ( uint32 index = 0; index < row._allowedTextCount; ++index )
                {
                    if ( row._pAllowedText[index] == value )
                        return true;
                }
                return false;
            }

            /** @brief 단계 하나를 읽습니다. @p bWeighted 면(`pick` 의 자식) `_weight` 가 필수이고, 아니면 `_weight` 는 오류다. */
            [[nodiscard]] static expected<void> readStep( const JSONValue& value, LoadBotStep& outStep, int32 depth, bool bWeighted );
            [[nodiscard]] static expected<void> readStepList( const JSONValue& listValue, vector<LoadBotStep>& outListStep, int32 depth, bool bWeighted );
            /** @brief 실행기 칸(`_executor` · `_arrivalRatePerSecond` · `_gracefulStopMs`)을 읽습니다 — 숨은 기본값 없이 시나리오가 정한다. */
            [[nodiscard]] static expected<void> readExecutor( const JSONValue& root, LoadBotScenario& inoutScenario );
        };

        static_assert( LoadBotScenarioInternal::isInEnumOrder(), "kArrAction rows follow LoadBotAction order" );
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( LoadBotAction action )
    {
        if ( action >= LoadBotAction::Count )
            return "unknown";
        return LoadBotScenarioInternal::getRow( action )._name.data();
    }

    expected<void> LoadBotScenarioInternal::readStepList( const JSONValue& listValue, vector<LoadBotStep>& outListStep, int32 depth, bool bWeighted )
    {
        if ( listValue.isArray() == false || listValue.size() == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_listStep must be a non-empty array" } );
        for ( size_t index = 0; index < listValue.size(); ++index )
        {
            const expected<void> step = readStep( listValue.at( index ), outListStep.emplace_back(), depth, bWeighted );
            if ( step.has_value() == false )
                return step;
        }
        return {};
    }

    expected<void> LoadBotScenarioInternal::readStep( const JSONValue& value, LoadBotStep& outStep, int32 depth, bool bWeighted )
    {
        if ( depth > kMaxRepeatDepth || value.isObject() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "step must be an object (repeat nesting up to 8)" } );
        const string actionName = value.get( "_action" ).asString();
        bool         bFound     = false;
        for ( const ActionRow& entry : kArrAction )
        {
            if ( entry._name == actionName )
            {
                outStep._action = entry._action;
                bFound          = true;
            }
        }
        if ( bFound == false )
            return unexpected( Error{ ErrorKind::NotFound, "unknown action", actionName } );
        for ( const string& key : value.getMemberNames() )
        {
            if ( isKnownKey( outStep._action, key ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the action has an unknown key", actionName }.appendDetail( " / " ).appendDetail( key ) );
        }
        for ( const string_view key : kArrRequiredKey )
        {
            const bool bMissingRequired = isKnownKey( outStep._action, key ) && value.has( key ) == false;
            if ( bMissingRequired )
                return unexpected( Error{ ErrorKind::NotFound, "the action needs a key", actionName }.appendDetail( " / " ).appendDetail( key ) );
        }

        // 비율은 `pick` 의 자식에만 있고 거기서는 필수다 — 비율을 코드 기본값으로 두지 않는다
        if ( value.has( "_weight" ) != bWeighted )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_weight is required on every child of pick and allowed nowhere else", actionName } );
        if ( bWeighted && ( value.readOptionalInto( "_weight", outStep._weight ) == false || outStep._weight <= 0 ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_weight must be a positive integer", actionName } );

        // 적지 않은 시간 · 횟수는 단계의 초기값이고, `_maxMs` 는 없으면 `_minMs` 와 같다(고정 대기). 정수가 아니면 거절한다.
        const bool bRepeat     = outStep._action == LoadBotAction::Repeat;
        const bool bPick       = outStep._action == LoadBotAction::Pick;
        bool       bTimingRead = value.readOptionalInto( "_minMs", outStep._minMs );
        outStep._maxMs         = outStep._minMs;
        bTimingRead            = bTimingRead && value.readOptionalInto( "_maxMs", outStep._maxMs ) && value.readOptionalInto( "_count", outStep._count ) &&
                      value.readOptionalInto( "_amount", outStep._amount );
        if ( outStep._action == LoadBotAction::WaitMatch || outStep._action == LoadBotAction::Trade )
        {
            bTimingRead    = bTimingRead && value.readOptionalInto( "_timeoutMs", outStep._minMs );
            outStep._maxMs = outStep._minMs;
        }
        if ( bTimingRead == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the action has a time or _count that is not an integer", actionName } );
        for ( const string_view key : kArrTextKey )
        {
            if ( value.has( key ) )
                outStep._text = value.get( key ).asString();
        }
        if ( value.has( "_text" ) )
            outStep._secondText = value.get( "_text" ).asString();
        else if ( value.has( "_activity" ) )
            outStep._secondText = value.get( "_activity" ).asString();
        const bool bInvalidStepTiming = outStep._minMs < 0 || outStep._maxMs < outStep._minMs || outStep._count <= 0 || ( isKnownKey( outStep._action, "_amount" ) && outStep._amount <= 0 );
        if ( bInvalidStepTiming )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the action has a negative time, _maxMs below _minMs, a non-positive _count or a non-positive _amount", actionName } );
        const bool bBadText = isAllowedText( getRow( outStep._action ), outStep._text ) == false;
        if ( bBadText )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the action has an unknown value", actionName }.appendDetail( " / " ).appendDetail( outStep._text ) );
        if ( bRepeat || bPick )
            return readStepList( value.get( "_listStep" ), outStep._listStep, depth + 1, bPick );
        return {};
    }

    expected<void> LoadBotScenarioInternal::readExecutor( const JSONValue& root, LoadBotScenario& inoutScenario )
    {
        const string executor = root.get( "_executor" ).asString();
        if ( executor == "per_vu_iterations" )
            inoutScenario._executor = LoadBotExecutor::PerUserIterations;
        else if ( executor == "constant_arrival_rate" )
            inoutScenario._executor = LoadBotExecutor::ConstantArrivalRate;
        else
            return unexpected( Error{ ErrorKind::InvalidArgument, "_executor must be \"per_vu_iterations\" or \"constant_arrival_rate\"", executor } );
        const bool bArrival   = inoutScenario._executor == LoadBotExecutor::ConstantArrivalRate;
        const bool bRateGiven = root.has( "_arrivalRatePerSecond" ) || root.has( "_gracefulStopMs" );
        if ( bArrival == false )
        {
            if ( bRateGiven )
                return unexpected( Error{ ErrorKind::InvalidArgument, "_arrivalRatePerSecond and _gracefulStopMs belong to constant_arrival_rate", executor } );
            return {};
        }
        const bool bRead = root.has( "_arrivalRatePerSecond" ) && root.has( "_gracefulStopMs" ) && root.readOptionalInto( "_arrivalRatePerSecond", inoutScenario._arrivalRatePerSecond ) &&
                           root.readOptionalInto( "_gracefulStopMs", inoutScenario._gracefulStopMs );
        if ( bRead == false || inoutScenario._arrivalRatePerSecond <= 0 || inoutScenario._gracefulStopMs < 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "constant_arrival_rate needs _arrivalRatePerSecond > 0 and _gracefulStopMs >= 0 (integers)", executor } );
        return {};
    }

    expected<LoadBotScenario> LoadBotScenario::loadFromFile( const AbsolutePath& path )
    {
        string text;
        if ( FileUtil::readTextFile( path, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "cannot read the scenario", path.str() } );
        expected<LoadBotScenario> scenario = loadFromXMLText( text );
        if ( scenario.has_value() )
            return scenario;
        Error error = scenario.error();
        error.prependDetail( ": " ).prependDetail( path.str() ); // 어느 파일인지 — 문장은 안쪽 리터럴 그대로다
        return unexpected( error );
    }

    expected<LoadBotScenario> LoadBotScenario::loadFromXMLText( string_view jsonText )
    {
        JSONDocument document;
        if ( document.parse( jsonText, "<scenario>" ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "scenario is not valid JSON", document.getLastError() } );
        const JSONValue root = document.getRoot();
        if ( root.isObject() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "scenario root must be an object" } );
        for ( const string& key : root.getMemberNames() )
        {
            if ( LoadBotScenarioInternal::isKnownRootKey( key ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "scenario has an unknown key", key } );
        }
        // 적지 않은 칸은 시나리오의 초기값이다. 정수가 아니면 거절한다.
        LoadBotScenario scenario;
        scenario._name = root.get( "_name" ).asString();
        if ( root.readOptionalInto( "_seed", scenario._seed ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_seed must be an integer" } );
        if ( root.has( "_region" ) )
            scenario._region = root.get( "_region" ).asString();
        // 보내기 방식은 숨은 기본값 없이 시나리오가 정한다 — 같은 수치가 닫힌 루프인지 열린 루프인지 보고서만 보고 알 수 없었다(감사 O-12)
        const string pacing = root.get( "_pacing" ).asString();
        if ( pacing == "closed" )
            scenario._pacing = LoadBotPacing::Closed;
        else if ( pacing == "open" )
            scenario._pacing = LoadBotPacing::Open;
        else
            return unexpected( Error{ ErrorKind::InvalidArgument, "_pacing must be \"closed\" or \"open\"", pacing } );
        if ( const expected<void> executor = LoadBotScenarioInternal::readExecutor( root, scenario ); executor.has_value() == false )
            return unexpected( executor.error() );
        const expected<void> steps = LoadBotScenarioInternal::readStepList( root.get( "_listStep" ), scenario._listStep, 0, false );
        if ( steps.has_value() == false )
            return unexpected( steps.error() );
        return scenario;
    }
} // namespace sw
