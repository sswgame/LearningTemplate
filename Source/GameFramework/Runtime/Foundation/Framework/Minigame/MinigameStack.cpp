#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
    void MinigameFrame::takeMatchResult( MinigameResult& inoutResult ) const
    {
        if ( _matchID == 0 )
            return;
        inoutResult._matchID    = _matchID;
        inoutResult._matchBoard = _definition._matchBoard;
        for ( size_t index = 0; index < inoutResult._listValue.size(); ++index )
        {
            if ( inoutResult._listValue[index]._name != _definition._matchScore )
                continue;
            inoutResult._matchScore = inoutResult._listValue[index]._amount;
            inoutResult._listValue.erase( inoutResult._listValue.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    void MinigameStack::push( MinigameFrame frame )
    {
        _listFrame.push_back( std::move( frame ) );
    }

    bool MinigameStack::pop( MinigameFrame& outFrame )
    {
        if ( _listFrame.empty() )
            return false;
        outFrame = std::move( _listFrame.back() );
        _listFrame.pop_back();
        return true;
    }

    uint64 MinigameStack::findMatchID() const
    {
        for ( const MinigameFrame& frame : _listFrame )
        {
            if ( frame._matchID != 0 )
                return frame._matchID;
        }
        return 0;
    }

    bool MinigameStack::hasSuspendedScene() const
    {
        for ( const MinigameFrame& frame : _listFrame )
        {
            if ( std::holds_alternative<MinigameSuspendedScene>( frame._returnPoint ) )
                return true;
        }
        return false;
    }
} // namespace sw
