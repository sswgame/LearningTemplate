#include "pch.h"

#include "Editor/Common/Workspace/EditorModuleUnloadListener.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Popups/EditorPopupManager.h"

namespace sw::editor
{
    EditorModuleUnloadListener::EditorModuleUnloadListener( EditorContext& context )
        : _context{ context }
    {
    }

    EditorModuleUnloadListener::~EditorModuleUnloadListener() = default;

    uint32 EditorModuleUnloadListener::onModuleUnloading( const void* pBegin, const void* pEnd, bool& /*outKeepImageMapped*/ )
    {
        uint32 releasedCount = _context.getPanelManager().releasePanelsWithin( pBegin, pEnd, _context.getRHIDevice() );
        releasedCount += _context.getPopupManager().releasePopupsWithin( pBegin, pEnd );
        releasedCount += _context.getInspectorComponentManager().releaseInspectorsWithin( pBegin, pEnd );
        return releasedCount;
    }
} // namespace sw::editor
