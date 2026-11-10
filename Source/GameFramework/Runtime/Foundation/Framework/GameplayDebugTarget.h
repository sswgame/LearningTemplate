/**
 * @file GameplayDebugTarget.h
 * @brief 런타임 게임플레이 디버거(디버그 HUD 섹션 `quest` · `progress` · `gimmick` · `npc` · `contacts` · `movement` · `ability` · `interaction`, 엔진의 `action` 은 대상이 없다)가 볼 대상입니다.
 * @details 언리얼 Gameplay Debugger 의 카테고리 + 대상 고르기(`gdt.SelectActor`)와 같은 나눔이다 — 섹션마다 자기 폴더에서 `SW_DEBUG_HUD_SECTION` 으로 등록하고(키트도 같은 길),
 *          대상은 디버그 섹션 표(`DebugHUDRegistry::setTargetName`)에 하나다 — 개발 명령 `debug.target <오브젝트 이름>` 과 에디터 Gameplay Debugger 창(선택을 따른다)이 정한다. 정하지 않았거나 그 오브젝트에 그 컴포넌트가 없으면 활성 씬에서 그 컴포넌트를 가진 첫 오브젝트다.
 *          섹션은 HUD 가 보이고 그 섹션이 켜졌을 때만(4 Hz) 불리고 읽기만 한다 — 꺼져 있으면 비용이 없다. 줄 수는 `kMaxLines` 로 묶는다(고정 용량).
 *          게임 스레드에서만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

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
        /** @brief `debug.target` · 에디터 선택이 정한 이름입니다(비면 정하지 않았다). 사본이다 — 대상은 섹션 표가 잠금 안에 든다. */
        static string getTargetName();
        /** @brief 대상 이름을 정합니다(빈 값은 지운다). 개발 명령과 테스트가 부른다. */
        static void setTargetName( string_view name );

        /**
         * @brief 대상 오브젝트의 @p T 컴포넌트입니다. 이름을 정했고 그 오브젝트에 @p T 가 있으면 그것, 아니면 @p manager 에서 @p T 를 가진 첫 오브젝트(만든 순서)의 것입니다.
         * @details 정한 이름의 오브젝트에 @p T 가 없을 때 첫 것으로 바꾸는 까닭: 섹션마다 대상 종류가 다르다(퀘스트는 판 상태, 이동은 캐릭터) — 이름은 "보고 싶은 것" 의 힌트다.
         *          그래서 섹션은 어떤 오브젝트를 보는지 첫 줄(`target`)에 적는다.
         */
        template <typename T>
        static T* findTargetComponent( const GameObjectManager& manager )
        {
            const string targetName = getTargetName();
            if ( targetName.empty() == false )
            {
                if ( const GameObject* pNamed = manager.findGameObjectByName( hashed_string( targetName ) ); pNamed != nullptr )
                {
                    if ( T* pComponent = pNamed->getComponent<T>(); pComponent != nullptr )
                        return pComponent;
                }
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
