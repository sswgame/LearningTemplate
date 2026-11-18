/**
 * @file GimmickCircuitEditSession.h
 * @brief 기믹 그래프 창이 편집하는 회로(선택한 오브젝트의 `GimmickCircuitComponent`)를 찾고, 모델로 읽고, 모델을 되돌리기 한 단계로 씁니다. 씬 · 선택 · 거래는 이 파일의 `.cpp` 만 본다.
 * @details 창 View 는 작업 공간 · 씬 헤더를 보지 않는다(`docs/agents/EditorUI.md` MVVM) — 회로는 핸들로만 들고, 이름 · 대상 같은 값은 질의로 받는다.
 *          언리얼 디테일 패널의 `FScopedTransaction` 과 같이 쓰기 한 번이 되돌리기 한 단계다(컴포넌트는 쓰기 전까지 그대로라 "고치기 전" 은 쓸 때 찍는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw::editor
{
    class GimmickGraphModel;

    /** @brief `GimmickCircuitEditSession::sync` 의 결과입니다. */
    enum class GimmickCircuitSync : uint8
    {
        NoCircuit,    ///< 선택에도 보던 오브젝트에도 회로가 없다
        Unchanged,    ///< 보던 회로가 그대로다
        Reloaded,     ///< 보던 회로가 바깥(되돌리기 · 인스펙터)에서 바뀌어 다시 읽었다
        OwnerChanged, ///< 다른 오브젝트의 회로를 읽었다
    };

    /**
     * @class GimmickCircuitEditSession
     * @brief 편집 대상 회로의 핸들과 읽기 · 쓰기입니다. 파일 머리말 참고.
     */
    class GimmickCircuitEditSession
    {
    public:
        /**
         * @brief 겨누는 창의 선택에서 회로를 찾고(없으면 보던 회로), 다른 회로이거나 저장 내용의 서명이 @p signature 와 다르면 @p outModel 로 다시 읽습니다.
         * @param outLoadFailed 다시 읽었는데 속성을 읽지 못했으면 true 입니다(모델은 비운다).
         */
        GimmickCircuitSync sync( GimmickGraphModel& outModel, uint64 signature, bool& outLoadFailed );
        /** @brief 보는 회로를 잊습니다. */
        void clear() { _ownerHandle = GameObjectHandle{}; }
        /** @brief 회로를 가진 오브젝트의 이름입니다. 없으면 빈 글입니다. */
        [[nodiscard]] string getOwnerName() const;
        /** @brief @p handle 오브젝트의 이름을 @p outName 에 씁니다. 그 오브젝트가 없으면 false 입니다. */
        [[nodiscard]] bool findObjectName( const GameObjectHandle& handle, string& outName ) const;
        /** @brief 회로와 같은 선택(겨누는 창)의 첫 오브젝트입니다. 고른 것이 없으면 빈 핸들입니다. */
        [[nodiscard]] GameObjectHandle findFirstSelectedObject() const;
        /** @brief @p model 을 회로에 쓰고 되돌리기 한 단계(@p pLabel)로 남깁니다. 회로가 없거나 속성을 쓰지 못하면 false 입니다. */
        [[nodiscard]] bool commit( const GimmickGraphModel& model, const utf8* pLabel );

    private:
        GameObjectHandle _ownerHandle{}; ///< 보는 회로를 가진 오브젝트
    };
} // namespace sw::editor
