#include "pch.h"

#include "Engine/Input/ActionMap.h"

/**
 * @file ActionMapCombo.cpp
 * @brief 선입력 버퍼링과 격투 게임식 커맨드 시퀀스/패턴 판정을 담당합니다.
 *
 * 초심자 가이드:
 *  - bufferAction/consumeBufferedAction : 공격 버튼을 살짝 일찍 눌러도 인정해주는 선입력 유예 링버퍼입니다.
 *  - checkCommandSequence : 최근 트리거된 액션 이력(_arrCommandHistory)에서 주어진 순서가 시간 윈도우 안에 나왔는지 검사합니다.
 *  - checkCommandPattern : "236P"(하-우하-우+펀치) 같은 넘패드 표기법 문자열을 액션 이름 시퀀스로 바꾼 뒤 checkCommandSequence에 위임합니다.
 */

namespace sw
{
    void ActionMap::bufferAction( string_view action, float32 expirationSeconds )
    {
        bufferAction( hashed_string( action ), expirationSeconds );
    }

    void ActionMap::bufferAction( const hashed_string& action, float32 expirationSeconds )
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

    bool ActionMap::consumeBufferedAction( string_view action )
    {
        return consumeBufferedAction( hashed_string( action ) );
    }

    bool ActionMap::consumeBufferedAction( const hashed_string& action )
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

    bool ActionMap::checkCommandSequence( const vector<hashed_string>& listSequence, float32 maxWindowSeconds ) const
    {
        if ( listSequence.empty() || _commandHistoryCount < listSequence.size() )
            return false;

        const size_t seqCount = listSequence.size();
        size_t       matchIdx = seqCount;
        float32      lastTime = 0.0f;

        for ( int32 index = static_cast<int32>( _commandHistoryCount ) - 1; index >= 0; --index )
        {
            const uint32               histIdx = ( _commandHistoryHead + static_cast<uint32>( index ) ) % kMaxCommandHistory;
            const CommandHistoryEntry& hist    = _arrCommandHistory[histIdx];

            if ( matchIdx == seqCount )
            {
                if ( hist._action == listSequence[seqCount - 1] )
                {
                    lastTime = hist._timestamp;
                    --matchIdx;
                    if ( matchIdx == 0 )
                        return true;
                }
            }
            else
            {
                if ( ( lastTime - hist._timestamp ) > maxWindowSeconds )
                    return false;

                if ( hist._action == listSequence[matchIdx - 1] )
                {
                    --matchIdx;
                    if ( matchIdx == 0 )
                        return true;
                }
            }
        }
        return matchIdx == 0;
    }

    bool ActionMap::checkCommandSequence( const vector<string>& listSequence, float32 maxWindowSeconds ) const
    {
        if ( listSequence.empty() )
            return false;
        vector<hashed_string> listHashed;
        listHashed.reserve( listSequence.size() );
        for ( const string& s : listSequence )
            listHashed.push_back( hashed_string( s ) );
        return checkCommandSequence( listHashed, maxWindowSeconds );
    }

    bool ActionMap::checkCommandPattern( string_view pattern, float32 maxWindowSeconds ) const
    {
        return checkCommandPattern( hashed_string( pattern ), maxWindowSeconds );
    }

    bool ActionMap::checkCommandPattern( const hashed_string& pattern, float32 maxWindowSeconds ) const
    {
        string_view sv = pattern.view();
        if ( sv.empty() || _commandHistoryCount == 0 )
            return false;

        vector<hashed_string> listExpected;
        string                actionToken;

        for ( size_t index = 0; index < sv.size(); ++index )
        {
            const utf8 ch = sv[index];
            if ( ch == '2' )
                listExpected.push_back( hashed_string( "Down" ) );
            else if ( ch == '3' )
                listExpected.push_back( hashed_string( "DownRight" ) );
            else if ( ch == '6' )
                listExpected.push_back( hashed_string( "Right" ) );
            else if ( ch == '4' )
                listExpected.push_back( hashed_string( "Left" ) );
            else if ( ch == '1' )
                listExpected.push_back( hashed_string( "DownLeft" ) );
            else if ( ch == '7' )
                listExpected.push_back( hashed_string( "UpLeft" ) );
            else if ( ch == '8' )
                listExpected.push_back( hashed_string( "Up" ) );
            else if ( ch == '9' )
                listExpected.push_back( hashed_string( "UpRight" ) );
            else
                actionToken.push_back( ch );
        }

        if ( actionToken.empty() == false )
            listExpected.push_back( hashed_string( actionToken ) );

        if ( listExpected.empty() )
            return false;

        return checkCommandSequence( listExpected, maxWindowSeconds );
    }
} // namespace sw
