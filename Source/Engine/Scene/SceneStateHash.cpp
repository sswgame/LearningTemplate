#include "pch.h"

#include "Engine/Scene/SceneStateHash.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Network/DebugReplyLine.h"
#include "Core/String/HexUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace sw
{
    namespace
    {
        struct SceneStateHashInternal
        {
            static void foldFloat( uint64& inoutHash, float32 value )
            {
                const uint32 bits = BitUtil::floatToBits( value );
                inoutHash         = HashUtil::appendBytes64( inoutHash, &bits, sizeof( bits ) );
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
                if ( '0' <= ch && ch <= '9' )
                    return ch - '0';
                if ( 'A' <= ch && ch <= 'F' )
                    return ch - 'A' + 10;
                return -1;
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
    } // namespace

    /**
     * @brief `-gv_stateHashHistory=N`: 틱마다 씬 상태 해시를 남기고 최근 N 틱을 보관합니다(0=끄기). 개발 명령 `state.hashes` 로 읽습니다.
     * @details 전용 서버는 자기 틱으로, 클라이언트 App 은 서버가 알린 틱으로 남긴다(`StateHashHistory::recordActiveScene`). 에디터 멀티 프로세스 재생이 둘 다에 준다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stateHashHistory, 0, "틱별 씬 상태 해시 이력 길이 (0=끄기)" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneStateHash" );

    // 샘플 하나는 가장 길 때 `<20자리 틱>:<16자리 해시>,` = 38 바이트다.
    static_assert( 38u * StateHashHistory::kMaxReportLineSamples + 32u <= DebugReplyLine::kMaxBytes, "kMaxReportLineSamples 가 한 줄 상한 안에 들어야 한다" );

    uint64 SceneStateHash::compute( const GameObjectManager& manager )
    {
        vector<SceneStateHashInternal::Entry> listEntry;
        manager.forEachGameObject( [&listEntry]( GameObject* pObject )
        {
            if ( pObject->getName().empty() )
                return;
            SceneStateHashInternal::Entry& entry = listEntry.emplace_back();
            entry._name                          = pObject->getName().c_str();
            entry._bActive                       = pObject->isActive() ? 1 : 0;
            const SceneComponent* pRoot          = pObject->getComponent<SceneComponent>();
            if ( pRoot != nullptr )
                entry._position = pRoot->getWorldPosition();
        } );
        // 이름이 같은 오브젝트끼리는 값으로 정렬해 프로세스마다 순회 순서가 달라도 같은 결과가 되게 한다.
        std::sort( listEntry.begin(), listEntry.end(), []( const SceneStateHashInternal::Entry& left, const SceneStateHashInternal::Entry& right )
        {
            if ( left._name != right._name )
                return left._name < right._name;
            if ( left._bActive != right._bActive )
                return left._bActive < right._bActive;
            if ( left._position._x != right._position._x )
                return left._position._x < right._position._x;
            if ( left._position._y != right._position._y )
                return left._position._y < right._position._y;
            return left._position._z < right._position._z;
        } );
        uint64 hash = HashUtil::kFnvOffset64;
        for ( const SceneStateHashInternal::Entry& entry : listEntry )
        {
            hash = HashUtil::appendBytes64( hash, entry._name.data(), entry._name.size() );
            hash = HashUtil::appendBytes64( hash, &entry._bActive, sizeof( entry._bActive ) );
            SceneStateHashInternal::foldFloat( hash, entry._position._x );
            SceneStateHashInternal::foldFloat( hash, entry._position._y );
            SceneStateHashInternal::foldFloat( hash, entry._position._z );
        }
        return hash;
    }

    void SceneStateHash::collectObjects( const GameObjectManager& manager, vector<SceneObjectHash>& outListObject )
    {
        outListObject.clear();
        manager.forEachGameObject( [&outListObject]( GameObject* pObject )
        {
            if ( pObject->getName().empty() )
                return;
            SceneObjectHash& object        = outListObject.emplace_back();
            object._name                   = pObject->getName().c_str();
            const uint8           bActive  = pObject->isActive() ? 1 : 0;
            const SceneComponent* pRoot    = pObject->getComponent<SceneComponent>();
            const float3          position = pRoot != nullptr ? pRoot->getWorldPosition() : float3{};
            uint64                hash     = HashUtil::kFnvOffset64;
            hash                           = HashUtil::appendBytes64( hash, object._name.data(), object._name.size() );
            hash                           = HashUtil::appendBytes64( hash, &bActive, sizeof( bActive ) );
            SceneStateHashInternal::foldFloat( hash, position._x );
            SceneStateHashInternal::foldFloat( hash, position._y );
            SceneStateHashInternal::foldFloat( hash, position._z );
            object._hash = hash;
        } );
        std::sort( outListObject.begin(), outListObject.end(), []( const SceneObjectHash& left, const SceneObjectHash& right )
        {
            return left._name != right._name ? left._name < right._name : left._hash < right._hash;
        } );
    }

    void SceneStateHash::writeObjectReport( const vector<SceneObjectHash>& listObject, string& outText )
    {
        for ( const SceneObjectHash& object : listObject )
        {
            HexUtil::appendUint64( outText, object._hash );
            outText += ' ';
            outText += object._name;
            outText += '\n';
        }
    }

    bool SceneStateHash::parseObjectReport( string_view text, vector<SceneObjectHash>& outListObject )
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

    void SceneStateHash::writeTickedObjectReport( uint64 tick, const vector<SceneObjectHash>& listObject, string& outText )
    {
        outText = "tick " + to_string( tick ) + "\n";
        writeObjectReport( listObject, outText );
    }

    bool SceneStateHash::parseTickedObjectReport( string_view text, uint64& outTick, vector<SceneObjectHash>& outListObject )
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

    bool SceneStateHash::writeObjectReportLine( uint64 tick, const vector<SceneObjectHash>& listObject, string& outLine )
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
        if ( DebugReplyLine::fits( outLine.size() ) == false )
        {
            SW_LOG_ERROR_ID( "State.ReportLineTooLong", "SceneStateHash: the object report line is %# bytes for %# objects, over the %# byte line limit - not written", outLine.size(), listObject.size(), DebugReplyLine::kMaxBytes );
            outLine.clear();
            return false;
        }
        return true;
    }

    bool SceneStateHash::tryParseObjectReportLine( string_view line, uint64& outTick, vector<SceneObjectHash>& outListObject )
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
        if ( DebugReplyLine::fits( static_cast<size_t>( body.data() + body.size() - ( line.data() + prefixAt ) ) ) == false )
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

    bool SceneStateHash::findDifferingObjects( const vector<SceneObjectHash>& listLeft, const vector<SceneObjectHash>& listRight, vector<string>& outListName )
    {
        outListName.clear();
        const auto less = []( const SceneObjectHash& first, const SceneObjectHash& second )
        {
            return first._name != second._name ? first._name < second._name : first._hash < second._hash;
        };
        vector<SceneObjectHash> listSortedLeft  = listLeft;
        vector<SceneObjectHash> listSortedRight = listRight;
        std::sort( listSortedLeft.begin(), listSortedLeft.end(), less );
        std::sort( listSortedRight.begin(), listSortedRight.end(), less );
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

    SubsystemInitResult StateHashHistory::initializeSubsystem()
    {
        if ( gv_stateHashHistory > 0 )
            setCapacity( static_cast<uint32>( gv_stateHashHistory ) );
        return SubsystemInitResult::Succeeded;
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
        record( tick, SceneStateHash::compute( *pScene->getObjectManager() ) );
    }

    void StateHashHistory::setCapacity( uint32 capacity )
    {
        _listSample.clear();
        _head     = 0;
        _capacity = capacity;
        if ( capacity > 0 )
            _listSample.reserve( capacity );
    }

    void StateHashHistory::record( uint64 tick, uint64 hash )
    {
        if ( _capacity == 0 )
            return;
        const StateHashSample sample{ tick, hash };
        if ( _listSample.size() < _capacity )
        {
            _listSample.push_back( sample );
            return;
        }
        _listSample[_head] = sample;
        _head              = ( _head + 1 ) % _capacity;
    }

    bool StateHashHistory::tryGetLatest( StateHashSample& outSample ) const
    {
        if ( _listSample.empty() )
            return false;
        const size_t latest = _listSample.size() < _capacity ? _listSample.size() - 1 : ( _head + _capacity - 1 ) % _capacity;
        outSample           = _listSample[latest];
        return true;
    }

    void StateHashHistory::collect( uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const
    {
        outListSample.clear();
        const size_t size  = _listSample.size();
        const size_t start = size < _capacity ? 0 : _head;
        for ( size_t offset = 0; offset < size && outListSample.size() < count; ++offset )
        {
            const StateHashSample& sample = _listSample[( start + offset ) % size];
            if ( sample._tick >= fromTick )
                outListSample.push_back( sample );
        }
    }

    void StateHashHistory::writeReport( const vector<StateHashSample>& listSample, string& outText )
    {
        for ( const StateHashSample& sample : listSample )
        {
            utf8                       arrBuffer[constant::kMaxBuffer32];
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
        if ( DebugReplyLine::fits( outText.size() ) == false )
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
        if ( DebugReplyLine::fits( static_cast<size_t>( body.data() + body.size() - ( line.data() + prefixAt ) ) ) == false )
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
} // namespace sw
