#include "pch.h"

#include "EditorFoundation/EditSessionState.h"

namespace sw
{
    EditSessionState::EditSessionState()
        : _snapshot{}
        , _bStored{ false }
        , _bModuleLibraryKept{ false }
    {
    }

    void EditSessionState::store( Snapshot snapshot )
    {
        _snapshot = std::move( snapshot );
        _bStored  = true;
    }

    bool EditSessionState::take( uint64 sceneGeneration, Snapshot& outSnapshot )
    {
        if ( _bStored == false )
            return false;
        _bStored = false;
        if ( _snapshot._sceneGeneration != sceneGeneration )
            return false;
        outSnapshot = std::move( _snapshot );
        return true;
    }
} // namespace sw
