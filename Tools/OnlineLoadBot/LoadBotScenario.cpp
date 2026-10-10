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
            struct ActionName
            {
                LoadBotAction _action;
                string_view   _name;
            };

            /** @brief 동작 이름 표 — 시나리오 `_action` 칸 · 보고의 동작 이름이 같은 표를 쓴다. */
            static constexpr ActionName kArrActionName[] = {
                {             LoadBotAction::Login,                "login"},
                {            LoadBotAction::Logout,               "logout"},
                {        LoadBotAction::Disconnect,           "disconnect"},
                {         LoadBotAction::Reconnect,            "reconnect"},
                {              LoadBotAction::Wait,                 "wait"},
                {            LoadBotAction::Repeat,               "repeat"},
                {   LoadBotAction::DirectoryStatus,     "directory_status"},
                {   LoadBotAction::DirectoryAssign,     "directory_assign"},
                {          LoadBotAction::ChatJoin,            "chat_join"},
                {          LoadBotAction::ChatSend,            "chat_send"},
                {       LoadBotAction::ChatHistory,         "chat_history"},
                {    LoadBotAction::SocialPresence,      "social_presence"},
                {LoadBotAction::SocialFriendRandom, "social_friend_random"},
                {    LoadBotAction::LeaderboardTop,      "leaderboard_top"},
                { LoadBotAction::LeaderboardAround,   "leaderboard_around"},
                {  LoadBotAction::MatchmakingQueue,    "matchmaking_queue"},
                {         LoadBotAction::WaitMatch,           "wait_match"},
                {       LoadBotAction::PartyCreate,         "party_create"},
                {      LoadBotAction::LiveOpsState,        "liveops_state"},
            };
            static_assert( std::size( kArrActionName ) == static_cast<size_t>( LoadBotAction::Count ), "every load bot action has a name" );

            /** @brief 첫 칸(`_text`)으로 읽는 키 — 동작마다 많아야 하나다. */
            static constexpr string_view kArrTextKey[] = { "_channel", "_board", "_mode", "_kind", "_status" };

            /** @brief 동작마다 받는 값 — 로그인 방식 · 접속 상태(봇이 같은 이름을 쓴다). 빈 값은 첫 값(기본). */
            static constexpr string_view kArrLoginMode[]      = { "guest", "password" };
            static constexpr string_view kArrPresenceStatus[] = { "online", "away", "busy", "in_game", "offline" };

            static constexpr int32 kMaxRepeatDepth        = 8;
            static constexpr int64 kDefaultMatchTimeoutMs = 60000;
            static constexpr int32 kDefaultRowCount       = 10;

            /** @brief 동작마다 받는 칸입니다 — 그 밖의 칸은 오류. */
            static bool isKnownKey( LoadBotAction action, string_view key )
            {
                if ( key == "_action" )
                    return true;
                switch ( action )
                {
                    case LoadBotAction::Wait:
                        return key == "_minMs" || key == "_maxMs";
                    case LoadBotAction::Repeat:
                        return key == "_count" || key == "_listStep";
                    case LoadBotAction::Login:
                    case LoadBotAction::MatchmakingQueue:
                        return key == "_mode";
                    case LoadBotAction::ChatJoin:
                        return key == "_channel";
                    case LoadBotAction::ChatHistory:
                        return key == "_channel" || key == "_count";
                    case LoadBotAction::ChatSend:
                        return key == "_channel" || key == "_text";
                    case LoadBotAction::SocialPresence:
                        return key == "_status" || key == "_activity";
                    case LoadBotAction::LeaderboardTop:
                    case LoadBotAction::LeaderboardAround:
                        return key == "_board" || key == "_count";
                    case LoadBotAction::WaitMatch:
                        return key == "_timeoutMs";
                    case LoadBotAction::DirectoryAssign:
                        return key == "_kind";
                    default:
                        return false;
                }
            }

            static bool isKnownRootKey( string_view key )
            {
                return key == "_name" || key == "_botCount" || key == "_rampUpSeconds" || key == "_durationSeconds" || key == "_seed" || key == "_region" ||
                       key == "_listStep";
            }

            template <size_t Count>
            static bool isOneOf( string_view value, const string_view ( &arrAllowed )[Count] )
            {
                for ( const string_view allowed : arrAllowed )
                {
                    if ( allowed == value )
                        return true;
                }
                return value.empty();
            }

            [[nodiscard]] static bool readStep( const JSONValue& value, LoadBotStep& outStep, string& outError, int32 depth );
            [[nodiscard]] static bool readStepList( const JSONValue& listValue, vector<LoadBotStep>& outListStep, string& outError, int32 depth );
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( LoadBotAction action )
    {
        for ( const LoadBotScenarioInternal::ActionName& entry : LoadBotScenarioInternal::kArrActionName )
        {
            if ( entry._action == action )
                return entry._name.data();
        }
        return "unknown";
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
        const string actionName = value.get( "_action", false ).asString();
        bool         bFound     = false;
        for ( const ActionName& entry : kArrActionName )
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

        const bool bRepeat = outStep._action == LoadBotAction::Repeat;
        outStep._minMs     = value.get( "_minMs", false ).asInt( 0 );
        outStep._maxMs     = value.get( "_maxMs", false ).asInt( outStep._minMs );
        outStep._count     = static_cast<int32>( value.get( "_count", false ).asInt( bRepeat ? 1 : kDefaultRowCount ) );
        for ( const string_view key : kArrTextKey )
        {
            if ( value.has( key, false ) )
                outStep._text = value.get( key, false ).asString();
        }
        if ( value.has( "_text", false ) )
            outStep._secondText = value.get( "_text", false ).asString();
        else if ( value.has( "_activity", false ) )
            outStep._secondText = value.get( "_activity", false ).asString();
        if ( outStep._action == LoadBotAction::WaitMatch )
        {
            outStep._minMs = value.get( "_timeoutMs", false ).asInt( kDefaultMatchTimeoutMs );
            outStep._maxMs = outStep._minMs;
        }
        if ( outStep._minMs < 0 || outStep._maxMs < outStep._minMs || outStep._count <= 0 )
        {
            outError = "action '" + actionName + "' has a negative time, _maxMs below _minMs or a non-positive _count";
            return false;
        }
        const bool bBadLoginMode = outStep._action == LoadBotAction::Login && isOneOf( outStep._text, kArrLoginMode ) == false;
        const bool bBadStatus    = outStep._action == LoadBotAction::SocialPresence && isOneOf( outStep._text, kArrPresenceStatus ) == false;
        if ( bBadLoginMode || bBadStatus )
        {
            outError = "action '" + actionName + "' has an unknown value '" + outStep._text + "'";
            return false;
        }
        if ( bRepeat )
            return readStepList( value.get( "_listStep", false ), outStep._listStep, outError, depth + 1 );
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
        const LoadBotScenario defaults;
        outScenario                  = LoadBotScenario{};
        outScenario._name            = root.get( "_name", false ).asString();
        outScenario._botCount        = static_cast<int32>( root.get( "_botCount", false ).asInt( defaults._botCount ) );
        outScenario._rampUpSeconds   = static_cast<int32>( root.get( "_rampUpSeconds", false ).asInt( defaults._rampUpSeconds ) );
        outScenario._durationSeconds = static_cast<int32>( root.get( "_durationSeconds", false ).asInt( defaults._durationSeconds ) );
        outScenario._seed            = static_cast<uint32>( root.get( "_seed", false ).asUint( defaults._seed ) );
        if ( root.has( "_region", false ) )
            outScenario._region = root.get( "_region", false ).asString();
        if ( outScenario._botCount <= 0 || outScenario._rampUpSeconds < 0 || outScenario._durationSeconds <= 0 )
        {
            outError = "_botCount and _durationSeconds must be positive and _rampUpSeconds not negative";
            return false;
        }
        return LoadBotScenarioInternal::readStepList( root.get( "_listStep", false ), outScenario._listStep, outError, 0 );
    }
} // namespace sw
