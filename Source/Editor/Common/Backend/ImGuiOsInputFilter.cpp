#include "pch.h"

#include "Editor/Common/Backend/ImGuiOsInputFilter.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    ScopedImGuiOsInputFilter::ScopedImGuiOsInputFilter( bool bActive )
        : _firstIndex{ -1 }
    {
        const ImGuiContext* pContext = ImGui::GetCurrentContext();
        if ( bActive && pContext != nullptr )
            _firstIndex = pContext->InputEventsQueue.Size;
    }

    ScopedImGuiOsInputFilter::~ScopedImGuiOsInputFilter()
    {
        ImGuiContext* pContext = ImGui::GetCurrentContext();
        if ( _firstIndex < 0 || pContext == nullptr )
            return;
        ImVector<ImGuiInputEvent>& queue      = pContext->InputEventsQueue;
        int32                      writeIndex = _firstIndex;
        for ( int32 readIndex = _firstIndex; readIndex < queue.Size; ++readIndex )
        {
            if ( queue[readIndex].Type == ImGuiInputEventType_Focus )
                queue[writeIndex++] = queue[readIndex];
        }
        if ( writeIndex < queue.Size )
            queue.resize( writeIndex );
    }
} // namespace sw::editor
