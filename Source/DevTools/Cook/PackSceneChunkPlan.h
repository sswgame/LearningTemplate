/**
 * @file PackSceneChunkPlan.h
 * @brief 씬 단위 청크 나누기입니다 — 기동 필수(엔진 · 공용 · 첫 씬과 그 밖의 뿌리가 닿는 것)는 기본 청크에 두고, 한 씬만 쓰는 것은 그 씬의 청크,
 *        둘 이상의 씬이 쓰는 것은 공유 청크(`shared`)에 둡니다. 쿠커(`PackCooker`)가 팩 몫을 나눌 때 씁니다.
 * @details 언리얼 청크 pak 의 Primary Asset 라벨 · 유니티 Addressables 그룹과 같은 자리다 — 여기서는 씬이 라벨이고, 참조 고르기(`PackReachResult`)의
 *          씬마다의 닿음이 그 라벨의 범위다. 결과는 입력 순서와 상관없이 같다(이름 · id 로만 정한다).
 *          쿠킹 끝의 확인(`findUncovered`)이 실제로 쓴 팩 몫에서 씬이 닿는 에셋마다 그 청크가 기동 필수거나 그 씬의 청크인지 본다 — 빠지면 쿠킹 오류다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/Pack/PackChunkManifest.h"

namespace sw
{
    /** @brief 씬 하나와 그 씬에서 참조를 따라 닿는 에셋 id(정규화)입니다. */
    struct PackSceneReach
    {
        string                _sceneID;
        unordered_set<string> _setReached;
    };

    /** @brief 확인에서 빠진 것 하나 — 씬이 닿는 에셋이 기동 필수도 그 씬의 청크도 아닌 곳에 있다. */
    struct PackChunkIssue
    {
        string _sceneID;
        string _assetID;
        string _chunk; ///< 그 에셋이 실제로 놓인 청크(팩에 없으면 빈 글)
    };

    /**
     * @struct PackSceneChunkPlan
     * @brief 파일 머리말 참고.
     */
    struct PackSceneChunkPlan
    {
        /** @brief 둘 이상의 씬이 쓰는 에셋의 청크 이름입니다. */
        static constexpr const utf8* kSharedChunk = "shared";

        unordered_map<string, string> _mapAssetChunk; ///< 기동 필수가 아닌 에셋 id → 청크 이름

        /** @brief 씬 id 의 청크 이름입니다(`scene_` + 게임 팩 · 폴더 · 줄기, 글자 · 숫자 · `_` 만 — 팩 파일 이름의 한 마디). */
        static string makeSceneChunkName( string_view sceneID );
        /**
         * @brief 기동 필수 @p setStartup 밖에서 씬들이 닿는 에셋을 청크에 놓습니다.
         * @return 두 씬이 같은 청크 이름이 되면(줄기가 같은 다른 폴더 등) 오류입니다.
         */
        [[nodiscard]] static expected<PackSceneChunkPlan> plan( const unordered_set<string>& setStartup, const vector<PackSceneReach>& listScene );
        /**
         * @brief 실제로 놓인 청크(@p mapAssetChunk — 팩에 담긴 모든 에셋, 기본 청크는 빈 글)로 씬마다 마운트할 청크 목록을 만듭니다(기동 필수 @p setStartupChunk 는 뺀다, 이름순).
         */
        static vector<PackSceneEntry> makeSceneChunks( const vector<PackSceneReach>& listScene, const unordered_map<string, string>& mapAssetChunk, const unordered_set<string>& setStartupChunk );
        /**
         * @brief 씬이 닿는 에셋마다 실제 청크가 기동 필수거나 그 씬의 목록(@p listSceneChunks)에 있는지 봅니다. 빠진 것을 (씬, 에셋) 순으로 돌려줍니다.
         * @details 팩에 아예 없는 에셋(@p mapAssetChunk 에 없음)도 빠진 것이다 — 씬을 열면 찾지 못한다.
         */
        static vector<PackChunkIssue> findUncovered( const vector<PackSceneReach>& listScene, const unordered_map<string, string>& mapAssetChunk, const unordered_set<string>& setStartupChunk,
                                                     const vector<PackSceneEntry>& listSceneChunks );
    };
} // namespace sw
