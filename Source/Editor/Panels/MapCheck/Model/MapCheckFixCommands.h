/**
 * @file MapCheckFixCommands.h
 * @brief 맵 검사 결과의 안전한 고침(`ValidationPropertyEdit`)을 적용합니다 — 되돌리기 한 단계, 적용 뒤 그 오브젝트를 다시 검증합니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct MapCheckFixCommands
     * @brief 검증 결과의 고침을 컴포넌트 프로퍼티에 입힙니다. 고침은 검증 함수가 데이터로 적은 것이고(`ValidationContext::addErrorWithFix`), 여기서는 그 값을 쓰기만 한다.
     * @details 대상은 결과의 출처 오브젝트(`_sourceID`)에서 타입(`_typeName`)이 같은 컴포넌트다. 여러 고침은 트랜잭션 하나라 되돌리기 한 번에 모두 돌아간다
     *          (언리얼 Map Check 의 일괄 수정 · Godot 구성 경고와 같은 자리). Play 중에는 고치지 않는다(씬 편집 거절).
     */
    struct MapCheckFixCommands
    {
        /** @brief 고칠 수 있는 결과면 true 입니다(고침이 있고 출처가 있다). 그 오브젝트가 지금 씬에 있는지는 보지 않는다. */
        static bool canFix( const ValidationIssue& issue );
        /**
         * @brief @p listIssue 중 고칠 수 있는 것을 모두 적용합니다. 되돌리기 한 단계(@p pUndoLabel)로 묶고, 고친 오브젝트를 다시 검증합니다.
         * @return 적용한 고침 수. 오브젝트 · 컴포넌트 · 프로퍼티를 찾지 못했거나 값을 쓰지 못한 고침은 경고하고 건너뛴다.
         */
        static uint32 applyFixes( GameObjectManager* pManager, const vector<ValidationIssue>& listIssue, const utf8* pUndoLabel );
    };
} // namespace sw::editor
