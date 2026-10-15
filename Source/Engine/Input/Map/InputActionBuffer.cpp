#include "pch.h"

#include "Engine/Input/Map/InputActionBuffer.h"

namespace sw
{
    InputActionBuffer::InputActionBuffer()
        : _arrBufferedAction{}
        , _arrCommandHistory{}
        , _elapsedSeconds{ 0.0f }
        , _bufferedActionCount{ 0 }
        , _bufferedActionHead{ 0 }
        , _commandHistoryCount{ 0 }
        , _commandHistoryHead{ 0 }
    {
    }

    void InputActionBuffer::clear()
    {
        _elapsedSeconds      = 0.0f;
        _bufferedActionCount = 0;
        _bufferedActionHead  = 0;
        _commandHistoryCount = 0;
        _commandHistoryHead  = 0;
    }

    void InputActionBuffer::update( float32 deltaSeconds )
    {
        _elapsedSeconds += deltaSeconds;
        for ( uint32 index = 0; index < _bufferedActionCount; ++index )
        {
            const uint32 idx = ( _bufferedActionHead + index ) % kMaxBufferedActions;
            _arrBufferedAction[idx]._remainingTime -= deltaSeconds;
        }
        while ( _bufferedActionCount > 0 && _arrBufferedAction[_bufferedActionHead]._remainingTime <= 0.0f )
        {
            _bufferedActionHead = ( _bufferedActionHead + 1 ) % kMaxBufferedActions;
            --_bufferedActionCount;
        }
        while ( _commandHistoryCount > 0 && ( _elapsedSeconds - _arrCommandHistory[_commandHistoryHead]._timestamp ) > kHistorySeconds )
        {
            _commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
            --_commandHistoryCount;
        }
    }

    void InputActionBuffer::recordTriggered( const hashed_string& action )
    {
        const uint32 insertIdx                   = ( _commandHistoryHead + _commandHistoryCount ) % kMaxCommandHistory;
        _arrCommandHistory[insertIdx]._action    = action;
        _arrCommandHistory[insertIdx]._timestamp = _elapsedSeconds;
        if ( _commandHistoryCount < kMaxCommandHistory )
            ++_commandHistoryCount;
        else
            _commandHistoryHead = ( _commandHistoryHead + 1 ) % kMaxCommandHistory;
    }

    void InputActionBuffer::bufferAction( const hashed_string& action, float32 expirationSeconds )
    {
        if ( action.empty() )
            return;

        const uint32 insertIdx                       = ( _bufferedActionHead + _bufferedActionCount ) % kMaxBufferedActions;
        _arrBufferedAction[insertIdx]._action        = action;
        _arrBufferedAction[insertIdx]._remainingTime = expirationSeconds;
        if ( _bufferedActionCount < kMaxBufferedActions )
            ++_bufferedActionCount;
        else
            _bufferedActionHead = ( _bufferedActionHead + 1 ) % kMaxBufferedActions;
    }

    bool InputActionBuffer::consumeBufferedAction( const hashed_string& action )
    {
        for ( uint32 index = 0; index < _bufferedActionCount; ++index )
        {
            const uint32 idx = ( _bufferedActionHead + index ) % kMaxBufferedActions;
            if ( _arrBufferedAction[idx]._action == action && _arrBufferedAction[idx]._remainingTime > 0.0f )
            {
                _arrBufferedAction[idx]._remainingTime = 0.0f;
                return true;
            }
        }
        return false;
    }

    bool InputActionBuffer::wasCommandSequenceTriggered( const vector<hashed_string>& listSequence, float32 maxWindowSeconds ) const
    {
        if ( listSequence.empty() || _commandHistoryCount < listSequence.size() )
            return false;

        const size_t seqCount = listSequence.size();
        size_t       matchIdx = seqCount;
        float32      lastTime = 0.0f;

        for ( int32 index = static_cast<int32>( _commandHistoryCount ) - 1; index >= 0; --index )
        {
            const uint32               historyIndex = ( _commandHistoryHead + static_cast<uint32>( index ) ) % kMaxCommandHistory;
            const CommandHistoryEntry& historyEntry = _arrCommandHistory[historyIndex];

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
        if ( patternText.empty() || _commandHistoryCount == 0 )
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
