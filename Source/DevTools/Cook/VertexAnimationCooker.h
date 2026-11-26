/**
 * @file VertexAnimationCooker.h
 * @brief 정점 애니메이션(VAT) 쿠킹 — 데이터(`*.vertexanimation.xml`, `VertexAnimationCookList`)가 고른 (메시, 클립)을 `.vat` 로 베이크합니다.
 * @details 런타임이 처음 쓸 때 베이크하는 길과 같은 함수(`MeshVertexAnimationBaker`)라 결과가 바이트까지 같다. `App --cook-scenes` 단계가 부른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Content/CookJob.h"

namespace sw
{
    struct VertexAnimationCookList;

    class CookStore;

    /** @struct VertexAnimationCooker @brief 파일 머리말 참고. */
    struct VertexAnimationCooker
    {
        /**
         * @brief 모든 목록을 베이크합니다. 활성 게임이 아닌 게임 팩의 목록은 건너뜁니다(배포본은 활성 게임의 팩만 연다).
         * @param framesPerSecond VAT 프레임율(군중 표 `animationcrowd.xml` 과 같은 값이어야 런타임 베이크와 같다). 작업 키의 설정이다.
         * @param store 기록과 DDC 입니다. (목록, 클립)마다 작업 하나로 베이크하고 — 목록 · 메시 · 스켈레톤 · 클립이 입력이다 — 기록이 새것이면 DDC 의 출력을 다시 씁니다.
         * @return 쓴 `.vat` 수입니다. 읽지 못한 목록 · 베이크하지 못한 클립은 @p outFailedCount 에 셉니다.
         */
        static uint32 cookAll( const AbsolutePath& resourceRoot, const AbsolutePath& cookedDir, float32 framesPerSecond, const CookStore& store, uint32& outFailedCount );
        /** @brief 목록의 클립 하나를 @p outputPath 에 베이크합니다. 메시 · 스켈레톤 · 클립을 읽습니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool cookClip( const VertexAnimationCookList& list, const hashed_string& clipName, const AbsolutePath& outputPath, float32 framesPerSecond );
        /** @brief VAT 쿠킹 작업의 키 값입니다. 설정 해시는 프레임율입니다. */
        static CookJobDesc makeCookJobDesc( float32 framesPerSecond );
    };
} // namespace sw
