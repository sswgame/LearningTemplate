/**
 * @file AbilityDebugViewModel.h
 * @brief 어빌리티 디버거 창의 ViewModel — 대상 고르기(선택 · 목록), 플레이 상태, 대상의 런타임 상태(`AbilityDebugSnapshot`)를 값으로 모읍니다. ImGui 를 모릅니다.
 * @details 창(`AbilityDebugPanel`)은 `gather` 가 채운 값만 그리고 대상 고르기를 명령으로 넘긴다(`docs/agents/EditorUI.md` MVVM). 씬 · 선택 · 컴포넌트는 이 파일의 `.cpp` 만 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityDebugSnapshot.h"

namespace sw::editor
{
    /** @brief 대상 고르기 목록의 한 줄입니다(활성 씬에서 `AbilityComponent` 를 가진 오브젝트). */
    struct AbilityDebugCandidate
    {
        string           _name{};
        GameObjectHandle _handle{};
        uint32           _objectID{ 0 };
        uint8            _bCurrent{ SW_FALSE }; ///< 지금 보는 대상이다
    };

    /** @brief 창이 한 프레임 그릴 값입니다(`AbilityDebugViewModel::gather`). */
    struct AbilityDebugViewState
    {
        AbilityDebugSnapshot _snapshot;                 ///< 대상의 상태(`_bHasSnapshot` 일 때만 뜻이 있다)
        string               _targetName;               ///< 지금 대상의 이름. 없으면 빈 글
        uint8                _bHasScene{ SW_FALSE };    ///< 활성 씬이 있다(대상 고르기 칸을 그린다)
        uint8                _bPlayStopped{ SW_FALSE }; ///< 에디터 플레이가 멈춰 있다
        uint8                _bHasSnapshot{ SW_FALSE }; ///< 플레이 중이고 대상이 있어 상태를 모았다
    };

    /**
     * @class AbilityDebugViewModel
     * @brief 어빌리티 디버거의 상태와 명령입니다. 파일 머리말 참고.
     */
    class AbilityDebugViewModel
    {
    public:
        /** @brief 대상을 정하고 이번 프레임 값을 @p outState 에 채웁니다. 창이 그리기 맨 앞에서 한 번 부릅니다. 선택한 오브젝트에 컴포넌트가 있으면 그것이 대상이 된다. */
        void gather( AbilityDebugViewState& outState );
        /** @brief 대상 고르기 목록을 @p outListCandidate 에 채웁니다(먼저 비운다). 목록을 펼친 프레임에만 부른다 — 씬을 훑는다. */
        void gatherCandidates( vector<AbilityDebugCandidate>& outListCandidate ) const;
        /** @brief 목록에서 고른 오브젝트를 대상으로 둡니다(보기 상태 — 기록하지 않는다). */
        void pickTarget( const GameObjectHandle& handle ) { _pickedHandle = handle; }

    private:
        GameObjectHandle _pickedHandle{}; ///< 목록에서 고른(또는 선택으로 정한) 대상의 주인
    };
} // namespace sw::editor
