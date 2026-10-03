#include "pch.h"

#include "GameFramework/Control/InputCommandBuffer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct InputCommandInternal
        {
            /** @brief 커맨드가 적은 방향 · 버튼의 수입니다(같은 길이의 커맨드끼리 더 구체적인 쪽을 고른다). */
            static int32 computeSpecificity( const InputCommand& command )
            {
                int32 count = 0;
                for ( const InputCommandStep& step : command._listStep )
                {
                    count += step._direction != 0 ? 1 : 0;
                    for ( uint16 bits = step._buttons; bits != 0; bits = static_cast<uint16>( bits & ( bits - 1 ) ) )
                        ++count;
                }
                return count;
            }

            /** @brief 방향 낱말 → 넘패드(앞 = 6). 모르면 0 입니다. */
            static uint8 parseDirection( string_view word, bool& outHold )
            {
                outHold = word.empty() == false && word[0] >= 'A' && word[0] <= 'Z';
                string lower( word );
                for ( utf8& letter : lower )
                    letter = static_cast<utf8>( letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter );
                if ( lower == "f" )
                    return 6;
                if ( lower == "b" )
                    return 4;
                if ( lower == "u" )
                    return 8;
                if ( lower == "d" )
                    return 2;
                if ( lower == "n" )
                    return 5;
                if ( lower == "u/f" )
                    return 9;
                if ( lower == "d/f" )
                    return 3;
                if ( lower == "u/b" )
                    return 7;
                if ( lower == "d/b" )
                    return 1;
                return 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // InputCommandParser
    // ------------------------------------------------------------------------------
    InputCommandParser::InputCommandParser()
        : _listButtonName{}
    {
        setButtonNames( "1,2,3,4" );
    }

    void InputCommandParser::setButtonNames( string_view names )
    {
        _listButtonName.clear();
        GameDataXml::forEachToken( names, ", ", [&]( string_view token )
        { _listButtonName.push_back( string( token ) ); } );
    }

    bool InputCommandParser::parseStep( string_view token, vector<InputCommandStep>& outListStep ) const
    {
        // 줄임 낱말 — 여러 단계로 펼치고, 붙은 버튼은 마지막 단계에.
        const size_t      plus = token.find( '+' );
        const string_view head = plus == string_view::npos ? token : token.substr( 0, plus );
        const string_view rest = plus == string_view::npos ? string_view() : token.substr( plus + 1 );
        InputCommandStep  step;
        bool              bKnown = true;
        if ( head == "qcf" || head == "qcb" || head == "dp" )
        {
            const uint8 arrMotion[3][3] = {
                {2, 3, 6},
                {2, 1, 4},
                {6, 2, 3}
            };
            const int32 motion = head == "qcf" ? 0 : ( head == "qcb" ? 1 : 2 );
            for ( int32 index = 0; index < 2; ++index )
            {
                InputCommandStep motionStep;
                motionStep._direction = arrMotion[motion][index];
                outListStep.push_back( motionStep );
            }
            step._direction = arrMotion[motion][2];
        }
        else if ( head.empty() == false )
        {
            bool        bHold     = false;
            const uint8 direction = InputCommandInternal::parseDirection( head, bHold );
            if ( direction == 0 )
            {
                // 방향이 아니면 버튼일 수 있다("1+2").
                bKnown = false;
                for ( size_t index = 0; index < _listButtonName.size(); ++index )
                {
                    if ( string_view( _listButtonName[index] ) == head )
                    {
                        step._buttons = static_cast<uint16>( step._buttons | ( 1u << index ) );
                        bKnown        = true;
                    }
                }
            }
            else
            {
                step._direction = direction;
                step._bHold     = bHold ? SW_TRUE : SW_FALSE;
            }
        }
        if ( bKnown == false )
            return false;
        bool bAllButtons = true;
        GameDataXml::forEachToken( rest, "+", [&]( string_view name )
        {
            bool bFound = false;
            for ( size_t index = 0; index < _listButtonName.size(); ++index )
            {
                if ( string_view( _listButtonName[index] ) == name )
                {
                    step._buttons = static_cast<uint16>( step._buttons | ( 1u << index ) );
                    bFound        = true;
                }
            }
            bAllButtons = bAllButtons && bFound;
        } );
        if ( bAllButtons == false )
            return false;
        outListStep.push_back( step );
        return true;
    }

    bool InputCommandParser::parse( string_view notation, InputCommand& outCommand ) const
    {
        outCommand._listStep.clear();
        bool bOk = true;
        GameDataXml::forEachToken( notation, ", ", [&]( string_view token )
        { bOk = bOk && parseStep( token, outCommand._listStep ); } );
        return bOk && outCommand._listStep.empty() == false;
    }

    // ------------------------------------------------------------------------------
    // InputCommandBuffer
    // ------------------------------------------------------------------------------
    InputCommandBuffer::InputCommandBuffer( int32 capacity )
        : _listFrame{}
        , _capacity{ MathUtil::max( 4, capacity ) }
        , _head{ 0 }
        , _count{ 0 }
    {
        _listFrame.assign( static_cast<size_t>( _capacity ), InputFrame{} );
    }

    void InputCommandBuffer::push( const InputFrame& frame )
    {
        _listFrame[static_cast<size_t>( _head )] = frame;
        _head                                    = ( _head + 1 ) % _capacity;
        _count                                   = MathUtil::min( _count + 1, _capacity );
    }

    void InputCommandBuffer::writeState( BitWriter& outWriter ) const
    {
        outWriter.writeInt( _count, 0, _capacity );
        for ( int32 framesAgo = _count - 1; framesAgo >= 0; --framesAgo )
        {
            const InputFrame& frame = getFrame( framesAgo );
            outWriter.writeBits( frame._buttons, 16 );
            outWriter.writeBits( frame._direction, 4 );
        }
    }

    bool InputCommandBuffer::readState( BitReader& reader )
    {
        const int32 count = reader.readInt( 0, _capacity );
        if ( reader.hasOverflowed() )
            return false;
        vector<InputFrame> listFrame( static_cast<size_t>( count ) );
        for ( InputFrame& frame : listFrame )
        {
            frame._buttons   = static_cast<uint16>( reader.readBits( 16 ) );
            frame._direction = static_cast<uint8>( reader.readBits( 4 ) );
            if ( frame._direction > 9 )
                return false;
        }
        if ( reader.hasOverflowed() )
            return false;
        clear();
        for ( const InputFrame& frame : listFrame )
            push( frame );
        return true;
    }

    void InputCommandBuffer::clear()
    {
        _head  = 0;
        _count = 0;
    }

    const InputFrame& InputCommandBuffer::getFrame( int32 framesAgo ) const
    {
        static const InputFrame kNeutralFrame{};
        if ( framesAgo < 0 || framesAgo >= _count )
            return kNeutralFrame;
        const int32 index = ( _head - 1 - framesAgo + _capacity * 2 ) % _capacity;
        return _listFrame[static_cast<size_t>( index )];
    }

    uint8 InputCommandBuffer::mirrorDirection( uint8 direction, int32 facing )
    {
        if ( facing >= 0 || direction == 0 )
            return direction;
        // 좌우만 뒤집는다: 1↔3, 4↔6, 7↔9.
        const int32 column = ( direction - 1 ) % 3;
        return static_cast<uint8>( direction - column + ( 2 - column ) );
    }

    bool InputCommandBuffer::matchesStep( const InputCommandStep& step, int32 framesAgo, int32 facing ) const
    {
        const InputFrame& frame    = getFrame( framesAgo );
        const InputFrame& previous = getFrame( framesAgo + 1 );
        if ( step._direction != 0 )
        {
            const uint8 wanted = mirrorDirection( step._direction, facing );
            if ( frame._direction != wanted )
                return false;
            // 버튼과 함께면 누르고 있기만 해도 된다("f+2"). 방향만이면 새로 넣어야 한다("f,f").
            if ( step._buttons == 0 && step._bHold == SW_FALSE && previous._direction == wanted )
                return false;
        }
        if ( step._buttons != 0 )
        {
            if ( ( frame._buttons & step._buttons ) != step._buttons )
                return false;
            // 동시 — 이 틱과 바로 앞 몇 틱에 새로 눌린 것을 모아 모두 덮어야 한다. 이 틱에 적어도 하나는 새로.
            uint16 pressed = 0;
            for ( int32 offset = 0; offset < kSimultaneousFrames; ++offset )
            {
                const uint16 now    = getFrame( framesAgo + offset )._buttons;
                const uint16 before = getFrame( framesAgo + offset + 1 )._buttons;
                pressed             = static_cast<uint16>( pressed | ( now & ~before ) );
            }
            const uint16 pressedNow = static_cast<uint16>( frame._buttons & ~previous._buttons );
            if ( ( pressed & step._buttons ) != step._buttons || ( pressedNow & step._buttons ) == 0 )
                return false;
        }
        return true;
    }

    bool InputCommandBuffer::isCompleted( const InputCommand& command, int32 facing ) const
    {
        if ( command._listStep.empty() || _count == 0 )
            return false;
        // 마지막 단계는 이번 틱에, 앞 단계들은 거꾸로 `_maxGap` 안에서.
        int32 framesAgo      = 0;
        int32 laterFrame     = -1;
        uint8 laterDirection = 0;
        for ( size_t stepIndex = command._listStep.size(); stepIndex > 0; --stepIndex )
        {
            const InputCommandStep& step  = command._listStep[stepIndex - 1];
            const bool              bLast = stepIndex == command._listStep.size();
            const int32             limit = bLast ? 0 : command._maxGap;
            // 같은 방향이 이어지면("f,f+2") 그 사이에 한 번은 떼야 한다 — 누르고 있기는 두 번 넣기가 아니다.
            const uint8 direction    = mirrorDirection( step._direction, facing );
            const bool  bNeedRelease = laterFrame >= 0 && direction != 0 && direction == laterDirection && step._bHold == SW_FALSE;
            bool        bReleased    = bNeedRelease == false;
            bool        bFound       = false;
            for ( int32 gap = bLast ? 0 : 1; gap <= limit && framesAgo + gap < _count; ++gap )
            {
                if ( bReleased && matchesStep( step, framesAgo + gap, facing ) )
                {
                    framesAgo += gap;
                    bFound = true;
                    break;
                }
                bReleased = bReleased || getFrame( framesAgo + gap )._direction != direction;
            }
            if ( bFound == false )
                return false;
            laterFrame     = framesAgo;
            laterDirection = direction;
        }
        return true;
    }

    const InputCommand* InputCommandBuffer::findCompleted( const vector<InputCommand>& listCommand, int32 facing ) const
    {
        const InputCommand* pBest = nullptr;
        for ( const InputCommand& command : listCommand )
        {
            if ( isCompleted( command, facing ) == false )
                continue;
            if ( pBest == nullptr || command._priority > pBest->_priority )
            {
                pBest = &command;
                continue;
            }
            if ( command._priority != pBest->_priority )
                continue;
            // 같은 우선도 — 단계가 많은 것, 단계 수도 같으면 방향 · 버튼을 더 많이 적은 것("d/f+1" 이 "1" 을 이긴다).
            const size_t stepCount = command._listStep.size();
            const size_t bestCount = pBest->_listStep.size();
            if ( stepCount > bestCount || ( stepCount == bestCount && InputCommandInternal::computeSpecificity( command ) > InputCommandInternal::computeSpecificity( *pBest ) ) )
                pBest = &command;
        }
        return pBest;
    }
} // namespace sw
