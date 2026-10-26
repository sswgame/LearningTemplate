#include "pch.h"

#include "Engine/Input/Map/InputActionBuffer.h"

namespace sw
{
    InputActionBuffer::InputActionBuffer()
        : _listBufferedAction{}
        , _listCommandHistory{}
        , _elapsedSeconds{ 0.0f }
    {
    }

    void InputActionBuffer::clear()
    {
        _elapsedSeconds = 0.0f;
        _listBufferedAction.clear();
        _listCommandHistory.clear();
    }

    void InputActionBuffer::update( float32 deltaSeconds )
    {
        _elapsedSeconds += deltaSeconds;
        for ( BufferedActionItem& item : _listBufferedAction )
        {
            item._remainingTime -= deltaSeconds;
        }
        while ( _listBufferedAction.empty() == false && _listBufferedAction.front()._remainingTime <= 0.0f )
        {
            _listBufferedAction.popFront();
        }
        while ( _listCommandHistory.empty() == false && ( _elapsedSeconds - _listCommandHistory.front()._timestamp ) > kHistorySeconds )
        {
            _listCommandHistory.popFront();
        }
    }

    void InputActionBuffer::recordTriggered( const hashed_string& action )
    {
        CommandHistoryEntry entry;
        entry._action    = action;
        entry._timestamp = _elapsedSeconds;
        _listCommandHistory.push( entry );
    }

    void InputActionBuffer::bufferAction( const hashed_string& action, float32 expirationSeconds )
    {
        if ( action.empty() )
            return;

        BufferedActionItem item;
        item._action        = action;
        item._remainingTime = expirationSeconds;
        _listBufferedAction.push( item );
    }

    bool InputActionBuffer::consumeBufferedAction( const hashed_string& action )
    {
        for ( BufferedActionItem& item : _listBufferedAction )
        {
            if ( item._action == action && item._remainingTime > 0.0f )
            {
                item._remainingTime = 0.0f;
                return true;
            }
        }
        return false;
    }

    bool InputActionBuffer::wasCommandSequenceTriggered( const vector<hashed_string>& listSequence, float32 maxWindowSeconds ) const
    {
        if ( listSequence.empty() || _listCommandHistory.size() < listSequence.size() )
            return false;

        const size_t seqCount = listSequence.size();
        size_t       matchIdx = seqCount;
        float32      lastTime = 0.0f;

        for ( int32 index = static_cast<int32>( _listCommandHistory.size() ) - 1; index >= 0; --index )
        {
            const CommandHistoryEntry& historyEntry = _listCommandHistory[static_cast<uint32>( index )];

            if ( matchIdx == seqCount )
            {
                if ( historyEntry._action == listSequence[seqCount - 1] )
                {
                    lastTime = historyEntry._timestamp;
                    --matchIdx;
                    if ( matchIdx == 0 )
                        return true;
                }
            }
            else
            {
                if ( ( lastTime - historyEntry._timestamp ) > maxWindowSeconds )
                    return false;

                if ( historyEntry._action == listSequence[matchIdx - 1] )
                {
                    --matchIdx;
                    if ( matchIdx == 0 )
                        return true;
                }
            }
        }
        return matchIdx == 0;
    }

    bool InputActionBuffer::wasCommandPatternTriggered( const hashed_string& pattern, float32 maxWindowSeconds ) const
    {
        string_view patternText = pattern.view();
        if ( patternText.empty() || _listCommandHistory.empty() )
            return false;

        vector<hashed_string> listExpected;
        string                actionToken;

        for ( size_t index = 0; index < patternText.size(); ++index )
        {
            const utf8 character = patternText[index];
            if ( character == '2' )
                listExpected.push_back( "Down" );
            else if ( character == '3' )
                listExpected.push_back( "DownRight" );
            else if ( character == '6' )
                listExpected.push_back( "Right" );
            else if ( character == '4' )
                listExpected.push_back( "Left" );
            else if ( character == '1' )
                listExpected.push_back( "DownLeft" );
            else if ( character == '7' )
                listExpected.push_back( "UpLeft" );
            else if ( character == '8' )
                listExpected.push_back( "Up" );
            else if ( character == '9' )
                listExpected.push_back( "UpRight" );
            else
                actionToken.push_back( character );
        }

        if ( actionToken.empty() == false )
            listExpected.push_back( hashed_string( actionToken ) );

        if ( listExpected.empty() )
            return false;

        return wasCommandSequenceTriggered( listExpected, maxWindowSeconds );
    }
} // namespace sw
