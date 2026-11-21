#include "pch.h"

#include "Editor/Common/Widgets/EditorVirtualList.h"

#include <imgui.h>

namespace sw::editor
{
    EditorVirtualList::EditorVirtualList( int32 rowCount, float32 rowHeight )
        : _pClipper{ make_unique<ImGuiListClipper>() }
        , _rowCount{ rowCount }
        , _begin{ 0 }
        , _end{ 0 }
    {
        _pClipper->Begin( rowCount, rowHeight > 0.0f ? rowHeight : -1.0f );
    }

    EditorVirtualList::~EditorVirtualList()
    {
        _pClipper->End();
    }

    void EditorVirtualList::includeItemByIndex( int32 rowIndex )
    {
        if ( 0 <= rowIndex && rowIndex < _rowCount )
            _pClipper->IncludeItemByIndex( rowIndex );
    }

    bool EditorVirtualList::step()
    {
        if ( _pClipper->Step() == false )
            return false;
        _begin = _pClipper->DisplayStart;
        _end   = _pClipper->DisplayEnd;
        return true;
    }
} // namespace sw::editor
