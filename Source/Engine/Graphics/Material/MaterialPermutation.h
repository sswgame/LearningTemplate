/**
 * @file MaterialPermutation.h
 * @brief 머티리얼 퍼뮤테이션 define 목록(용도 · 품질 · 키워드)과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Material/Material.h"

namespace sw
{
    /** @brief 머티리얼 퍼뮤테이션 define 목록(용도 · 품질 · 키워드)과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다. */
    struct SW_API MaterialPermutation
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 증가하는 전역 세대입니다.
         * @details **정지한 씬을 위한 신호입니다.** `GPUScene` 은 "등록과 해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뜁니다. 그런데 머티리얼과 인스턴스의 정적 스위치, 키워드, 멀티 컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          **바뀐 퍼뮤테이션이 영원히 반영되지 않습니다**(움직이는 씬에서는 가려집니다).
         *          누가 어떤 프리미티브를 쓰는지 역참조를 만드는 대신 세대 하나만 증가시킵니다. 바뀌는 일이
         *          드물고, 읽는 쪽은 프레임당 한 번입니다.
         */
        static uint64 getPermutationGeneration();

        /** @brief 세대를 증가시킵니다. define 이 실제로 달라질 수 있는 설정에서만 부릅니다. */
        static void bumpPermutationGeneration();

        static void appendUniqueDefine( vector<string>& outListDefine, string_view define );

        static void appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );

        static void appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );

        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
