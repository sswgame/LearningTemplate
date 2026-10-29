#include "pch.h"

#include "Editor/EditorModuleUnloadListener.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

namespace sw::editor
{
    span<const utf8* const> EditorModuleUnloadListener::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorPanelManager::kSubsystemName, EditorPopupManager::kSubsystemName, InspectorComponentManager::kSubsystemName, InspectorPropertyManager::kSubsystemName, EditorDefaultObjects::kSubsystemName, EditorExtensionSubsystems::kSubsystemName };
        return kArrDependency;
    }

    EditorModuleUnloadListener::EditorModuleUnloadListener( EditorContext& context )
        : _context{ context }
    {
    }

    EditorModuleUnloadListener::~EditorModuleUnloadListener() = default;

    uint32 EditorModuleUnloadListener::onModuleUnloading( const void* pBegin, const void* pEnd )
    {
        // 확장 모듈의 환경설정 섹션 값은 그 이미지의 소유자(`EditorSettingsRegistrar`) 멤버다. 환경설정 창은 고친 뒤 잠시 기다렸다 저장하므로, 그 사이에 언로드되면 고친 값이 사라진다.
        // 그 이미지가 섹션을 가지면 내리기 전에 저장한다(등록 줄은 이미지의 정적 소멸 때 빠지므로 지금은 아직 목록에 있다).
        // 다시 올라오면 새 이미지가 항목을 새로 만들어 '파일 값을 입힘' 표시도 거짓으로 돌아가므로 다음 프레임(또는 다음 saveAll)에 파일에서 다시 읽는다 — 따로 잊게 하지 않는다.
        if ( EditorPreferencesStore::hasSectionWithin( pBegin, pEnd ) )
            (void)EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ); // 실패는 saveAll 이 경고로 알린다
        uint32 releasedCount = _context.getSubsystem<EditorPanelManager>().releasePanelsWithin( pBegin, pEnd, _context.getSubsystem<EditorRenderViews>().getRHIDevice() );
        releasedCount += _context.getSubsystem<EditorPopupManager>().releasePopupsWithin( pBegin, pEnd );
        releasedCount += _context.getSubsystem<InspectorComponentManager>().releaseInspectorsWithin( pBegin, pEnd );
        releasedCount += _context.getSubsystem<InspectorPropertyManager>().releaseDrawersWithin( pBegin, pEnd );
        releasedCount += EditorCommandGUI::releaseCommandsWithin( pBegin, pEnd );
        // 확장이 기여한 서브시스템은 그 모듈의 패널 · 명령이 쓴다 — 그것들을 종료한 뒤에 종료한다.
        releasedCount += _context.getSubsystem<EditorExtensionSubsystems>().releaseSubsystemsWithin( pBegin, pEnd );
        // 기본값 인스턴스는 어느 모듈의 컴포넌트든 될 수 있다 — 무엇이 언로드되든 모두 지운다(다음에 물을 때 다시 만든다). 세지 않는다(해제하는 등록이 아니다).
        _context.getSubsystem<EditorDefaultObjects>().clear();
        return releasedCount;
    }
} // namespace sw::editor
