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
        // 포커스 사건도 지운다. 다른 창이 전경을 가져가는 것(사람 · 다른 프로세스)은 장치 입력과 같은 바깥 간섭이다. 남기면 ImGui 가
        // 눌린 키 · 단추를 모두 놓은 것으로 처리해 누르고 있는 자동화 입력(뷰포트 비행)이 끊긴다.
        ImVector<ImGuiInputEvent>& queue = pContext->InputEventsQueue;
        if ( _firstIndex < queue.Size )
            queue.resize( _firstIndex );
    }
} // namespace sw::editor
