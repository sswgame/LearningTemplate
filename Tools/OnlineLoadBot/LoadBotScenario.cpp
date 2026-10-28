#include "pch.h"

#include "OnlineLoadBot/LoadBotScenario.h"

#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include <iterator>

namespace sw
{
    namespace
    {
        struct LoadBotScenarioInternal
        {
            /** @brief 동작 표 한 줄 — 시나리오 `_action` 이름 · 보고의 동작 이름, 그 동작이 받는 칸(`_action` 밖, 둘까지), 첫 칸이 받는 값(비면 아무 값). */
            struct ActionRow
            {
                LoadBotAction      _action;
                string_view        _name;
                string_view        _arrKey[2];
                const string_view* _pAllowedText;
                uint32             _allowedTextCount;
            };

            /** @brief 동작마다 받는 값 — 로그인 방식 · 접속 상태(봇이 같은 이름을 쓴다). 빈 값은 첫 값(기본). */
            static constexpr string_view kArrLoginMode[]      = { "guest", "password" };
            static constexpr string_view kArrPresenceStatus[] = { "online", "away", "busy", "in_game", "offline" };

            /** @brief 동작 표 — 이름 찾기 · 칸 검사 · 값 검사가 이 표 하나를 쓴다. 열거형 순서와 같다. */
            static constexpr ActionRow kArrAction[] = {
                {             LoadBotAction::Login,                "login",                { "_mode" },      kArrLoginMode,      SW_COUNT_OF( kArrLoginMode )},
                {            LoadBotAction::Logout,               "logout",                         {},            nullptr,                                 0},
                {        LoadBotAction::Disconnect,           "disconnect",                         {},            nullptr,                                 0},
                {         LoadBotAction::Reconnect,            "reconnect",                         {},            nullptr,                                 0},
                {              LoadBotAction::Wait,                 "wait",     { "_minMs", "_maxMs" },            nullptr,                                 0},
                {            LoadBotAction::Repeat,               "repeat",  { "_count", "_listStep" },            nullptr,                                 0},
                {   LoadBotAction::DirectoryStatus,     "directory_status",                         {},            nullptr,                                 0},
                {   LoadBotAction::DirectoryAssign,     "directory_assign",                { "_kind" },            nullptr,                                 0},
                {          LoadBotAction::ChatJoin,            "chat_join",             { "_channel" },            nullptr,                                 0},
                {          LoadBotAction::ChatSend,            "chat_send",    { "_channel", "_text" },            nullptr,                                 0},
                {       LoadBotAction::ChatHistory,         "chat_history",   { "_channel", "_count" },            nullptr,                                 0},
                {    LoadBotAction::SocialPresence,      "social_presence", { "_status", "_activity" }, kArrPresenceStatus, SW_COUNT_OF( kArrPresenceStatus )},
                {LoadBotAction::SocialFriendRandom, "social_friend_random",                         {},            nullptr,                                 0},
                {    LoadBotAction::LeaderboardTop,      "leaderboard_top",     { "_board", "_count" },            nullptr,                                 0},
                { LoadBotAction::LeaderboardAround,   "leaderboard_around",     { "_board", "_count" },            nullptr,                                 0},
                {  LoadBotAction::MatchmakingQueue,    "matchmaking_queue",                { "_mode" },            nullptr,                                 0},
                {         LoadBotAction::WaitMatch,           "wait_match",           { "_timeoutMs" },            nullptr,                                 0},
                {       LoadBotAction::PartyCreate,         "party_create",                         {},            nullptr,                                 0},
                {      LoadBotAction::LiveOpsState,        "liveops_state",                         {},            nullptr,                                 0},
                {       LoadBotAction::GameConnect,         "game_connect",                         {},            nullptr,                                 0},
                {    LoadBotAction::GameDisconnect,      "game_disconnect",                         {},            nullptr,                                 0},
            };
            static_assert( std::size( kArrAction ) == static_cast<size_t>( LoadBotAction::Count ), "every load bot action has a row" );

            /** @brief 표가 열거형 순서인지 봅니다 — `getRow` 가 열거자를 인덱스로 쓴다. */
            static constexpr bool isInEnumOrder()
            {
                for ( size_t index = 0; index < std::size( kArrAction ); ++index )
                {
                    if ( static_cast<size_t>( kArrAction[index]._action ) != index )
                        return false;
                }
                return true;
            }

            static const ActionRow& getRow( LoadBotAction action ) { return kArrAction[static_cast<size_t>( action )]; }

            /** @brief 첫 칸(`_text`)으로 읽는 키 — 동작마다 많아야 하나다. */
            static constexpr string_view kArrTextKey[] = { "_channel", "_board", "_mode", "_kind", "_status" };

            /** @brief 동작이 받으면 반드시 적어야 하는 키 — 횟수 · 시간은 시나리오가 정한다(읽는 코드에 숨은 기본값이 없다). `_maxMs` 는 비면 `_minMs` 다(고정 대기). */
            static constexpr string_view kArrRequiredKey[] = { "_count", "_timeoutMs", "_minMs" };

            static constexpr int32 kMaxRepeatDepth = 8;

