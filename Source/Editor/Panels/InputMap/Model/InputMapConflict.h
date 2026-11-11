/**
 * @file InputMapConflict.h
 * @brief Input Map Editor 의 바인딩 충돌 찾기입니다 — 같은 레이어에서 한 입력이 두 액션을 함께 발화시키는 곳(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Input/IInputDevice.h"

namespace sw
{
    class InputMap;
} // namespace sw

namespace sw::editor
{
    struct InputMapConflictView;

    /** @brief 충돌 하나 — 액션 둘이 같은 레이어에서 같은 슬롯을 쓴다. */
    struct InputMapConflict
    {
        hashed_string _actionA;
        hashed_string _actionB;
        hashed_string _layer;
        InputSlot     _slot{};
        uint32        _bindIndexA{ 0 }; ///< A 의 그 바인딩 번호
        uint32        _bindIndexB{ 0 }; ///< B 의 그 바인딩 번호

        // 바인딩 충돌을 모읍니다. 슬롯 수는 `BindingKinds::getConflictSlotCount` 가 정한다(합성 축의 네 방향 · Chord 의 수식 키 포함).
        // 같은 액션 안의 겹침(Enter · Space 가 둘 다 Confirm)은 충돌이 아니다. 쌍은 액션 순서대로 한 번만 담는다.
        /** @brief @p inputMap 의 충돌을 @p outListConflict 에 담습니다(먼저 비운다). */
        static void collect( const InputMap& inputMap, vector<InputMapConflict>& outListConflict );
        /** @brief @p action 이 낀 충돌이 있으면 true 입니다. */
        static bool isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action );
        /** @brief @p action 의 바인딩 @p bindIndex 가 낀 충돌이 있으면 true 입니다. */
        static bool isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action, uint32 bindIndex );
        /** @brief @p action 이 낀 충돌을 그 액션 쪽에서 본 줄로 @p outListView 에 담습니다(먼저 비운다). */
        static void collectFor( const vector<InputMapConflict>& listConflict, const hashed_string& action, vector<InputMapConflictView>& outListView );
    };

    /** @brief 충돌 하나를 어느 한 액션 쪽에서 본 것입니다(상세 칸의 한 줄). */
    struct InputMapConflictView
    {
        hashed_string _otherAction;
        hashed_string _layer;
        InputSlot     _slot{};
        uint32        _bindIndex{ 0 };      ///< 보는 액션의 바인딩 번호
        uint32        _otherBindIndex{ 0 }; ///< 상대 액션의 바인딩 번호
    };
} // namespace sw::editor
