/**
 * @file Editor/Common/Workspace/Undo/ObjectSnapshot.h
 * @brief 오브젝트 편집을 직렬화한 상태로 기록하는 Undo 데이터 명령입니다 — 코드는 엔진에 있어 에디터 모듈 핫 리로드를 넘습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"

namespace sw
{
    class GameObject;
    class SceneManager;

    enum class ObjectLifetimeEdit : uint8;

    /**
     * @struct ObjectSnapshot
     * @brief 오브젝트 하나의 상태(XML)와 찍을 때의 런타임 id 입니다.
     * @details 되돌리기는 상태를 다시 읽으면서 컴포넌트를 전부 새로 만듭니다. id 를 같이 적어 두지 않으면 속성 하나만 되돌려도 컴포넌트마다 새 id 가
     *          나가, 그 컴포넌트를 가리키던 `ComponentHandle`(컴포넌트 선택 · 씬의 활성 카메라 등)이 끊깁니다.
     */
    struct SW_EDITOR_API ObjectSnapshot
    {
        string         _xml;
        ObjectIdentity _identity;

        // 오브젝트 편집을 `CommandStack` 의 데이터 명령으로 만듭니다(UE `FTransaction` 의 오브젝트 레코드 자리).
        /** @brief 지금 상태(런타임 id 포함)를 찍습니다. nullptr 이면 빈 스냅샷입니다. */
        static ObjectSnapshot captureSnapshot( const GameObject* pObj );

        /** @brief 오브젝트 @p obj 의 수정 전후 상태를 오가는 명령을 만듭니다. undo 는 @p before, redo 는 @p after 를 다시 읽습니다. */
        static CommandStack::Command makeModify( SceneManager& sceneManager, const GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after,
                                                 string_view label );

        /**
         * @brief 생성 · 삭제 명령을 만듭니다. 같은 두 절차(없애기 · 원래 id 로 복원하기)를 @p edit 이 정한 순서로 잇습니다.
         * @param pObj 방금 만든 · 곧 지울 오브젝트. 지금 상태 · 이름 · 프리팹 경로를 여기서 찍습니다.
         */
        static CommandStack::Command makeLifetime( SceneManager& sceneManager, const GameObject* pObj, ObjectLifetimeEdit edit, string_view label );
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 수명 편집의 방향입니다. 어느 쪽이 "복원하기" 인지를 정합니다. */
    enum class ObjectLifetimeEdit : uint8
    {
        Created = 0, ///< 방금 만들었습니다. Undo 가 없애고 Redo 가 복원합니다
        Destroyed    ///< 방금 지웠습니다. Undo 가 복원하고 Redo 가 없앱니다
    };

} // namespace sw
