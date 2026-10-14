/**
 * @file DeformableModifier.h
 * @brief 하위 모디파이어 스택을 품고 한 덩어리로 거는 묶음 모디파이어입니다. 패널 · 테스트가 하위 스택을 다루려고 헤더를 둡니다.
 */
#pragma once
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

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

        void               applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override;
        void               writeExtraJSON( const JSONValue& object ) const override;
        [[nodiscard]] bool readExtraJSON( const JSONValue& object ) override;

        MeshModifierStack&       getStack() { return _stack; }
        const MeshModifierStack& getStack() const { return _stack; }

    private:
        MeshModifierStack _stack;
    };
} // namespace sw::editor
