#include "pch.h"

#include "Editor/Common/GUI/EditorColorUtil.h"

#include <imgui.h>

namespace sw::editor
{
    ImVec4 EditorColorUtil::toImVec4( const Color& color )
    {
        return ImVec4{ color._r, color._g, color._b, color._a };
    }

    uint32 EditorColorUtil::toImU32( const Color& color )
    {
        return color.toAbgr();
    }

    Color EditorColorUtil::fromImVec4( const ImVec4& value )
    {
        return Color{ value.x, value.y, value.z, value.w };
    }
} // namespace sw::editor
