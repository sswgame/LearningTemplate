#include "pch.h"

#include "Engine/Scene/SceneObjectHash.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/HexUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/DebugReplyLine.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace sw
{
    namespace
    {
        struct SceneStateHashInternal
        {
            /** @brief 오브젝트 하나의 해시입니다 — 이름 · 활성, 그리고 컴포넌트마다(소유 순서) `Component::foldStateHash` 가 접은 상태. */
            static uint64 hashObject( const GameObject& object, string_view name )
            {
                StateHashBuilder builder;
                builder.addString( name );
                builder.addUint8( object.isActive() ? 1 : 0 );
                object.forEachComponent( [&builder]( const Component* pComponent )
                { pComponent->foldStateHash( builder ); } );
                return builder.getHash();
            }

            struct Entry
            {
                string _name;
                uint8  _bActive{ 0 };
                float3 _position{};
            };

            /** @brief 줄 구분 · 로그 꼬리 구분에 쓰는 글자와 제어 문자, '%' 자신은 `%XX` 로 씁니다. */
            static bool needsEscape( utf8 ch )
            {
                return ch == '%' || ch == ',' || ch == ':' || static_cast<uint8>( ch ) <= 0x20 || static_cast<uint8>( ch ) == 0x7F;
            }

            static int32 hexValue( utf8 ch )
            {
                uint32 value = 0;
                return HexDigitUtil::tryParseDigit( ch, value ) ? static_cast<int32>( value ) : -1;
            }

            /** @brief `%XX` 를 푼 이름입니다. 잘못된 `%` 이면 false 입니다. */
            static bool unescapeName( string_view escaped, string& outName )
            {
                outName.clear();
                for ( size_t index = 0; index < escaped.size(); ++index )
                {
                    if ( escaped[index] != '%' )
                    {
                        outName += escaped[index];
                        continue;
                    }
                    const int32 high = index + 2 < escaped.size() ? hexValue( escaped[index + 1] ) : -1;
                    const int32 low  = index + 2 < escaped.size() ? hexValue( escaped[index + 2] ) : -1;
                    if ( high < 0 || low < 0 )
                        return false;
                    outName += static_cast<utf8>( high * 16 + low );
                    index += 2;
                }
                return true;
            }
        };

        struct SceneStateHashCommandsInternal
        {

            /** @brief 이력을 남기고 있는 상태 해시 이력입니다. 서비스가 없거나 꺼져 있으면 nullptr 이고 @p outReply 에 까닭을 씁니다. */
            /** @brief 기록 중인 이력 서비스입니다. 없거나 꺼져 있으면 그 까닭(명령 답이 문장 그대로다)입니다. */
            [[nodiscard]] static expected<const StateHashHistory*> findRecordingHistory()
            {
                const StateHashHistory* pHistory = engine::findSubsystem<StateHashHistory>();
                if ( pHistory == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "state hash history service is not running in this host" } );
                if ( pHistory->isRecording() == false )
                    return unexpected( Error{ ErrorKind::Unsupported, "state hash history is off - start with -gv_stateHashHistory=<ticks>" } );
                return pHistory;
            }

            static bool runStateHash( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                vector<StateHashSample> listSample;
                listSample.push_back( StateHashSample{ 0, SceneObjectHash::compute( *pManager ) } );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.objects` — 이름 있는 오브젝트마다 해시(`<16자리 16진 해시> <이름>`)를 답합니다. 어긋난 틱 근처에서 두 프로세스의 답을 견주면 어느 오브젝트가 다른지 나옵니다(`SceneObjectHash::findDifferingObjects`). */
            static bool runStateObjects( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<SceneObjectHash> listObject;
                const expected<uint64>  tick = collectTickedObjects( listObject );
                if ( tick.has_value() == false )
                {
                    outReply = string( tick.error().message() );
                    return false;
                }
                SceneObjectHash::writeTickedObjectReport( *tick, listObject, outReply );
                return true;
            }

            /** @brief `state.objectline` — `state.objects` 와 같은 값(틱 + 오브젝트별 해시)을 한 줄(`SceneObjectHash::writeObjectReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭의 클라이언트 칸이 쓴다. */
            static bool runStateObjectLine( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<SceneObjectHash> listObject;
                const expected<uint64>  tick = collectTickedObjects( listObject );
                if ( tick.has_value() == false )
                {
                    outReply = string( tick.error().message() );
                    return false;
                }
                if ( SceneObjectHash::writeObjectReportLine( *tick, listObject, outReply ) == false )
                {
                    outReply = "the object report does not fit one line (" + to_string( listObject.size() ) + " objects, limit " + to_string( DebugReplyLine::kMaxBytes ) +
                               " bytes) - use state.objects";
                    return false;
                }
                return true;
            }

            /**
             * @brief 두 `state.objects*` 명령이 같이 쓰는 값입니다. 돌려주는 틱은 이력의 가장 최근 틱이다 — 이력이 없으면 어느 틱의 해시인지 말할 수 없어
             *        답하지 않는다(그 까닭 — 문장이 명령 답이다).
             */
            [[nodiscard]] static expected<uint64> collectTickedObjects( vector<SceneObjectHash>& outListObject )
            {
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                    return unexpected( history.error() );
                StateHashSample latest;
                if ( ( *history )->tryGetLatest( latest ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "state hash history has no tick yet" } );
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "no active scene" } );
                SceneObjectHash::collectObjects( *pManager, outListObject );
                return latest._tick;
            }

            /** @brief 이력의 줄 이름 인자입니다(생략하면 씬 줄). 그 줄이 있는지는 부르는 쪽이 `StateHashHistory::hasChannel` 로 본다. */
            [[nodiscard]] static string_view readChannelName( const vector<string>& listArgument, size_t nameIndex )
            {
                return listArgument.size() > nameIndex ? string_view{ listArgument[nameIndex] } : string_view{ StateHashHistory::kSceneChannelName };
            }

            /** @brief 그 이름의 줄이 없을 때의 답입니다. */
            [[nodiscard]] static string makeNoChannelReply( string_view channelName )
            {
                return "no state hash channel '" + string( channelName ) + "' - state.hashnames lists them";
            }

            static bool runStateHashes( const vector<string>& listArgument, string& outReply )
            {
                uint64     fromTick  = 0;
                uint64     count     = 0;
                const bool bArgCount = listArgument.size() == 2 || listArgument.size() == 3;
                const bool bHasFrom  = bArgCount && StringUtil::parseUint64( listArgument[0], fromTick );
                const bool bHasCount = bHasFrom && StringUtil::parseUint64( listArgument[1], count );
                if ( bHasCount == false )
                    return false;
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory    = *history;
                const string_view       channelName = readChannelName( listArgument, 2 );
                if ( pHistory->hasChannel( channelName ) == false )
                {
                    outReply = makeNoChannelReply( channelName );
                    return false;
                }
                vector<StateHashSample> listSample;
                pHistory->collectChannel( channelName, fromTick, static_cast<uint32>( count ), listSample );
                outReply.clear();
                StateHashHistory::writeReport( listSample, outReply );
                return true;
            }

            /** @brief `state.hashnames` — 해시 이력의 줄 이름(씬 줄 `scene` 과 키트가 더한 줄)을 한 줄에 하나씩 답합니다. */
            static bool runStateHashNames( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory = *history;
                vector<string>          listName;
                pHistory->collectChannelNames( listName );
                outReply = StateHashHistory::kSceneChannelName;
                outReply += '\n';
                for ( const string& name : listName )
                {
                    outReply += name;
                    outReply += '\n';
                }
                return true;
            }

            /** @brief `state.hashline <count> [name]` — 가장 최근 @p count 틱의 이력(@p name 은 키트의 줄, 생략하면 씬 줄)을 한 줄(`StateHashHistory::writeReportLine`)로 답합니다. 표준 입력으로 묻는 에디터 Sync 탭이 쓴다. */
            static bool runStateHashLine( const vector<string>& listArgument, string& outReply )
            {
                uint64     count     = 0;
                const bool bHasCount = ( listArgument.size() == 1 || listArgument.size() == 2 ) && StringUtil::parseUint64( listArgument[0], count );
                if ( bHasCount == false || count == 0 )
                    return false;
                if ( count > StateHashHistory::kMaxReportLineSamples )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "count %# does not fit one line (at most %#) - use state.hashes", count, StateHashHistory::kMaxReportLineSamples );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                const expected<const StateHashHistory*> history = findRecordingHistory();
                if ( history.has_value() == false )
                {
                    outReply = string( history.error().message() );
                    return false;
                }
                const StateHashHistory* pHistory    = *history;
                const string_view       channelName = readChannelName( listArgument, 1 );
                if ( pHistory->hasChannel( channelName ) == false )
                {
                    outReply = makeNoChannelReply( channelName );
                    return false;
                }
                vector<StateHashSample> listSample;
                StateHashSample         latest;
                if ( pHistory->tryGetLatestChannel( channelName, latest ) )
                {
                    const uint64 fromTick = latest._tick + 1 > count ? latest._tick + 1 - count : 0;
                    pHistory->collectChannel( channelName, fromTick, static_cast<uint32>( count ), listSample );
                }
                if ( StateHashHistory::writeReportLine( listSample, outReply ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "the hash history does not fit one line (limit %# bytes) - use state.hashes", DebugReplyLine::kMaxBytes );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // 샘플 하나는 가장 길 때 `<20자리 틱>:<16자리 해시>,` = 38 바이트다.
    static_assert( 38u * StateHashHistory::kMaxReportLineSamples + 32u <= DebugReplyLine::kMaxBytes, "kMaxReportLineSamples 가 한 줄 상한 안에 들어야 한다" );

    uint64 SceneObjectHash::compute( const GameObjectManager& manager )
    {
        // 오브젝트 해시를 (이름, 해시) 정수 순으로 접는다 — 실수 비교로 정렬하면 NaN 에서 순서가 깨진다(std::sort 미정의 동작).
        vector<SceneObjectHash> listObject;
        collectObjects( manager, listObject );
        uint64 hash = HashUtil::kFNVOffset64;
        for ( const SceneObjectHash& object : listObject )
        {
            hash = HashUtil::appendBytes64( hash, object._name.data(), object._name.size() );
            hash = HashUtil::appendValue64( hash, object._hash );
        }
        return hash;
    }

    void SceneObjectHash::collectObjects( const GameObjectManager& manager, vector<SceneObjectHash>& outListObject )
    {
        // 비용은 이름 있는 오브젝트 × 컴포넌트 수의 가상 호출이다 — 이력이 켜진 틱에만 돈다. 측정은 이 구간과 오브젝트 수 카운터로 본다.
        SW_PROFILE_SCOPE( "GT.StateHash.collect" );
        outListObject.clear();
        manager.forEachGameObject( [&outListObject]( GameObject* pObject )
        {
            if ( pObject->getName().empty() || pObject->isTransient() ) // 세운 쪽이 상태에서 다시 세우는 파생 오브젝트는 상태가 아니다
                return;
            SceneObjectHash& object = outListObject.emplace_back();
            object._name            = pObject->getName().c_str();
            object._hash            = SceneStateHashInternal::hashObject( *pObject, object._name );
        } );
        SW_PROFILE_COUNT( "GT.StateHash.objects", static_cast<uint32>( outListObject.size() ) );
        VectorUtil::sort( outListObject, []( const SceneObjectHash& left, const SceneObjectHash& right )
        {
            return left._name != right._name ? left._name < right._name : left._hash < right._hash;
        } );
    }

    void SceneObjectHash::writeObjectReport( const vector<SceneObjectHash>& listObject, string& outText )
    {
        for ( const SceneObjectHash& object : listObject )
        {
            HexUtil::appendUint64( outText, object._hash );
            outText += ' ';
            outText += object._name;
            outText += '\n';
        }
    }

    bool SceneObjectHash::parseObjectReport( string_view text, vector<SceneObjectHash>& outListObject )
    {
        outListObject.clear();
        size_t lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            string_view line = text.substr( lineStart, lineEnd - lineStart );
            lineStart        = lineEnd + 1;
            while ( line.empty() == false && ( line.back() == '\r' ) )
            {
                line.remove_suffix( 1 );
            }
            if ( line.empty() )
                continue;
            SceneObjectHash object;
            const bool      bHasHash = line.size() > 17 && line[16] == ' ' && StringUtil::parseUint64( line.substr( 0, 16 ), object._hash, 16 );
            if ( bHasHash == false )
            {
                outListObject.clear();
                return false;
            }
            object._name = string( line.substr( 17 ) );
            outListObject.push_back( object );
        }
        return true;
    }

    void SceneObjectHash::writeTickedObjectReport( uint64 tick, const vector<SceneObjectHash>& listObject, string& outText )
    {
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "tick %#\n", tick );
        outText = string( textBuilder.view() );
        writeObjectReport( listObject, outText );
    }

    bool SceneObjectHash::parseTickedObjectReport( string_view text, uint64& outTick, vector<SceneObjectHash>& outListObject )
    {
        outListObject.clear();
        constexpr string_view kPrefix = "tick ";
        const size_t          lineEnd = text.find( '\n' );
        if ( text.substr( 0, kPrefix.size() ) != kPrefix || lineEnd == string_view::npos )
            return false;
        string_view tickText = text.substr( kPrefix.size(), lineEnd - kPrefix.size() );
        while ( tickText.empty() == false && tickText.back() == '\r' )
        {
            tickText.remove_suffix( 1 );
        }
        uint64 tick = 0;
        if ( StringUtil::parseUint64( tickText, tick ) == false )
            return false;
        if ( parseObjectReport( text.substr( lineEnd + 1 ), outListObject ) == false )
            return false;
        outTick = tick;
        return true;
    }

    bool SceneObjectHash::writeObjectReportLine( uint64 tick, const vector<SceneObjectHash>& listObject, string& outLine )
    {
        outLine = kObjectReportLinePrefix;
        outLine += ' ';
        outLine += to_string( tick );
        outLine += ' ';
        if ( listObject.empty() )
        {
            outLine += '-';
            return true;
        }
        for ( size_t index = 0; index < listObject.size(); ++index )
        {
            if ( index > 0 )
                outLine += ',';
            HexUtil::appendUint64( outLine, listObject[index]._hash );
            outLine += ':';
            for ( const utf8 ch : listObject[index]._name )
            {
                if ( SceneStateHashInternal::needsEscape( ch ) )
                {
                    const uint8 code = static_cast<uint8>( ch );
                    outLine += '%';
                    HexUtil::appendBytes( outLine, &code, 1, HexCase::Upper );
                }
                else
                {
                    outLine += ch;
                }
            }
        }
        if ( DebugReplyLine::canFit( outLine.size() ) == false )
        {
            SW_LOG_ERROR_ID( "State.ReportLineTooLong", "SceneStateHash: the object report line is %# bytes for %# objects, over the %# byte line limit - not written", outLine.size(), listObject.size(), DebugReplyLine::kMaxBytes );
            outLine.clear();
            return false;
        }
        return true;
    }

    bool SceneObjectHash::tryParseObjectReportLine( string_view line, uint64& outTick, vector<SceneObjectHash>& outListObject )
    {
        outListObject.clear();
        const size_t prefixAt = line.find( kObjectReportLinePrefix );
        if ( prefixAt == string_view::npos )
            return false;
        string_view  body    = StringUtil::trim( line.substr( prefixAt + string_view( kObjectReportLinePrefix ).size() ) );
        const size_t tickEnd = body.find( ' ' );
        uint64       tick    = 0;
        if ( tickEnd == string_view::npos || StringUtil::parseUint64( body.substr( 0, tickEnd ), tick ) == false )
            return false;
        body = StringUtil::trim( body.substr( tickEnd + 1 ) );
        body = body.substr( 0, body.find_first_of( " \t" ) ); // 로그 꼬리는 읽지 않는다
        if ( DebugReplyLine::canFit( static_cast<size_t>( body.data() + body.size() - ( line.data() + prefixAt ) ) ) == false )
            return false; // 쓰는 쪽이 만들 수 없는 길이 — 잘린 줄이거나 다른 글이다
        if ( body != "-" )
        {
            for ( const string_view item : StringUtil::split( body, "," ) )
            {
                const size_t    colon = item.find( ':' );
                SceneObjectHash object;
                const bool      bHasHash = colon == 16 && StringUtil::parseUint64( item.substr( 0, colon ), object._hash, 16 );
                const bool      bHasName = bHasHash && colon + 1 < item.size() && SceneStateHashInternal::unescapeName( item.substr( colon + 1 ), object._name );
                if ( bHasName == false )
                {
                    outListObject.clear();
                    return false;
                }
                outListObject.push_back( std::move( object ) );
            }
        }
        outTick = tick;
        return true;
    }

    bool SceneObjectHash::findDifferingObjects( const vector<SceneObjectHash>& listLeft, const vector<SceneObjectHash>& listRight, vector<string>& outListName )
    {
        outListName.clear();
        const auto less = []( const SceneObjectHash& first, const SceneObjectHash& second )
        {
            return first._name != second._name ? first._name < second._name : first._hash < second._hash;
        };
        vector<SceneObjectHash> listSortedLeft  = listLeft;
        vector<SceneObjectHash> listSortedRight = listRight;
        VectorUtil::sort( listSortedLeft, less );
        VectorUtil::sort( listSortedRight, less );
        size_t leftIndex  = 0;
        size_t rightIndex = 0;
        while ( leftIndex < listSortedLeft.size() || rightIndex < listSortedRight.size() )
        {
            const bool bLeftOnly  = rightIndex >= listSortedRight.size() || ( leftIndex < listSortedLeft.size() && less( listSortedLeft[leftIndex], listSortedRight[rightIndex] ) );
            const bool bRightOnly = leftIndex >= listSortedLeft.size() || ( rightIndex < listSortedRight.size() && less( listSortedRight[rightIndex], listSortedLeft[leftIndex] ) );
            if ( bLeftOnly )
            {
                outListName.push_back( listSortedLeft[leftIndex++]._name );
            }
            else if ( bRightOnly )
            {
                outListName.push_back( listSortedRight[rightIndex++]._name );
            }
            else
            {
                ++leftIndex;
                ++rightIndex;
            }
        }
        outListName.erase( std::unique( outListName.begin(), outListName.end() ), outListName.end() );
        return outListName.empty() == false;
    }

    void StateHashHistory::recordActiveScene( uint64 tick )
    {
        if ( isRecording() == false || engine::areEngineServicesBound() == false )
            return;
        StateHashSample latest;
        if ( tryGetLatest( latest ) && latest._tick == tick )
            return;
        const Scene* pScene = engine::getSubsystem<SceneManager>().getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        record( tick, SceneObjectHash::compute( *pScene->getObjectManager() ) );
        recordChannels( tick );
    }

    void StateHashHistory::recordChannels( uint64 hostTick )
    {
        if ( isRecording() == false )
            return;
        // 키트의 줄은 같은 호스트 틱에 남긴다. 제공자가 바꾼 틱이 그 줄의 가장 최근 틱과 같으면 두 번 남기지 않는다(비교가 같은 틱의 두 값을 보지 않게).
        for ( Channel& channel : _listChannel )
        {
            StateHashSample sample;
            if ( channel._pProvider == nullptr || channel._pProvider->sampleStateHash( hostTick, sample ) == false )
                continue;
            StateHashSample channelLatest;
            if ( channel._ring.tryGetLatest( _capacity, channelLatest ) && channelLatest._tick == sample._tick )
                continue;
            channel._ring.record( _capacity, sample );
        }
    }

    void StateHashHistory::Ring::reset( uint32 capacity )
    {
        _listSample.clear();
        _head = 0;
        if ( capacity > 0 )
            _listSample.reserve( capacity );
    }

    void StateHashHistory::Ring::record( uint32 capacity, const StateHashSample& sample )
    {
        if ( capacity == 0 )
            return;
        if ( _listSample.size() < capacity )
        {
            _listSample.push_back( sample );
            return;
        }
        _listSample[_head] = sample;
        _head              = ( _head + 1 ) % capacity;
    }

    bool StateHashHistory::Ring::tryGetLatest( uint32 capacity, StateHashSample& outSample ) const
    {
        if ( _listSample.empty() )
            return false;
        const size_t latest = _listSample.size() < capacity ? _listSample.size() - 1 : ( _head + capacity - 1 ) % capacity;
        outSample           = _listSample[latest];
        return true;
    }

    void StateHashHistory::Ring::collect( uint32 capacity, uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const
    {
        outListSample.clear();
        const size_t size  = _listSample.size();
        const size_t start = size < capacity ? 0 : _head;
        for ( size_t offset = 0; offset < size && outListSample.size() < count; ++offset )
        {
            const StateHashSample& sample = _listSample[( start + offset ) % size];
            if ( sample._tick >= fromTick )
                outListSample.push_back( sample );
        }
    }

    void StateHashHistory::setCapacity( uint32 capacity )
    {
        _capacity = capacity;
        _scene.reset( capacity );
        for ( Channel& channel : _listChannel )
        {
            channel._ring.reset( capacity );
        }
    }

    void StateHashHistory::record( uint64 tick, uint64 hash )
    {
        _scene.record( _capacity, StateHashSample{ tick, hash } );
    }

    bool StateHashHistory::tryGetLatest( StateHashSample& outSample ) const
    {
        return _scene.tryGetLatest( _capacity, outSample );
    }

    void StateHashHistory::collect( uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const
    {
        _scene.collect( _capacity, fromTick, count, outListSample );
    }

    bool StateHashHistory::isValidChannelName( string_view name )
    {
        constexpr size_t kMaxNameLength = 32;
        if ( name.empty() || name.size() > kMaxNameLength || name == kSceneChannelName )
            return false;
        for ( const utf8 ch : name )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.' || ch == '-';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    const StateHashHistory::Channel* StateHashHistory::findChannel( string_view name ) const
    {
        for ( const Channel& channel : _listChannel )
        {
            if ( channel._name == name )
                return &channel;
        }
        return nullptr;
    }

    Subscription StateHashHistory::registerChannel( string_view name, const IStateHashProvider* pProvider )
    {
        if ( pProvider == nullptr || isValidChannelName( name ) == false )
        {
            SW_LOG_ERROR_ID( "State.ChannelInvalid", "StateHashHistory: channel '%#' not registered - the name must be [a-z0-9_.-] (1-32, not 'scene') and the provider must exist", name );
            return Subscription{};
        }
        Channel* pChannel = nullptr;
        for ( Channel& channel : _listChannel )
        {
            pChannel = channel._name == name ? &channel : pChannel;
        }
        if ( pChannel != nullptr && pChannel->_pProvider != nullptr )
        {
            SW_LOG_ERROR_ID( "State.ChannelTaken", "StateHashHistory: channel '%#' is already provided by another owner - not registered", name );
            return Subscription{};
        }
        if ( pChannel == nullptr )
        {
            if ( _listChannel.size() >= kMaxChannels )
            {
                SW_LOG_ERROR_ID( "State.ChannelLimit", "StateHashHistory: more than %# channels - '%#' not registered", static_cast<int32>( kMaxChannels ), name );
                return Subscription{};
            }
            Channel& added = _listChannel.emplace_back();
            added._name    = string( name );
            added._ring.reset( _capacity );
            pChannel = &added;
        }
        pChannel->_pProvider = pProvider;
        return _subscriptions.issue( pProvider );
    }

    void StateHashHistory::releaseProvider( const IStateHashProvider* pProvider )
    {
        for ( Channel& channel : _listChannel )
        {
            channel._pProvider = channel._pProvider == pProvider ? nullptr : channel._pProvider; // 이력은 남긴다
        }
    }

    void StateHashHistory::collectChannelNames( vector<string>& outListName ) const
    {
        outListName.clear();
        for ( const Channel& channel : _listChannel )
        {
            outListName.push_back( channel._name );
        }
    }

    bool StateHashHistory::hasChannel( string_view name ) const
    {
        return name == kSceneChannelName || findChannel( name ) != nullptr;
    }

    bool StateHashHistory::tryGetLatestChannel( string_view name, StateHashSample& outSample ) const
    {
        if ( name == kSceneChannelName )
            return tryGetLatest( outSample );
        const Channel* pChannel = findChannel( name );
        return pChannel != nullptr && pChannel->_ring.tryGetLatest( _capacity, outSample );
    }

    void StateHashHistory::collectChannel( string_view name, uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const
    {
        outListSample.clear();
        if ( name == kSceneChannelName )
        {
            collect( fromTick, count, outListSample );
            return;
        }
        const Channel* pChannel = findChannel( name );
        if ( pChannel != nullptr )
            pChannel->_ring.collect( _capacity, fromTick, count, outListSample );
    }

    void StateHashHistory::writeReport( const vector<StateHashSample>& listSample, string& outText )
    {
        for ( const StateHashSample& sample : listSample )
        {
            utf8                       arrBuffer[constant::buffer::kMax32];
            const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), sample._tick );
            outText.append( arrBuffer, result.ptr );
            outText += ' ';
            HexUtil::appendUint64( outText, sample._hash );
            outText += '\n';
        }
    }

    bool StateHashHistory::writeReportLine( const vector<StateHashSample>& listSample, string& outText )
    {
        outText = kReportLinePrefix;
        outText += ' ';
        if ( listSample.empty() )
        {
            outText += '-';
            return true;
        }
        string block;
        writeReport( listSample, block ); // `<tick> <hash>\n` 줄들 — 한 줄 판은 공백을 ':' 로, 줄 끝을 ',' 로 바꾼다
        for ( utf8& ch : block )
        {
            if ( ch == ' ' )
                ch = ':';
            else if ( ch == '\n' )
                ch = ',';
        }
        block.pop_back(); // 마지막 ','
        outText += block;
        if ( DebugReplyLine::canFit( outText.size() ) == false )
        {
            SW_LOG_ERROR_ID( "State.ReportLineTooLong", "StateHashHistory: the hash line is %# bytes for %# samples, over the %# byte line limit - not written", outText.size(), listSample.size(),
                             DebugReplyLine::kMaxBytes );
            outText.clear();
            return false;
        }
        return true;
    }

    bool StateHashHistory::tryParseReportLine( string_view line, vector<StateHashSample>& outListSample )
    {
        const size_t prefixAt = line.find( kReportLinePrefix );
        if ( prefixAt == string_view::npos )
            return false;
        outListSample.clear();
        string_view body = StringUtil::trim( line.substr( prefixAt + string_view( kReportLinePrefix ).size() ) );
        body             = body.substr( 0, body.find_first_of( " \t" ) ); // 로그 꼬리는 읽지 않는다
        if ( DebugReplyLine::canFit( static_cast<size_t>( body.data() + body.size() - ( line.data() + prefixAt ) ) ) == false )
            return false; // 쓰는 쪽이 만들 수 없는 길이 — 잘린 줄이거나 다른 글이다
        if ( body == "-" )
            return true;
        for ( const string_view item : StringUtil::split( body, "," ) )
        {
            const size_t    colon = item.find( ':' );
            StateHashSample sample;
            const bool      bHasTick = colon != string_view::npos && StringUtil::parseUint64( item.substr( 0, colon ), sample._tick );
            const bool      bHasHash = bHasTick && StringUtil::parseUint64( item.substr( colon + 1 ), sample._hash, 16 );
            if ( bHasHash == false )
            {
                outListSample.clear();
                return false;
            }
            outListSample.push_back( sample );
        }
        return true;
    }

    bool StateHashHistory::parseReport( string_view text, vector<StateHashSample>& outListSample )
    {
        outListSample.clear();
        size_t lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            const string_view line = StringUtil::trim( text.substr( lineStart, lineEnd - lineStart ) );
            lineStart              = lineEnd + 1;
            if ( line.empty() )
                continue;
            const size_t    space = line.find( ' ' );
            StateHashSample sample;
            const bool      bHasTick = space != string_view::npos && StringUtil::parseUint64( line.substr( 0, space ), sample._tick );
            const bool      bHasHash = bHasTick && StringUtil::parseUint64( line.substr( space + 1 ), sample._hash, 16 );
            // 읽지 못한 줄을 건너뛰면 그 틱이 비교에서 조용히 빠진다 — 다른 글(오류 답 · 다른 파일)을 이력으로 받지 않는다.
            if ( bHasHash == false )
            {
                outListSample.clear();
                return false;
            }
            outListSample.push_back( sample );
        }
        return true;
    }

    bool StateHashHistory::findFirstDivergence( const vector<StateHashSample>& listLeft, const vector<StateHashSample>& listRight, uint64& outTick )
    {
        bool bFound = false;
        for ( const StateHashSample& left : listLeft )
        {
            for ( const StateHashSample& right : listRight )
            {
                const bool bDiffers = left._tick == right._tick && left._hash != right._hash;
                const bool bEarlier = bFound == false || left._tick < outTick;
                if ( bDiffers && bEarlier )
                {
                    outTick = left._tick;
                    bFound  = true;
                }
            }
        }
        return bFound;
    }

    SW_DEV_COMMAND( StateHash, "state.hash", "state.hash", "Print the state hash of the active scene right now", &SceneStateHashCommandsInternal::runStateHash );
    SW_DEV_COMMAND( StateObjects, "state.objects", "state.objects", "Print one hash per named object of the active scene (compare two processes to find which object differs)",
                    &SceneStateHashCommandsInternal::runStateObjects );
    SW_DEV_COMMAND( StateHashes, "state.hashes", "state.hashes <fromTick> <count> [channel]",
                    "Print the recorded per-tick state hashes (needs -gv_stateHashHistory); channel is a kit's hash line from state.hashnames, default the scene",
                    &SceneStateHashCommandsInternal::runStateHashes );
    SW_DEV_COMMAND( StateHashNames, "state.hashnames", "state.hashnames", "List the state hash history's channels (the scene and the lines kits registered, e.g. destruction, rollback)",
                    &SceneStateHashCommandsInternal::runStateHashNames );
    SW_DEV_COMMAND( StateHashLine, "state.hashline", "state.hashline <count> [channel]",
                    "Print the latest <count> recorded state hashes (of a kit's hash line, default the scene) on one line (the editor Sync tab reads client histories this way)",
                    &SceneStateHashCommandsInternal::runStateHashLine );
    SW_DEV_COMMAND( StateObjectLine, "state.objectline", "state.objectline", "Print the state.objects hashes with their tick on one line (the editor Sync tab reads client object hashes this way)",
                    &SceneStateHashCommandsInternal::runStateObjectLine );
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( StateHashHistory, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( StateHashHistory ) );
} // namespace sw
