#include "pch.h"

#include "DevTools/Client/Render/MeshLODForce.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /** @brief `-gv_meshLodForce=N` — 메시 LOD 를 화면 크기와 상관없이 단계 N 으로 그립니다(-1 이면 화면 크기로 고른다). 활성 씬에만 건다(에디터 미리보기 씬은 자기 고정을 쓴다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_meshLodForce, MeshLODSystem::kPickByScreenSize, "Draw every LOD mesh of the active scene at this LOD level regardless of screen size (-1 = pick by screen size)" );

    namespace
    {
        /** @brief 지난 프레임에 고정을 걸었는지입니다 — -1 로 돌아온 첫 프레임에만 풀어, 이 스위치를 쓰지 않는 실행이 다른 쪽의 고정을 건드리지 않게 한다. 게임 스레드만 쓴다. */
        bool s_bForcing = false;
    } // namespace

    void MeshLODForce::apply()
    {
        const int32 forcedLOD = gv_meshLodForce;
        if ( forcedLOD < 0 && s_bForcing == false )
            return;
        SceneManager* const      pSceneManager = engine::findSubsystem<SceneManager>();
        Scene* const             pActiveScene  = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* const pManager      = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        MeshLODSystem& lodSystem = pManager->getSubsystem<MeshLODSystem>();
        if ( forcedLOD < 0 )
        {
            lodSystem.setForcedLOD( MeshLODSystem::kPickByScreenSize );
            s_bForcing = false;
            return;
        }
        if ( lodSystem.getForcedLOD() != forcedLOD )
            lodSystem.setForcedLOD( forcedLOD );
        s_bForcing = true;
    }
} // namespace sw
