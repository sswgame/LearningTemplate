/**
 * @file PackCooker.h
 * @brief 도메인 폴더(`engine` · `common` · `game/<팩>`)마다 팩 하나(`engine.pack` · `common.pack` · `game_<팩>.pack`)를 베이크합니다. `App --cook-packs` · `--cook-all` 이 부릅니다.
 * @details 무엇을 담을지(이 파일)와 어떤 바이트가 되는지(`PackWriter`)를 나눈다. 쿠킹은 엔진 언어 한 곳이다 — 예전 파이썬 쿠커(`CookAssets.py`)가
 *          레이아웃 · 압축 · 증분 기록을 따로 들던 것을 옮겼다.
 *          - 담는 것: 도메인 폴더의 파일 중 `PackConfig` 의 제외(전역 · 규칙별 폴더 · 패턴), 셰이더 원본, 다른 백엔드의 셰이더 바이너리, 빌드 타깃이 빼는
 *            종류(`AssetPaths::isExcludedForTarget` — 런타임이 같은 표로 읽지 않는다)를 뺀 것과, 스테이징 폴더(`<쿠킹 폴더>/<도메인>`)의 쿠킹 산출물 전부.
 *            스테이징은 도메인 기준 **같은 상대 경로**로 들어간다. 소스 트리에 남은 쿠킹본(`AssetCookPath::isCookedOutput`)은 낡은 잔재라 넣지 않고 이름을 알린다.
 *          - `.meta` 가 있는 도메인은 스테이징에 GUID 레지스트리(`assetregistry.txt`, `AssetDatabase::writeRegistryFiles`)가 있어야 한다. 없으면 그 팩은 실패다 —
 *            배포본은 `.meta` 를 싣지 않으므로 레지스트리가 GUID 의 유일한 출처다.
 *          - 증분: 팩 하나가 쿠킹 작업 하나다(`CookStore::cookFile`). 입력은 읽은 파일마다의 내용 해시와 두 폴더의 목록이고, 설정(`PackConfig` 전체 · 셰이더 폴더 ·
 *            빌드 타깃)은 작업 키의 설정 해시다. 무변경 재쿠킹은 팩을 다시 쓰지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct PackConfig;

    class CookStore;
} // namespace sw

namespace sw
{
    /** @struct PackCookParams @brief 팩 쿠킹 한 번의 입력입니다. 모두 필수입니다(`_gamePackFolder` 만 비울 수 있다). */
    struct PackCookParams
    {
        string _resourceRoot;   ///< `Content/` 의 절대 경로
        string _cookedDir;      ///< 씬 · 프리팹 · 레지스트리 쿠킹 산출물의 스테이징 루트(`--cooked-dir`)
        string _packOutputDir;  ///< 팩을 쓸 폴더(`--pack-dir`)
        string _shaderFolder;   ///< 담을 셰이더 바이너리 폴더(`dx12` 등, 쿠킹 표의 `shader_folder`)
        string _buildTarget;    ///< 빌드 타깃(`Game` · `Client` · `Server`) — 그 타깃이 빼는 종류를 팩에 넣지 않는다
        string _gamePackFolder; ///< 비우면 `Content/game/` 의 모든 팩, 아니면 그 폴더(`empty`) 하나만 베이크한다
        /**
         * @brief 담지 않을 소스 파일의 절대 경로(`FileUtil::normalizePath`)입니다. 비어 있을 수 있다.
         * @details 이 층(Resource)이 읽을 수 없는 규칙으로 위 층이 정한다 — 출시 문화권(`stagedCultures`) 밖의 번역 테이블(`LocalizationProject::collectUnstagedTranslationTables`).
         *          목록은 쿠킹 설정 해시에 들어가 바뀌면 팩을 다시 베이크한다.
         */
        vector<string> _listExcludedSourceFile;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackCooker
     * @brief 팩 쿠킹입니다. 파일 머리말 참고.
     */
    struct SW_API PackCooker
    {
        /** @brief 쿠커 버전 — 담는 규칙이나 팩 바이트를 바꾸면 올립니다(모든 팩의 기록이 낡는다). 쿠커 소스 옆에서 사람이 올리는 상수다. */
        static constexpr uint32 kCookerVersion = 1;

        /** @brief 도메인(`engine` · `common` · `game/empty`)의 팩 파일 이름(`engine.pack` · `game_empty.pack`)입니다. 리더(`PackManager`)는 `_` 를 `/` 로 되읽는다. */
        static string makePackFileName( string_view domain );

        /**
         * @brief 도메인 기준 경로 @p relativePath 를 팩에 담을지 판정합니다(스테이징 산출물은 이 판정을 거치지 않는다).
         * @param shaderFolder 담을 셰이더 바이너리 폴더 — 다른 백엔드의 `shaders/bin/<폴더>` 는 뺀다
         * @param buildTarget 빌드 타깃 — 그 타깃이 빼는 에셋 종류는 뺀다(빈 값이면 빼지 않는다)
         */
        static bool shouldIncludeFile( string_view relativePath, const PackConfig& config, string_view shaderFolder, string_view buildTarget );

        /**
         * @brief 모든 도메인의 팩을 베이크합니다.
         * @param outPackCount 쓰거나 기록에서 다시 쓴 팩 수입니다.
         * @return 팩 하나라도 실패하면(이유는 오류로 남긴다) false 입니다. 굽을 도메인이 하나도 없어도 false 입니다.
         */
        [[nodiscard]] static bool cookAllPacks( const PackConfig& config, const PackCookParams& params, const CookStore& store, uint32& outPackCount );
    };
} // namespace sw
