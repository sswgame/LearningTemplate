/**
 * @file Scene/ObjectUndoUtil.h
 * @brief 오브젝트 편집을 직렬화한 상태로 기록하는 Undo 데이터 명령입니다 — 코드는 엔진에 있어 에디터 모듈 핫 리로드를 넘습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Utility/CommandStack.h"

namespace sw
{
    class GameObject;
    class SceneManager;

    /**
     * @struct ObjectSnapshot
     * @brief 오브젝트 하나의 상태(XML)와 찍을 때의 런타임 id 입니다.
     * @details 되돌리기는 상태를 다시 읽으면서 컴포넌트를 전부 새로 만듭니다. id 를 같이 적어 두지 않으면 속성 하나만 되돌려도 컴포넌트마다 새 id 가
     *          나가, 그 컴포넌트를 가리키던 `ComponentHandle`(컴포넌트 선택 · 씬의 활성 카메라 등)이 끊깁니다.
     */
    struct ObjectSnapshot
    {
        string         _xml;
        ObjectIdentity _identity;
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 수명 편집의 방향입니다. 어느 쪽이 "되살리기" 인지를 정합니다. */
    enum class ObjectLifetimeEdit : uint8
    {
        Created = 0, ///< 방금 만들었습니다. Undo 가 없애고 Redo 가 되살립니다
        Destroyed    ///< 방금 지웠습니다. Undo 가 되살리고 Redo 가 없앱니다
    };

    /**
     * @struct ObjectUndoUtil
     * @brief 오브젝트 편집을 `CommandStack` 의 데이터 명령으로 만듭니다(UE `FTransaction` 의 오브젝트 레코드 자리).
     * @details 명령이 드는 것은 값뿐입니다 — 대상 오브젝트 id(핸들의 id · 원래 id 로 되살리므로 재생성 뒤에도 같다), 직렬화한 상태, 이름 · 프리팹 경로.
     *          undo · redo 의 코드는 이 파일(Engine)에 있으므로 명령을 만든 모듈이 내려가도 스택에 남아 같은 결과를 냅니다.
     *          대상은 실행할 때마다 @p sceneManager 의 **활성 씬**에서 id 로 찾고, 없으면 기록할 때의 이름으로 찾습니다.
     *          적용한 뒤에는 `CommandStack::notifyObjectEdit` 로 알려, 에디터가 선택 · 씬 dirty 를 맞춥니다.
     * @warning @p stack · @p sceneManager 는 명령보다 오래 살아야 합니다(엔진 소유 서비스 · 같은 스코프의 시험 객체).
     * @warning 이름 조회는 한 프레임 안에서 없애고 되살린 경우를 위한 것입니다. 옛 오브젝트가 지연 파괴를 기다리는 동안에는 그 id 가 아직 등록돼 있어
     *          `GameObjectManager::createGameObjectWithID` 가 새 id 를 줍니다(히스토리 점프 · 시험처럼 프레임 없이 연달아 되돌릴 때).
     */
    struct SW_API ObjectUndoUtil
    {
        /** @brief 지금 상태(런타임 id 포함)를 찍습니다. nullptr 이면 빈 스냅샷입니다. */
        static ObjectSnapshot captureSnapshot( const GameObject* pObj );

        /** @brief 오브젝트 @p obj 의 수정 전후 상태를 오가는 명령을 만듭니다. undo 는 @p before, redo 는 @p after 를 다시 읽습니다. */
        static CommandStack::Command makeModify( CommandStack& stack, SceneManager& sceneManager, const GameObject& obj,
                                                 const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label );

        /**
         * @brief 생성 · 삭제 명령을 만듭니다. 같은 두 절차(없애기 · 원래 id 로 되살리기)를 @p edit 이 정한 순서로 잇습니다.
         * @param pObj 방금 만든 · 곧 지울 오브젝트. 지금 상태 · 이름 · 프리팹 경로를 여기서 찍습니다.
         */
        static CommandStack::Command makeLifetime( CommandStack& stack, SceneManager& sceneManager, const GameObject* pObj,
                                                   ObjectLifetimeEdit edit, string_view label );
    };
} // namespace sw