            /** @brief 동작마다 받는 칸입니다(`kArrAction`) — 그 밖의 칸은 오류. */
            static bool isKnownKey( LoadBotAction action, string_view key )
            {
                if ( key == "_action" )
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
                return key == "_name" || key == "_botCount" || key == "_rampUpSeconds" || key == "_durationSeconds" || key == "_seed" || key == "_region" ||
                       key == "_listStep";
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

            [[nodiscard]] static bool readStep( const JSONValue& value, LoadBotStep& outStep, string& outError, int32 depth );
            [[nodiscard]] static bool readStepList( const JSONValue& listValue, vector<LoadBotStep>& outListStep, string& outError, int32 depth );
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

    bool LoadBotScenarioInternal::readStepList( const JSONValue& listValue, vector<LoadBotStep>& outListStep, string& outError, int32 depth )
    {
        if ( listValue.isArray() == false || listValue.size() == 0 )
        {
            outError = "_listStep must be a non-empty array";
            return false;
        }
        for ( size_t index = 0; index < listValue.size(); ++index )
        {
            if ( readStep( listValue.at( index ), outListStep.emplace_back(), outError, depth ) == false )
                return false;
        }
        return true;
    }

    bool LoadBotScenarioInternal::readStep( const JSONValue& value, LoadBotStep& outStep, string& outError, int32 depth )
    {
        if ( depth > kMaxRepeatDepth || value.isObject() == false )
        {
            outError = "step must be an object (repeat nesting up to 8)";
            return false;
        }
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
        {
            outError = "unknown action '" + actionName + "'";
            return false;
        }
        for ( const string& key : value.getMemberNames() )
        {
            if ( isKnownKey( outStep._action, key ) == false )
            {
                outError = "action '" + actionName + "' has unknown key '" + key + "'";
                return false;
            }
        }
        for ( const string_view key : kArrRequiredKey )
        {
            const bool bMissingRequired = isKnownKey( outStep._action, key ) && value.has( key ) == false;
            if ( bMissingRequired )
            {
                outError = "action '" + actionName + "' needs '" + string( key ) + "'";
                return false;
            }
        }

        // 적지 않은 시간 · 횟수는 단계의 초기값이고, `_maxMs` 는 없으면 `_minMs` 와 같다(고정 대기). 정수가 아니면 거절한다.
        const bool bRepeat     = outStep._action == LoadBotAction::Repeat;
        bool       bTimingRead = value.readOptionalInto( "_minMs", outStep._minMs );
        outStep._maxMs         = outStep._minMs;
        bTimingRead            = bTimingRead && value.readOptionalInto( "_maxMs", outStep._maxMs ) && value.readOptionalInto( "_count", outStep._count );
        if ( outStep._action == LoadBotAction::WaitMatch )
        {
            bTimingRead    = bTimingRead && value.readOptionalInto( "_timeoutMs", outStep._minMs );
            outStep._maxMs = outStep._minMs;
        }
        if ( bTimingRead == false )
        {
            outError = "action '" + actionName + "' has a time or _count that is not an integer";
            return false;
        }
        for ( const string_view key : kArrTextKey )
        {
            if ( value.has( key ) )
                outStep._text = value.get( key ).asString();
        }
        if ( value.has( "_text" ) )
            outStep._secondText = value.get( "_text" ).asString();
        else if ( value.has( "_activity" ) )
            outStep._secondText = value.get( "_activity" ).asString();
        const bool bInvalidStepTiming = outStep._minMs < 0 || outStep._maxMs < outStep._minMs || outStep._count <= 0;
        if ( bInvalidStepTiming )
        {
            outError = "action '" + actionName + "' has a negative time, _maxMs below _minMs or a non-positive _count";
            return false;
        }
        const bool bBadText = isAllowedText( getRow( outStep._action ), outStep._text ) == false;
        if ( bBadText )
        {
            outError = "action '" + actionName + "' has an unknown value '" + outStep._text + "'";
            return false;
        }
        if ( bRepeat )
            return readStepList( value.get( "_listStep" ), outStep._listStep, outError, depth + 1 );
        return true;
    }

    bool LoadBotScenario::loadFile( string_view path, LoadBotScenario& outScenario, string& outError )
    {
        string text;
        if ( FileUtil::readTextFile( path, text ) == false )
        {
            outError = "cannot read scenario '" + string( path ) + "'";
            return false;
        }
        if ( loadText( text, outScenario, outError ) )
            return true;
        outError = string( path ) + ": " + outError;
        return false;
    }

    bool LoadBotScenario::loadText( string_view jsonText, LoadBotScenario& outScenario, string& outError )
    {
        JSONDocument document;
        if ( document.parse( jsonText, "<scenario>" ) == false )
        {
            outError = "scenario is not valid JSON: " + document.getLastError();
            return false;
        }
        const JSONValue root = document.getRoot();
        if ( root.isObject() == false )
        {
            outError = "scenario root must be an object";
            return false;
        }
        for ( const string& key : root.getMemberNames() )
        {
            if ( LoadBotScenarioInternal::isKnownRootKey( key ) == false )
            {
                outError = "scenario has unknown key '" + key + "'";
                return false;
            }
        }
        // 적지 않은 칸은 시나리오의 초기값이다. 정수가 아니면 거절한다.
        outScenario       = LoadBotScenario{};
        outScenario._name = root.get( "_name" ).asString();
        const bool bRead  = root.readOptionalInto( "_botCount", outScenario._botCount ) && root.readOptionalInto( "_rampUpSeconds", outScenario._rampUpSeconds ) &&
                           root.readOptionalInto( "_durationSeconds", outScenario._durationSeconds ) && root.readOptionalInto( "_seed", outScenario._seed );
        if ( bRead == false )
        {
            outError = "_botCount, _rampUpSeconds, _durationSeconds and _seed must be integers";
            return false;
        }
        if ( root.has( "_region" ) )
            outScenario._region = root.get( "_region" ).asString();
        const bool bInvalidScenarioTiming = outScenario._botCount <= 0 || outScenario._rampUpSeconds < 0 || outScenario._durationSeconds <= 0;
        if ( bInvalidScenarioTiming )
        {
            outError = "_botCount and _durationSeconds must be positive and _rampUpSeconds not negative";
            return false;
        }
        return LoadBotScenarioInternal::readStepList( root.get( "_listStep" ), outScenario._listStep, outError, 0 );
    }
} // namespace sw
