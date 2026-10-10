#include "pch.h"

#include "Editor/Common/Workspace/EditorModuleUnloadListener.h"

#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
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
        releasedCount += EditorCommandGUI::releaseCommandsWithin( pBegin, pEnd );
        // 기본값 인스턴스는 어느 모듈의 컴포넌트든 될 수 있다 — 무엇이 언로드되든 모두 지운다(다음에 물을 때 다시 만든다). 세지 않는다(떼는 등록이 아니다).
        _context.getDefaultObjects().clear();
        return releasedCount;
    }
} // namespace sw::editor
