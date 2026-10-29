#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

// 되돌리기 기록이 리로드 · 백엔드 교체를 넘는지(데이터 단계 + 맡김 칸) 확인한다 — 기록을 바이트로 썼다 새 스택에 읽어 되돌린다.
namespace sw::editor
{
    namespace
    {
        struct EditorUndoSelfTestsInternal
        {
            /**
             * @brief 오브젝트 이름 바꾸기를 기록하고, 메인 스택을 바이트로 써서 새 스택에 읽은 뒤 그 스택으로 되돌리면 이름이 돌아오는지 봅니다.
             * @details 단계가 코드(델리게이트)였다면 새 스택에는 실행할 것이 없다. 데이터 단계라 이름 해시로 처리기를 찾아 같은 오브젝트에 쓴다.
             */
            static EditorSelfTestStep runHistorySurvivesHandoff( EditorSelfTestContext& context )
            {
                Scene* const pScene = editor::getSubsystem<SceneManager>().getActiveScene();
                if ( context.expect( pScene != nullptr && pScene->getObjectManager() != nullptr, "there is no active scene to edit" ) == false )
                    return EditorSelfTestStep::Done;
                GameObjectManager& manager = *pScene->getObjectManager();
                GameObject* const  pObject = manager.createGameObject( hashed_string( "UndoHandoffProbe" ) );
                if ( context.expect( pObject != nullptr, "the probe object could not be created" ) == false )
                    return EditorSelfTestStep::Done;

                const ObjectSnapshot before = EditorTransaction::captureSnapshot( pObject );
                pObject->setName( hashed_string( "UndoHandoffProbeRenamed" ) );
                EditorTransaction::recordModify( pObject, before, EditorTransaction::captureSnapshot( pObject ), "Rename Probe" );

                CommandStack& stack = editor::getSubsystem<CommandStack>();
                Archive       written;
                stack.save( written );
                Archive      reread( written.getData(), written.getSize() );
                CommandStack resumed;
                (void)context.expect( resumed.load( reread ), "the saved undo history could not be read back" );
                (void)context.expect( resumed.getCommandCount() == stack.getCommandCount(), "the resumed history has a different number of commands" );
                resumed.undo();
                (void)context.expect( pObject->getName() == hashed_string( "UndoHandoffProbe" ), "undo from the resumed history did not restore the name" );
                resumed.redo();
                (void)context.expect( pObject->getName() == hashed_string( "UndoHandoffProbeRenamed" ), "redo from the resumed history did not apply the name" );

                // 호스트가 맡김 칸을 모듈에 넘겼는지 — 없으면 리로드 사이에 기록이 사라진다(모듈 서비스 범위는 고른 것만 건다).
                (void)context.expect( editor::findSubsystem<IModuleStateStore>() != nullptr, "the host state store is not reachable from the editor module" );

                stack.clear();
                manager.destroyObject( pObject );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( UndoHandoff, "undo.historySurvivesHandoff", 1500, &EditorUndoSelfTestsInternal::runHistorySurvivesHandoff );
} // namespace sw::editor
