/**
 * @file EditorInputActions.cpp
 * @brief 에디터 입력 맵의 소유 · 포커스 레이어 · 액션 → 명령 실행입니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputActions.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Input/InputManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    EditorInputActions::EditorInputActions()
        : _map{}
        , _focusedPanelLayer{}
    {
    }

    EditorInputActions::~EditorInputActions() = default;

    span<const utf8* const> EditorInputActions::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorCommandRegistry::kSubsystemName, EditorPanelManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult EditorInputActions::initializeSubsystem()
    {
        // 엔진 입력 매니저는 클라이언트 호스트에 늘 있다(필수) — 에디터 키도 같은 장치 · 같은 맵 규칙으로 판정한다.
        _map.setInputManager( &editor::getSubsystem<InputManager>() );
        // 게임 뷰가 포커스여도(F8 플레이 빠져나오기 · F9 스크린샷) 에디터 도구 창이 포커스여도 키를 본다. 셸 · UI 맵은 도구 포커스 동안 키를 보지 않는다.
        _map.setKeyboardFocusIgnored( true );
        _map.setToolKeyboardReader( true );
        const string& mapPath = getEditorToolDefaults()._editorInputMap.get();
        if ( _map.loadFromResource( mapPath ) == false )
        {
            SW_LOG_ERROR( "EditorInputActions: the editor input map '%#' could not be read - the editor has no shortcuts", mapPath.c_str() );
            _map.setInputManager( nullptr );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void EditorInputActions::shutdownSubsystem()
    {
        // 입력 매니저는 에디터보다 오래 산다 — 맵이 그것을 가리킨 채 남지 않게 끊는다(끊긴 맵의 `update` 는 아무것도 하지 않는다).
        _map.setInputManager( nullptr );
    }

    void EditorInputActions::processFrame( float32 deltaSeconds )
    {
        syncFocusedPanelLayer();
        _map.update( deltaSeconds );
    }

    void EditorInputActions::syncFocusedPanelLayer()
    {
        const EditorPanelManager& panelManager = editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>();
        hashed_string             focusedLayer{};
        for ( const EditorPanelEntry& entry : panelManager.getPanels() )
        {
            const bool bFocused = entry._pInstance != nullptr && entry._pInstance->isOpen() && entry._pInstance->isWindowFocused();
            if ( bFocused == false )
                continue;
            const hashed_string layer( ( string( kPanelLayerPrefix ) + entry._id ).c_str() );
            if ( _map.hasLayer( layer ) )
                focusedLayer = layer;
            break;
        }
        if ( focusedLayer == _focusedPanelLayer )
            return;
        if ( _focusedPanelLayer.empty() == false )
            _map.setLayerEnabled( _focusedPanelLayer, false );
        if ( focusedLayer.empty() == false )
            _map.setLayerEnabled( focusedLayer, true );
        _focusedPanelLayer = focusedLayer;
    }
} // namespace sw::editor
