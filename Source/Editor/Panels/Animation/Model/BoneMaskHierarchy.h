/**
 * @file BoneMaskHierarchy.h
 * @brief 본 마스크를 스켈레톤 계층 위에 펼친 보기와 칠하기 명령입니다 — 본마다 최종 가중치 · 그 값의 출처(이 본의 항목 · 조상이 퍼뜨린 값 · 없음), 스켈레톤에 없는 본 이름. ImGui 를 모릅니다.
 * @details 최종 가중치는 런타임의 규칙(`AnimationBoneMaskDef::computeBoneWeights`)으로 구한다 — 보기가 따로 셈하면 미리보기와 실제 적용이 갈린다. 스켈레톤은 검증 문맥일 뿐 마스크에 저장하지 않는다
 *          (언리얼 Blend Profile · 유니티 Avatar Mask 의 Transform 트리가 스켈레톤을 골라 보이되 마스크에는 본 이름만 남는 것과 같다). 칠하기는 마스크 항목을 바로 고친다 —
 *          자손까지는 "roots" 항목, 이 본 하나는 "bones" 항목이다(`AnimationBoneMask.h` 의 형식).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class Skeleton;

    struct AnimationBoneMaskDef;
} // namespace sw

namespace sw::editor
{
    /** @brief 본 하나의 최종 가중치가 어디서 왔는지입니다. */
    enum class BoneMaskWeightSource : uint8
    {
        None = 0,  ///< 어느 항목도 닿지 않아 0 이다
        Inherited, ///< 조상의 "roots" 항목이 퍼뜨린 값이다
        Subtree,   ///< 이 본의 "roots" 항목이다(자손에 퍼진다)
        Bone,      ///< 이 본의 "bones" 항목이다(이 본 하나만 — 퍼뜨린 뒤에 덮는다)
    };

    /** @brief 계층 보기의 줄 하나, 곧 스켈레톤 본 하나입니다. 줄 순서는 스켈레톤 본 순서(부모가 앞)입니다. */
    struct BoneMaskBoneRow
    {
        hashed_string        _bone{};
        optional<uint32>     _parentIndex;    ///< 부모 줄 번호(루트는 비어 있다)
        uint32               _depth{ 0 };     ///< 루트 0 부터의 깊이(들여쓰기)
        float32              _weight{ 0.0f }; ///< 런타임이 쓸 최종 가중치
        BoneMaskWeightSource _source{ BoneMaskWeightSource::None };
    };

    /**
     * @struct BoneMaskHierarchy
     * @brief 계층 보기를 만들고 마스크 항목을 칠하는 명령 모음입니다.
     */
    struct BoneMaskHierarchy
    {
        /**
         * @brief @p skeleton 의 본마다 줄을 만들어 @p outListRow 에 씁니다(먼저 비운다). 마스크가 가리키지만 스켈레톤에 없는 본 이름은 @p outListMissingBone 에 겹치지 않게 모읍니다.
         * @details 없는 본은 런타임이 오류로 마스크를 버린다 — 보기는 그 항목을 빼고 나머지로 가중치를 보이며 없는 이름을 따로 알린다(조용히 지우지 않는다).
         */
        static void build( const Skeleton& skeleton, const AnimationBoneMaskDef& mask, vector<BoneMaskBoneRow>& outListRow, vector<hashed_string>& outListMissingBone );
        /**
         * @brief @p bone 에 가중치를 칠합니다(0 이상 1 이하로 묶는다). @p bSubtree 면 자손까지 퍼지는 "roots" 항목을 두고 이 본의 "bones" 항목은 지웁니다(덮어 가리므로).
         *        아니면 이 본 하나의 "bones" 항목을 둡니다. 같은 본 · 같은 종류의 항목이 있으면 값만 바꿉니다.
         * @return 마스크가 바뀌었으면 true 입니다(같은 값이면 false — 되돌리기 단계를 쌓지 않는다).
         */
        [[nodiscard]] static bool paintWeight( AnimationBoneMaskDef& inoutMask, const hashed_string& bone, float32 weight, bool bSubtree );
        /**
         * @brief @p bone 의 항목을 모두 지워 조상의 값을 물려받게 합니다.
         * @return 항목이 없었거나, 지우면 항목이 하나도 남지 않으면(로드 오류가 된다) 바꾸지 않고 false 입니다.
         */
        [[nodiscard]] static bool resetBone( AnimationBoneMaskDef& inoutMask, const hashed_string& bone );
    };
} // namespace sw::editor
