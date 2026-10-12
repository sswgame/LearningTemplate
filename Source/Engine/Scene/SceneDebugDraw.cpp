#include "pch.h"

#include "Engine/Scene/SceneDebugDraw.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    /** @brief 활성 씬의 강체 물리(바디 셰이프 · 캐릭터 캡슐)를 디버그 선으로 그립니다(`ScenePhysics::drawDebug` → `DebugDrawQueue`). */
    SW_GLOBAL_VARIABLE( bool, gv_physicsDebugDraw, false, "강체 물리 바디 · 캐릭터를 디버그 선으로 그린다" );
    SW_GLOBAL_VARIABLE( int32, gv_navDebugDraw, 0, "내비메시 디버그 — 선(편집기 뷰포트): 1 폴리곤 테두리 · 2 에이전트 경로 · 4 에이전트 속도 · 8 장애물, 16 걷는 면 · 경로를 게임 화면의 메시로 (31 = 모두, 0 = 끔)" );
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @class SceneDebugDrawLineAdapter
         * @brief 물리 · 내비 디버그 선(`IPhysicsDebugRenderer`)을 디버그 드로우 큐로 옮깁니다.
         */
        class SceneDebugDrawLineAdapter final : public IPhysicsDebugRenderer
        {
        public:
            explicit SceneDebugDrawLineAdapter( DebugDrawQueue& queue )
                : _queue{ queue }
            {
            }

            void drawLine( const float3& from, const float3& to, const float4& color ) override { _queue.drawLine( from, to, color ); }

        private:
            DebugDrawQueue& _queue;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SceneDebugDraw::update( Scene* pScene, DebugDrawQueue& queue )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObjectManager& objectManager = *pScene->getObjectManager();

        if ( gv_physicsDebugDraw )
        {
            SceneDebugDrawLineAdapter adapter{ queue };
            objectManager.getSceneService<ScenePhysics>().drawDebug( adapter );
        }

        // 내비메시 · 에이전트 경로 · 속도 · 장애물 — 선은 물리 디버그와 같은 출구로(편집기 뷰포트가 그린다), 비트 16 은 게임 화면에 보이는 메시로.
        SceneNavigation& navigation = objectManager.getSceneService<SceneNavigation>();
        navigation.updateDebugView( static_cast<uint32>( gv_navDebugDraw ) );
        if ( gv_navDebugDraw != 0 )
        {
            SceneDebugDrawLineAdapter adapter{ queue };
            navigation.drawDebug( adapter, static_cast<uint32>( gv_navDebugDraw ) );
        }
    }
} // namespace sw
