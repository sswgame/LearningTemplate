/**
 * @file RigJiggleNode.h
 * @brief 지글 노드(`Jiggle`)입니다. 부드러운 살의 2 차 움직임(가슴 · 배 · 허벅지)을 스프링 사슬(`RigSpringChain`)로 풀고, 물성은 몸 입력(`RigBodyInput`)의 묶음에서 받습니다.
 * @details 리그 키는 `bones`(뿌리 → 끝, 두 본 이상), `group`(몸 입력의 지글 묶음 이름), `use_shared_colliders`(물리 에셋의 구 · 캡슐과 부딪힘)다. 물성 키는 리그에 없다 —
 *          캐릭터 프리셋이 정한다. 묶음이 없으면 노드는 포즈를 건드리지 않는다(로드 검사 `checkGroups` 가 오류로 알린다).
 *          동작 배율은 느슨함(1 − 강성)과 충격에 곱하고, 애니메이션 커브 배율은 공통 키 `weight_curve` 가 맡는다(달리기 클립의 커브가 흔들림을 키운다).
 *          스프링 사슬과 같은 시뮬레이션을 쓴다 — 머리카락 · 꼬리 노드(`SpringChain`)와 따로 두는 것은 물성 출처뿐이다(언리얼 AnimDynamics · 유니티 Dynamic Bone 과 같은 길).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/Rig/RigBodyInput.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Animation/Rig/RigSpringChain.h"

namespace sw
{
    class RigInstance;
    class RigNodeRegistry;

    /** @class RigJiggleNode @brief 파일 머리말 참고. */
    class SW_API RigJiggleNode final : public RigNode
    {
    public:
        static constexpr const utf8* kTypeName = "Jiggle";

        RigJiggleNode();

        unique_ptr<RigNode> clone() const override;
        const utf8*         getTypeName() const override { return kTypeName; }
        [[nodiscard]] bool  parse( RigXMLReader& reader ) override;
        [[nodiscard]] bool  bind( const RigBindContext& context ) override;
        void                evaluate( RigEvaluateContext& context ) override;
        void                collectWrittenBones( vector<uint32>& inoutListBone ) const override;
        void                reset() override { _chain.reset(); }

        const hashed_string&  getGroup() const { return _group; }
        const RigSpringChain& getChain() const { return _chain; }

        /**
         * @brief 묶음 물성 @p group 과 동작 배율 @p motionScale 로 스프링 값을 만듭니다. 느슨함(1 − 강성)에 배율을 곱한다 — 0 이면 애니메이션 그대로, 2 면 두 배로 느슨하다.
         * @details 순수 함수라 테스트가 직접 부른다.
         */
        static RigSpringSettings makeSettings( const RigJiggleGroupParams& group, float32 motionScale );
        /** @brief @p instance 의 모든 `Jiggle` 노드가 가리키는 묶음이 몸 입력에 있는지 봅니다. 없는 묶음마다 오류를 남기고 false 입니다(조용히 흔들리지 않게 하지 않는다). */
        [[nodiscard]] static bool checkGroups( const RigInstance& instance, string_view label );
        /** @brief 노드를 등록합니다(`RigNodeLibrary::registerEngineNodes`). */
        static void registerNodes( RigNodeRegistry& registry );

    private:
        vector<hashed_string> _listBoneName;
        vector<uint32>        _listBone;
        RigSpringChain        _chain;
        hashed_string         _group;
        uint8                 _bUseSharedColliders;
    };
} // namespace sw
