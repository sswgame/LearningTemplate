#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/AI/Model/BehaviorTreeLiveRunner.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/AI/IBehaviorTreeDebugSource.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw::editor
{
    const BehaviorTreeRunner* BehaviorTreeLiveRunner::findForFocusedSelection( string_view treePath, string& outName )
    {
        outName.clear();
        const EditorSelection* pSelection = editor::getFocusedSelection();
        if ( pSelection == nullptr )
            return nullptr;
        vector<GameObject*> listSelected;
        pSelection->getSelectedObjects( listSelected );
        vector<BehaviorTreeDebugRunner> listDebugRunner;
        for ( GameObject* pObject : listSelected )
        {
            if ( pObject == nullptr )
                continue;
            AIControllerComponent*    pController = pObject->getComponent<AIControllerComponent>();
            const BehaviorTreeRunner* pRunner     = pController != nullptr ? pController->getBehaviorTreeRunner() : nullptr;
            if ( pRunner != nullptr )
                return pRunner;

            // 컴포넌트에 안 붙은 실행기(지휘관 AI 등) — 노드 번호는 깊이 우선 번호라 창에 연 파일과 같은 트리 파일을 쥔 실행기만 맞다.
            listDebugRunner.clear();
            pObject->forEachComponentOfType<GameModeComponent>(
                [&listDebugRunner]( GameModeComponent* pGameMode )
            {
                const IBehaviorTreeDebugSource* pSource = pGameMode->findBehaviorTreeDebugSource();
                if ( pSource != nullptr )
                    pSource->collectBehaviorTreeRunners( listDebugRunner );
            } );
            for ( const BehaviorTreeDebugRunner& entry : listDebugRunner )
            {
                if ( entry._pRunner != nullptr && string_view( entry._treePath ) == treePath )
                {
                    outName = entry._name;
                    return entry._pRunner;
                }
            }
        }
        return nullptr;
    }
} // namespace sw::editor
