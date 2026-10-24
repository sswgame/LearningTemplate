#include "pch.h"

#include "Editor/Common/GUI/EditorUIContext.h"

#include <imgui.h>
#include <implot.h>

namespace sw::editor
{
    void EditorUIContext::publish( const EditorUIContextState& state )
    {
        using BinderRegistry = EditorExtensionRegistry<EditorUIBinderRegistration>;
        for ( uint32 index = 0; index < BinderRegistry::getCount(); ++index )
        {
            const EditorUIBinderRegistration& registration = BinderRegistry::getAt( index );
            if ( registration._pfnBind != nullptr )
                registration._pfnBind( state );
        }
    }

    EditorUIContextState EditorUIContext::getCurrent()
    {
        // 컨텍스트의 주인은 ImGui 자신이다(이 모듈의 사본). 같은 값을 따로 들고 있으면 두 곳이 어긋날 수 있어 묻는다.
        return EditorUIContextState{ ImGui::GetCurrentContext(), ImPlot::GetCurrentContext() };
    }
} // namespace sw::editor
