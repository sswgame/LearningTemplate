#include "pch.h"

#include "Editor/Common/Workspace/EditorModuleUnloadListener.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"

namespace sw::editor
{
    EditorModuleUnloadListener::EditorModuleUnloadListener( EditorContext& context )
        : _context{ context }
    {
    }

    EditorModuleUnloadListener::~EditorModuleUnloadListener() = default;

    uint32 EditorModuleUnloadListener::onModuleUnloading( const void* pBegin, const void* pEnd, bool& /*outKeepImageMapped*/ )
    {
        // 확장 모듈의 환경설정 섹션 값은 그 이미지의 함수 정적이다. 환경설정 창은 고친 뒤 잠시 기다렸다 저장하므로, 그 사이에 내려가면 고친 값이 사라진다.
        // 그 이미지가 섹션을 가지면 내리기 전에 저장한다(등록 줄은 이미지의 정적 소멸 때 빠지므로 지금은 아직 목록에 있다).
        if ( EditorPreferencesStore::hasSectionWithin( pBegin, pEnd ) )
            (void)EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ); // 실패는 saveAll 이 경고로 알린다
        uint32 releasedCount = _context.getPanelManager().releasePanelsWithin( pBegin, pEnd, _context.getRHIDevice() );
        releasedCount += _context.getPopupManager().releasePopupsWithin( pBegin, pEnd );
        releasedCount += _context.getInspectorComponentManager().releaseInspectorsWithin( pBegin, pEnd );
        releasedCount += _context.getInspectorPropertyManager().releaseDrawersWithin( pBegin, pEnd );
        releasedCount += EditorCommandGUI::releaseCommandsWithin( pBegin, pEnd );
        // 기본값 인스턴스는 어느 모듈의 컴포넌트든 될 수 있다 — 무엇이 언로드되든 모두 지운다(다음에 물을 때 다시 만든다). 세지 않는다(떼는 등록이 아니다).
        _context.getDefaultObjects().clear();
        return releasedCount;
    }
} // namespace sw::editor
