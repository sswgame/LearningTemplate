/**
 * @file FrameSystemUtil.h
 * @brief 매니저에 프레임 시스템(`ISceneFrameSystem`)을 형식으로 찾고 · 붙이고 · 떼는 한 벌입니다.
 * @details 프레임 시스템마다 같은 본문(키로 찾아 형식으로 바꾸기, 없으면 만들어 붙이기)을 따로 적던 것을 모은다. 시스템은 키를 `kFrameSystemKey`(글 상수)로 내놓는다.
 *          키 이름은 형식마다 한 번만 만든다(함수 정적) — 예전에는 `find` 를 부를 때마다 글에서 이름을 다시 찾았다(프레임마다 도는 틱 뒤 일 · 조종자 틱).
 *          언리얼 `UWorld::GetSubsystem<T>()` · 유니티 `World.GetOrCreateSystem<T>()` 와 같은 자리다. 매니저(엔진)가 형식 창구를 갖기 전까지 GameFramework 의 것만 쓴다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /**
     * @struct FrameSystemUtil
     * @brief `TSystem::kFrameSystemKey` 로 매니저의 프레임 시스템을 다룹니다. 키 하나에는 한 형식만 붙입니다(`getOrCreate`) — 그래서 찾은 것을 그 형식으로 봅니다.
     */
    struct FrameSystemUtil
    {
        /** @brief @p TSystem 의 매니저 키 이름입니다(형식마다 한 번 만든다). */
        template <typename TSystem>
        static const hashed_string& getKey()
        {
            static const hashed_string kKey{ TSystem::kFrameSystemKey };
            return kKey;
        }

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        template <typename TSystem>
        static TSystem* find( const GameObjectManager& manager )
        {
            return static_cast<TSystem*>( manager.findFrameSystem( getKey<TSystem>() ) );
        }

        /** @brief 매니저의 시스템을 반환합니다. 없으면 기본 생성해 붙입니다(게임 스레드, 틱 밖). */
        template <typename TSystem>
        static TSystem& getOrCreate( GameObjectManager& manager )
        {
            TSystem* pSystem = find<TSystem>( manager );
            if ( pSystem != nullptr )
                return *pSystem;
            unique_ptr<TSystem> pNew = make_unique<TSystem>();
            pSystem                  = pNew.get();
            const bool bAdded        = manager.addFrameSystem( getKey<TSystem>(), std::move( pNew ) );
            SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다. 거절되면 접근 선언과 붙인 순서가 어긋난 것이고(오류 로그), 시스템은 이미 지워졌다
            (void)bAdded;
            return *pSystem;
        }

        /** @brief 매니저에서 시스템을 뗍니다. 붙어 있지 않으면 할 일이 없다. */
        template <typename TSystem>
        static void remove( GameObjectManager& manager )
        {
            manager.removeFrameSystem( getKey<TSystem>() );
        }
    };
} // namespace sw
