#include "pch.h"

#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

namespace sw
{
    /**
     * @class AbilityCueSink
     * @brief 어빌리티 게임플레이 큐의 연출(프리팹 · 소리)을 냅니다(`IAbilityCueSink`). 상태가 없다.
     * @details 이름 해시가 함수 서명 글이라 익명 이름공간이 아닌 `sw` 에 둔다. 전용 서버는 이 소스를 짓지 않아 창구가 없다.
     */
    class AbilityCueSink final : public IAbilityCueSink
    {
    public:
        static constexpr const utf8* kModulePartName = "AbilityCueSink";

        void presentCue( GameObjectManager& manager, const AbilityPresentationDef& presentation, const float3& position ) override
        {
            GameObjectManager* const     pManager = &manager;
            const AbilityPresentationDef cueDef   = presentation;
            pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, cueDef, position]()
            {
                if ( cueDef._prefab.empty() == false )
                {
                    AssetManager*   pAssetManager = game::findSubsystem<AssetManager>();
                    GameObject*     pSpawned      = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( pManager, cueDef._prefab.view() ) : nullptr;
                    SceneComponent* pScene        = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                    if ( pScene != nullptr )
                        pScene->setWorldPosition( position );
                    else
                        SW_LOG_WARNING_ID( "AbilityComponent.GameplayCuePrefabCould", "Gameplay cue '%#': prefab '%#' could not be spawned", cueDef._cueTag.getString(), cueDef._prefab.c_str() );
                }
                // 재생 id 는 쓰지 않는다(원샷). 오디오 서비스가 없는 구성(헤드리스)에서는 0 이다.
                if ( cueDef._soundEvent.empty() == false && cueDef._bPositional == SW_TRUE )
                    (void)GameSound::postEventAt( cueDef._soundEvent, position );
                else if ( cueDef._soundEvent.empty() == false )
                    (void)GameSound::postEvent( cueDef._soundEvent );
            } ) );
        }
    };

    SW_MODULE_PART( Engine, AbilityCueSink, IAbilityCueSink );
} // namespace sw
