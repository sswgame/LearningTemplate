#include "pch.h"

#include "Editor/Common/Workspace/EditorStateHashCompare.h"

#include "Core/File/FileUtil.h"

#include "Engine/Observability/DebugChannel.h"

#include <algorithm>

namespace sw::editor
{
    EditorStateHashCompare::EditorStateHashCompare()
        : _arrSource{}
        , _arrChannel{}
        , _arrClientProcess{ -1, -1 }
        , _comparison{}
        , _listDifferingObject{}
        , _lastError{}
        , _hashChannel{ StateHashHistory::kSceneChannelName }
        , _objectTick{ 0 }
        , _bComparisonCurrent{ false }
        , _bObjectComparison{ false }
    {
    }

    bool EditorStateHashCompare::setHashChannel( string_view channelName )
    {
        if ( channelName != StateHashHistory::kSceneChannelName && StateHashHistory::isValidChannelName( channelName ) == false )
        {
            _lastError = "'" + string( channelName ) + "' is not a state hash channel name ([a-z0-9_.-], up to 32 characters)";
            return false;
        }
        _hashChannel        = string( channelName );
        _bComparisonCurrent = false;
        return true;
    }

    bool EditorStateHashCompare::isSourceIndexValid( int32 sourceIndex )
    {
        const bool bValid = 0 <= sourceIndex && sourceIndex < kSourceCount;
        if ( bValid == false )
            _lastError = "no history slot " + to_string( sourceIndex );
        return bValid;
    }

    const EditorStateHashSource& EditorStateHashCompare::getSource( int32 sourceIndex ) const
    {
        SW_ASSERT( 0 <= sourceIndex && sourceIndex < kSourceCount );
        return _arrSource[sourceIndex];
    }

    bool EditorStateHashCompare::loadFromReport( int32 sourceIndex, string_view label, string_view reportText )
    {
        if ( isSourceIndexValid( sourceIndex ) == false )
            return false;
        vector<StateHashSample> listSample;
        if ( StateHashHistory::parseReport( reportText, listSample ) == false )
        {
            _lastError = string( label ) + " is not a state hash history (<tick> <hex hash> per line)";
            return false;
        }
        if ( listSample.empty() )
        {
            _lastError = string( label ) + " has no recorded ticks";
            return false;
        }
        std::sort( listSample.begin(), listSample.end(), []( const StateHashSample& left, const StateHashSample& right )
        {
            return left._tick < right._tick;
        } );
        EditorStateHashSource& source = _arrSource[sourceIndex];
        source._label                 = string( label );
        source._listSample.swap( listSample );
        _arrChannel[sourceIndex]       = ChannelOrigin{}; // 채널에서 읽었으면 loadFromChannel 이 곧 다시 채운다
        _arrClientProcess[sourceIndex] = -1;              // 클라이언트에서 읽었으면 탭이 setClientOrigin 으로 다시 적는다
        _bComparisonCurrent            = false;
        _bObjectComparison             = false;
        return true;
    }

    bool EditorStateHashCompare::loadFromChannel( int32 sourceIndex, const NetAddress& remote, string_view token )
    {
        if ( isSourceIndexValid( sourceIndex ) == false )
            return false;
        DebugChannelClient client;
        if ( client.connect( remote, token, kChannelTimeoutMilliseconds ) == false )
        {
            _lastError = "could not connect to the debug channel at " + remote.toString();
            return false;
        }
        string     body;
        const bool bSceneChannel = _hashChannel == StateHashHistory::kSceneChannelName;
        const bool bAnswered     = client.request( "state.hashes 0 " + to_string( kMaxRequestedTicks ) + ( bSceneChannel ? string{} : " " + _hashChannel ), kChannelTimeoutMilliseconds, body );
        client.disconnect();
        if ( bAnswered == false )
        {
            // 서버가 이력을 켜지 않았거나 그 이름의 줄이 없으면 `state.hashes` 가 그 이유를 오류로 답한다.
            _lastError = body.empty() ? "no state.hashes answer from " + remote.toString() : body;
            return false;
        }
        if ( loadFromReport( sourceIndex, bSceneChannel ? remote.toString() : remote.toString() + " [" + _hashChannel + "]", body ) == false )
            return false;
        _arrChannel[sourceIndex] = ChannelOrigin{ remote, string( token ), true };
        return true;
    }

    bool EditorStateHashCompare::readObjectsFromChannel( int32 sourceIndex, uint64& outTick, vector<SceneObjectHash>& outListObject )
    {
        if ( isSourceIndexValid( sourceIndex ) == false )
            return false;
        const ChannelOrigin& origin = _arrChannel[sourceIndex];
        if ( origin._bValid == false )
        {
            _lastError = "slot " + to_string( sourceIndex ) + " was not read from a debug channel - object hashes come only from a live process";
            return false;
        }
        DebugChannelClient client;
        if ( client.connect( origin._remote, origin._token, kChannelTimeoutMilliseconds ) == false )
        {
            _lastError = "could not connect to the debug channel at " + origin._remote.toString();
            return false;
        }
        string     text;
        const bool bAnswered = client.request( "state.objects", kChannelTimeoutMilliseconds, text );
        client.disconnect();
        if ( bAnswered == false )
        {
            _lastError = text.empty() ? "no state.objects answer from " + origin._remote.toString() : text;
            return false;
        }
        if ( SceneStateHash::parseTickedObjectReport( text, outTick, outListObject ) == false )
        {
            _lastError = "an answer is not a ticked object hash list (tick <n>, then <hex hash> <name> per line)";
            return false;
        }
        return true;
    }

