#include "pch.h"

#include "DevTools/Client/Render/MeshLODForce.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    /** @brief `-gv_meshLodForce=N` — 메시 LOD 를 화면 크기와 상관없이 단계 N 으로 그립니다(-1 이면 화면 크기로 고른다). 활성 씬에만 건다(에디터 미리보기 씬은 자기 고정을 쓴다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_meshLodForce, MeshLODSystem::kPickByScreenSize, "Draw every LOD mesh of the active scene at this LOD level regardless of screen size (-1 = pick by screen size)" );

    void MeshLODForce::apply()
    {
        // 건 고정은 활성 씬의 LOD 시스템이 든다 — 그 칸을 거는 곳은 이 스위치뿐이다(에디터 미리보기는 자기 씬에 건다). 그래서 "지난 프레임에 걸었나" 를 따로 두지 않고
        // -1 로 돌아오면 걸린 고정을 풀기만 한다(언리얼 r.ForceLOD 도 콘솔 변수 값 하나다).
        const int32              forcedLOD     = gv_meshLodForce;
        SceneManager* const      pSceneManager = engine::findSubsystem<SceneManager>();
        Scene* const             pActiveScene  = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* const pManager      = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        MeshLODSystem& lodSystem = pManager->getSubsystem<MeshLODSystem>();
        const int32    wantedLOD = forcedLOD < 0 ? MeshLODSystem::kPickByScreenSize : forcedLOD;
        if ( lodSystem.getForcedLOD() != wantedLOD )
            lodSystem.setForcedLOD( wantedLOD );
    }
} // namespace sw
