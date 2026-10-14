#include "pch.h"

#include "Editor/Viewport/EditorViewportMouseClaim.h"

namespace sw::editor
{
    namespace
    {
        const void* s_pMouseOwner = nullptr;
    } // namespace

    void EditorViewportMouseClaim::acquire( const void* pOwner )
    {
        s_pMouseOwner = pOwner;
    }

    void EditorViewportMouseClaim::release( const void* pOwner )
    {
        if ( s_pMouseOwner == pOwner )
            s_pMouseOwner = nullptr;
    }

    bool EditorViewportMouseClaim::isClaimed()
    {
        return s_pMouseOwner != nullptr;
    }

    bool EditorViewportMouseClaim::isClaimedBy( const void* pOwner )
    {
        return pOwner != nullptr && s_pMouseOwner == pOwner;
    }
} // namespace sw::editor
