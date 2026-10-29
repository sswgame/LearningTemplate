/**
 * @file RetargetProfileInspection.h
 * @brief 리타깃 프로필을 두 스켈레톤에 묶어 보는 점검입니다(편집기 리타깃 창이 보인다). 런타임 리타기터(`PoseRetargeter::initialize`)와 같은 검사 하나를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class RetargetProfile;
} // namespace sw

namespace sw::editor
{
    /** @brief 사슬 하나의 레퍼런스 길이 쌍입니다(m). 비가 크게 다르면 IK 목표 사슬이 필요하다는 신호다. */
    struct RetargetChainInspection
    {
        hashed_string _name{};
        float32       _sourceLength{ 0.0f };
        float32       _targetLength{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로필 점검 결과입니다(에디터 리타깃 창이 보인다). */
    struct RetargetProfileInspection
    {
        vector<RetargetChainInspection> _listChain{};
        float32                         _heightRatio{ 0.0f };
        uint32                          _mappedTargetBoneCount{ 0 };
        uint32                          _targetBoneCount{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로필을 두 스켈레톤에 묶어 보는 점검입니다 — 런타임 리타기터와 같은 검사 하나(에디터 쪽 사본 규칙을 두지 않는다). */
    struct RetargetProfileInspector
    {
        /**
         * @brief 프로필이 적은 두 스켈레톤을 읽고 리타기터를 묶어 봅니다. 사슬마다 레퍼런스 길이와 짝지은 대상 본 수를 냅니다.
         * @return 스켈레톤을 읽지 못하거나 묶이지 않으면 false 이고 까닭을 @p outError 에 영어 한 문장으로 담습니다(본 이름은 로그).
         */
        [[nodiscard]] static bool inspect( const RetargetProfile& profile, RetargetProfileInspection& outInspection, string& outError );
    };
} // namespace sw::editor
