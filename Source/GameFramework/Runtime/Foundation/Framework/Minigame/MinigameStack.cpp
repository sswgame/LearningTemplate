#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
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
