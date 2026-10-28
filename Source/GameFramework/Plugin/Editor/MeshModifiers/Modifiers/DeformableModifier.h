/**
 * @file DeformableModifier.h
 * @brief 하위 모디파이어 스택을 품고 한 덩어리로 거는 묶음 모디파이어입니다. 패널 · 테스트가 하위 스택을 다루려고 헤더를 둡니다.
 */
#pragma once
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw::editor
{
    /**
     * @class DeformableModifier
     * @brief 하위 스택을 위에서 아래로 차례로 겁니다. 하위 스택이 비면 항등입니다.
     * @details 하위 스택은 이 모디파이어의 공간(기즈모 기준)을 자기 "메시 로컬" 로 본다 — 묶음의 기즈모를 옮기면 하위 모디파이어 전체가 함께 옮겨진다.
     *          기즈모가 기본값(원점 · 회전 없음 · 로컬)이면 하위 스택을 메시에 바로 건 결과와 같다.
     *          하위 스택은 저장 문서의 추가 상태(`extra.stack`)에 스택 저장 문서 글(한 줄 JSON)로 넣는다.
     */
    class DeformableModifier final : public MeshModifier
    {
    public:
        DeformableModifier();

        /** @brief 묶음 모디파이어(레지스트리 id `deformable`)면 그 포인터, 아니면 nullptr 입니다. RTTI 없이 id 로 가린다. */
        static DeformableModifier* findFrom( MeshModifier& modifier );

        /**
         * @brief 중첩 경로로 모디파이어를 찾습니다. 경로의 첫 번호는 @p inoutRoot 의 번호이고, 그 모디파이어가 묶음이면 다음 번호는 그 하위 스택의 번호입니다.
         * @return 빈 경로 · 범위 밖 번호 · 중간이 묶음이 아닌 경로면 nullptr 입니다. 하위 스택 편집 UI 가 "고른 모디파이어" 를 번호 목록으로 들고 다닐 때 쓴다.
         */
        static MeshModifier* findByPath( MeshModifierStack& inoutRoot, const vector<int32>& listPath );
        /**
         * @brief 경로가 가리키는 모디파이어를 **담은** 스택입니다(추가 · 삭제 · 위아래 · 켜고 끄기를 거는 스택). 길이 1 이면 @p inoutRoot 입니다.
         * @return `findByPath` 가 nullptr 을 줄 경로면 nullptr 입니다.
         */
        static MeshModifierStack* findOwningStack( MeshModifierStack& inoutRoot, const vector<int32>& listPath );

        const utf8*              getDescription() const override { return "Groups a nested modifier stack and applies it as one unit in this modifier's gizmo space."; }
        void                     applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override;
        bool                     supportsGPU() const override { return true; }
        const MeshModifierStack* getGPUChildStack() const override { return &_stack; }
        void                     writeExtraJSON( const JSONValue& object ) const override;
        [[nodiscard]] bool       readExtraJSON( const JSONValue& object ) override;

        MeshModifierStack&       getStack() { return _stack; }
        const MeshModifierStack& getStack() const { return _stack; }

    private:
        MeshModifierStack _stack;
    };
} // namespace sw::editor
