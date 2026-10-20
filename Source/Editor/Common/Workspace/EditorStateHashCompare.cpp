#include "pch.h"

#include "Editor/Common/Workspace/EditorStateHashCompare.h"

#include "Core/File/FileUtil.h"

#include "Engine/Observability/DebugChannel.h"

#include <algorithm>

namespace sw::editor
{
    EditorStateHashCompare::EditorStateHashCompare()
        : _arrSource{}
        , _comparison{}
        , _lastError{}
        , _bComparisonCurrent{ false }
    {
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
        _bComparisonCurrent = false;
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
        const bool bAnswered = client.request( "state.hashes 0 " + to_string( kMaxRequestedTicks ), kChannelTimeoutMilliseconds, body );
        client.disconnect();
        if ( bAnswered == false )
        {
            // 서버가 이력을 켜지 않았으면 `state.hashes` 가 그 이유를 오류로 답한다.
            _lastError = body.empty() ? "no state.hashes answer from " + remote.toString() : body;
            return false;
        }
        return loadFromReport( sourceIndex, remote.toString(), body );
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
        _arrSource[sourceIndex] = EditorStateHashSource{};
        _bComparisonCurrent     = false;
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