    bool EditorStateHashCompare::compareObjectsFromChannels()
    {
        uint64                  arrTick[kSourceCount] = {};
        vector<SceneObjectHash> arrList[kSourceCount];
        for ( int32 sourceIndex = 0; sourceIndex < kSourceCount; ++sourceIndex )
        {
            if ( readObjectsFromChannel( sourceIndex, arrTick[sourceIndex], arrList[sourceIndex] ) == false )
                return false;
        }
        return compareObjects( arrTick[0], arrList[0], arrTick[1], arrList[1] );
    }

    void EditorStateHashCompare::setClientOrigin( int32 sourceIndex, int32 processIndex )
    {
        if ( isSourceIndexValid( sourceIndex ) )
            _arrClientProcess[sourceIndex] = processIndex;
    }

    int32 EditorStateHashCompare::getClientOrigin( int32 sourceIndex ) const
    {
        return 0 <= sourceIndex && sourceIndex < kSourceCount ? _arrClientProcess[sourceIndex] : -1;
    }

    bool EditorStateHashCompare::hasChannelOrigin( int32 sourceIndex ) const
    {
        return 0 <= sourceIndex && sourceIndex < kSourceCount && _arrChannel[sourceIndex]._bValid;
    }

    bool EditorStateHashCompare::compareObjectReports( string_view leftText, string_view rightText )
    {
        vector<SceneObjectHash> listLeft;
        vector<SceneObjectHash> listRight;
        uint64                  leftTick  = 0;
        uint64                  rightTick = 0;
        if ( SceneStateHash::parseTickedObjectReport( leftText, leftTick, listLeft ) == false || SceneStateHash::parseTickedObjectReport( rightText, rightTick, listRight ) == false )
        {
            _lastError = "an answer is not a ticked object hash list (tick <n>, then <hex hash> <name> per line)";
            return false;
        }
        return compareObjects( leftTick, listLeft, rightTick, listRight );
    }

    bool EditorStateHashCompare::compareObjects( uint64 leftTick, const vector<SceneObjectHash>& listLeft, uint64 rightTick, const vector<SceneObjectHash>& listRight )
    {
        if ( leftTick != rightTick )
        {
            // 두 서버가 다른 틱에서 답했다 — 그 해시를 견주면 움직이는 오브젝트가 모두 다른 것으로 나온다. 두 서버를 pause 한 채 다시 누른다.
            _lastError = "the two servers answered at different ticks (" + to_string( leftTick ) + " vs " + to_string( rightTick ) + ") - pause both and try again";
            return false;
        }
        vector<string> listName;
        static_cast<void>( SceneStateHash::findDifferingObjects( listLeft, listRight, listName ) );
        _listDifferingObject = std::move( listName );
        _objectTick          = leftTick;
        _bObjectComparison   = true;
        return true;
    }

    bool EditorStateHashCompare::loadFromFile( int32 sourceIndex, string_view path )
    {
        if ( isSourceIndexValid( sourceIndex ) == false )
            return false;
        string text;
        if ( FileUtil::readTextFile( path, text ) == false )
        {
            _lastError = "could not read " + string( path );
            return false;
        }
        return loadFromReport( sourceIndex, path, text );
    }

    bool EditorStateHashCompare::saveToFile( int32 sourceIndex, string_view path ) const
    {
        const bool bValid = 0 <= sourceIndex && sourceIndex < kSourceCount && _arrSource[sourceIndex]._listSample.empty() == false;
        if ( bValid == false )
            return false;
        const string directory = FileUtil::getDirectoryPart( path );
        if ( directory.empty() == false && FileUtil::ensureDirectoryExists( directory ) == false )
            return false;
        string text;
        StateHashHistory::writeReport( _arrSource[sourceIndex]._listSample, text );
        return FileUtil::writeTextFile( path, text );
    }

    void EditorStateHashCompare::clearSource( int32 sourceIndex )
    {
        if ( isSourceIndexValid( sourceIndex ) == false )
            return;
        _arrSource[sourceIndex]        = EditorStateHashSource{};
        _arrChannel[sourceIndex]       = ChannelOrigin{};
        _arrClientProcess[sourceIndex] = -1;
        _bComparisonCurrent            = false;
        _bObjectComparison             = false;
    }

    bool EditorStateHashCompare::compare()
    {
        const vector<StateHashSample>& listLeft  = _arrSource[0]._listSample;
        const vector<StateHashSample>& listRight = _arrSource[1]._listSample;
        if ( listLeft.empty() || listRight.empty() )
        {
            _lastError = "load both histories before comparing";
            return false;
        }
        EditorStateHashComparison comparison;
        comparison._commonTickCount = countCommonTicks( listLeft, listRight );
        comparison._bDiverged       = StateHashHistory::findFirstDivergence( listLeft, listRight, comparison._firstDivergentTick );
        _comparison                 = comparison;
        _bComparisonCurrent         = true;
        return true;
    }

    uint32 EditorStateHashCompare::countCommonTicks( const vector<StateHashSample>& listLeft, const vector<StateHashSample>& listRight )
    {
        uint32 count      = 0;
        size_t leftIndex  = 0;
        size_t rightIndex = 0;
        while ( leftIndex < listLeft.size() && rightIndex < listRight.size() )
        {
            const uint64 leftTick  = listLeft[leftIndex]._tick;
            const uint64 rightTick = listRight[rightIndex]._tick;
            if ( leftTick == rightTick )
            {
                ++count;
                ++leftIndex;
                ++rightIndex;
            }
            else if ( leftTick < rightTick )
            {
                ++leftIndex;
            }
            else
            {
                ++rightIndex;
            }
        }
        return count;
    }
} // namespace sw::editor
