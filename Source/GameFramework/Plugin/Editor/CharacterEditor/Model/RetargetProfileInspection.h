/**
 * @file RetargetProfileInspection.h
 * @brief 리타깃 프로필을 두 스켈레톤에 묶어 보는 점검입니다(편집기 리타깃 창이 보인다). 런타임 리타기터(`PoseRetargeter::initialize`)와 같은 검사 하나를 쓴다.
 * @details 언리얼 IK Retargeter 의 경고 목록처럼 무엇이 틀렸는지 줄마다 말한다 — 없는 본 이름을 먼저 모아 리타기터를 부르지 않으므로, 입력 중인 프로필이 오류 로그를 쏟지 않는다.
 *          이름이 다 있으면 묶기 자체는 런타임 리타기터가 판정한다(에디터 쪽 사본 규칙을 두지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class RetargetProfile;
    class Skeleton;
} // namespace sw

namespace sw::editor
{
    /** @brief 사슬 하나의 레퍼런스 길이 쌍입니다(m). 비가 크게 다르면 IK 목표 사슬이 필요하다는 신호다. */
    struct RetargetChainInspection
    {
        hashed_string _name{};
        float32       _sourceLength{ 0.0f };
        float32       _targetLength{ 0.0f };

        /** @brief 길이 비(대상 / 원본)입니다. 원본 길이가 0 이면 0 입니다. */
        float32 computeLengthRatio() const { return _sourceLength > 0.0f ? _targetLength / _sourceLength : 0.0f; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로필 점검 결과입니다(에디터 리타깃 창이 보인다). */
    struct RetargetProfileInspection
    {
        vector<RetargetChainInspection> _listChain{};
        vector<string>                  _listProblem{}; ///< 묶지 못하게 하는 까닭(없는 본 · 빈 칸). 하나라도 있으면 저장하지 않는다
        vector<string>                  _listWarning{}; ///< 묶이지만 뜻대로 돌지 않을 곳(뒤집힌 사슬 · 겹친 대상 본 · 짧은 IK 사슬)
        float32                         _heightRatio{ 0.0f };
        uint32                          _mappedTargetBoneCount{ 0 };
        uint32                          _targetBoneCount{ 0 };

        // 프로필을 두 스켈레톤에 묶어 보는 점검입니다 — 런타임 리타기터와 같은 검사 하나(에디터 쪽 사본 규칙을 두지 않는다). */
        /**
         * @brief 프로필이 적은 두 스켈레톤을 디스크에서 읽어 점검합니다. 저장 직전처럼 지금 파일로 판정할 때 씁니다.
         * @return 스켈레톤을 읽지 못하거나 묶이지 않으면 까닭(`Error`)입니다.
         */
        [[nodiscard]] static expected<void> inspect( const RetargetProfile& profile, RetargetProfileInspection& outInspection );
        /**
         * @brief 이미 읽어 둔 스켈레톤으로 점검합니다. 편집마다 디스크를 읽지 않게 창이 씁니다. 못 읽은 쪽은 nullptr 입니다.
         * @return 위와 같습니다. 없는 본은 줄마다 `_listProblem` 에 담고 오류의 대상(`detail`)에는 첫 줄을 담습니다.
         */
        [[nodiscard]] static expected<void> inspect( const RetargetProfile& profile, const Skeleton* pSourceSkeleton, const Skeleton* pTargetSkeleton, RetargetProfileInspection& outInspection );
    };
} // namespace sw::editor
