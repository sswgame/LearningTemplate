/**
 * @file GameplayDebugTarget.h
 * @brief 런타임 게임플레이 디버거(디버그 HUD 섹션 `quest` · `progress` · `gimmick` · `npc` · `contacts` · `movement` · `ability` · `interaction`, 엔진의 `action` 은 대상이 없다)가 볼 대상입니다.
 * @details 언리얼 Gameplay Debugger 의 카테고리 + 대상 고르기(`gdt.SelectActor`)와 같은 나눔이다 — 섹션마다 자기 폴더에서 `SW_DEBUG_HUD_SECTION` 으로 등록하고(키트도 같은 길),
 *          대상은 디버그 섹션 표(`DebugHUDRegistry::setTarget`)에 핸들 하나다 — 개발 명령 `debug.target <이름|#id>` 와 씬 뷰 HUD(에디터 선택을 따른다)가 정한다. 정하지 않았거나 그 오브젝트에 그 컴포넌트가 없으면 활성 씬에서 그 컴포넌트를 가진 첫 오브젝트다.
 *          섹션은 HUD 가 보이고 그 섹션이 켜졌을 때만(4 Hz) 불리고 읽기만 한다 — 꺼져 있으면 비용이 없다. 줄 수는 `kMaxLines` 로 묶는다(고정 용량).
 *          게임 스레드에서만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @struct GameplayDebugTarget @brief 게임플레이 디버그 섹션의 대상 고르기와 줄 상한입니다. */
    struct SW_GF_API GameplayDebugTarget
    {
        /** @brief 섹션 하나가 쓰는 줄의 상한입니다(넘으면 `… N more` 한 줄). */
        static constexpr uint32 kMaxLines = 24;

        /** @brief 활성 씬의 오브젝트 매니저입니다. 씬이 없으면 nullptr 입니다. */
        static GameObjectManager* findActiveObjectManager();
        /** @brief `debug.target` · 에디터 선택이 정한 대상입니다(무효면 정하지 않았다). */
        static GameObjectHandle getTarget();
        /** @brief 대상을 정합니다(무효 핸들은 지운다). 개발 명령과 테스트가 부른다. */
        static void setTarget( GameObjectHandle target );
        /** @brief @p manager 에서 대상 오브젝트를 풉니다. 정하지 않았거나 사라졌거나 다른 씬의 것이면 nullptr 입니다. */
        static const GameObject* findTargetObject( const GameObjectManager& manager );
        /** @brief 활성 씬에서 푼 대상의 이름입니다(풀리지 않으면 빈 값). 이름으로 맞추는 쪽(일정 NPC id)만 쓴다. */
        static string getTargetName();
        /**
         * @brief `debug.target` 인자를 대상으로 풉니다 — `#<id>` · 숫자는 objectID, 그 밖은 활성 씬의 오브젝트 이름(같은 이름이 둘 이상이면 실패하고 id 들을 알린다).
         * @return 풀었으면 핸들, 아니면 까닭(없는 id · 없는 이름 · 겹치는 이름 — 겹치면 detail 에 id 들)입니다.
         */
        static expected<GameObjectHandle> resolveTargetArgument( const GameObjectManager& manager, string_view argument );

        /**
         * @brief 대상 오브젝트의 @p T 컴포넌트입니다. 대상을 정했고 그 오브젝트에 @p T 가 있으면 그것, 아니면 @p manager 에서 @p T 를 가진 첫 오브젝트(만든 순서)의 것입니다.
         * @details 대상 오브젝트에 @p T 가 없을 때 첫 것으로 바꾸는 까닭: 섹션마다 대상 종류가 다르다(퀘스트는 판 상태, 이동은 캐릭터) — 대상은 "보고 싶은 것" 의 힌트다.
         *          그래서 섹션은 어떤 오브젝트를 보는지 첫 줄(`target`)에 적는다.
         */
        template <typename T>
        static T* findTargetComponent( const GameObjectManager& manager )
        {
            if ( const GameObject* pTarget = findTargetObject( manager ); pTarget != nullptr )
            {
                if ( T* pComponent = pTarget->getComponent<T>(); pComponent != nullptr )
                    return pComponent;
            }
            for ( const GameObject* pObject : manager.getAllGameObjects() )
            {
                if ( T* pComponent = pObject != nullptr ? pObject->getComponent<T>() : nullptr; pComponent != nullptr )
                    return pComponent;
            }
            return nullptr;
        }
    };
} // namespace sw
