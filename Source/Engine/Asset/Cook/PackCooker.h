/**
 * @file PackCooker.h
 * @brief 도메인 폴더(`engine` · `common` · `game/<팩>`)마다 팩 하나(`engine.pack` · `common.pack` · `game_<팩>.pack`)를 베이크합니다. `App --cook-packs` · `--cook-all` 이 부릅니다.
 * @details 무엇을 담을지(이 파일)와 어떤 바이트가 되는지(`PackWriter`)를 나눈다. 쿠킹은 엔진 언어 한 곳이다 — 예전 파이썬 쿠커(`CookAssets.py`)가
 *          레이아웃 · 압축 · 증분 기록을 따로 들던 것을 옮겼다.
 *          - 담는 것(쓰는 것만): 도메인 폴더의 파일 중 `PackConfig` 의 제외(전역 · 규칙별 폴더 · 패턴), 셰이더 원본, 다른 백엔드의 셰이더 바이너리, 빌드 타깃이 빼는
 *            종류(`AssetPaths::isExcludedForTarget` — 런타임이 같은 표로 읽지 않는다)를 지나고, **뿌리에서 참조로 닿는 것**(`PackReachResult` — 뿌리 규칙은
 *            `PackConfig::_cookRules`)과, 스테이징 폴더(`<쿠킹 폴더>/<도메인>`)의 쿠킹 산출물. 씬 · 프리팹 쿠킹본은 그 원본이 담길 때만 담고, 셰이더 바이너리 같은 나머지 산출물은 다 담는다.
 *            고르기는 굽는 모든 도메인을 한 번에 본다 — `common` 은 굽는 게임들이 쓰는 것의 합이다(`-game=` 이면 그 게임이 쓰는 것만). 예전에는 폴더째 담아
 *            `common.pack` 이 게임이 쓰지 않는 캐릭터 · 소품까지 들었다(359MB, 빈 게임 팩은 6MB).
 *            스테이징은 도메인 기준 **같은 상대 경로**로 들어간다. 소스 트리에 남은 쿠킹본(`AssetCookPath::isCookedOutput`)은 낡은 잔재라 넣지 않고 이름을 알린다.
 *          - 증분: 팩 하나가 쿠킹 작업 하나다(`CookStore::cookFile`). 입력은 읽은 파일마다의 내용 해시와 두 폴더의 목록이고, 설정(`PackConfig` 전체 · 셰이더 폴더 ·
 *            빌드 타깃)과 **그 도메인에서 고른 파일 목록**은 작업 키의 설정 해시다 — 다른 팩의 참조가 바뀌어 고른 것이 달라지면 다시 굽고, 같으면 다시 쓰지 않는다.
 *            고르기는 팩 작업들 앞의 범위 하나(`PackReach`)에서 읽는다(쿠킹 세션은 작업 밖 읽기를 막는다). 무변경 재쿠킹은 팩을 다시 쓰지 않는다.
 *          - 요약: 고르기가 도메인마다 담은 수 · 크기와 후보 수 · 크기를 한 줄씩 남긴다(`PackCooker.PackReachDomain`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cook/PackReachResult.h"

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
        string                _projectRoot;                                          ///< 저장소 루트의 절대 경로 — 참조 칸 표(`AssetReferenceFields::kConfigFile` · `kGeneratedFile`)를 읽는다
        string                _resourceRoot;                                         ///< `Content/` 의 절대 경로
        string                _cookedDir;                                            ///< 씬 · 프리팹 · 레지스트리 쿠킹 산출물의 스테이징 루트(`--cooked-dir`)
        string                _packOutputDir;                                        ///< 팩을 쓸 폴더(`--pack-dir`)
        string                _shaderFolder;                                         ///< 담을 셰이더 바이너리 폴더(`dx12` 등, 쿠킹 표의 `shader_folder`)
        string                _buildTarget;                                          ///< 빌드 타깃(`Game` · `Client` · `Server`) — 그 타깃이 빼는 종류를 팩에 넣지 않는다
        PackAutomationContent _automationContent{ PackAutomationContent::Excluded }; ///< 자동화 콘텐츠(시나리오)를 담는가 — 쿠킹하는 구성에 자동화 실행기가 있으면 `Included`
        string                _gamePackFolder;                                       ///< 비우면 `Content/game/` 의 모든 팩, 아니면 그 폴더(`empty`) 하나만 베이크한다
        /**
         * @brief 담지 않을 소스 파일의 절대 경로(`FileUtil::normalizePath`)입니다. 비어 있을 수 있다.
         * @details 이 층(Resource)이 읽을 수 없는 규칙으로 위 층이 정한다 — 출시 문화권(`stagedCultures`) 밖의 번역 테이블(`LocalizationProject::collectUnstagedTranslationTables`).
         *          목록은 쿠킹 설정 해시에 들어가 바뀌면 팩을 다시 베이크한다.
         */
        vector<string> _listExcludedSourceFile;
        /**
         * @brief 셰이더 쿠킹 요청이 만드는 바이너리의 스테이징 기준 id(`engine/shaders/bin/dx12/<파일>`, 정규화) → 출처 묶음입니다 — 위 층(렌더러의 요청 수집, `ShaderCookRequest::_listOrigin`)이 채운다.
         *        셰이더가 없는 구성(전용 서버)은 비어 있다.
         * @details 출처 묶음 하나의 에셋 id 가 모두 닿았는데(빈 묶음은 늘) 원본이 닿지 않아 팩에서 버리는 것은 경고(`PackCooker.PackCookDroppedRequestedShader`)다 — 런타임이 찾을 셰이더가
         *          팩에 없다. 닿지 않은 출처만 요청한 것(쓰지 않는 개발 파이프라인)과 요청 밖 바이너리는 정보 줄이다.
         */
        unordered_map<string, vector<vector<string>>> _mapRequestedShaderBinary;
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
        /** @brief 도메인(`engine` · `common` · `game/empty`)의 팩 파일 이름(`engine.pack` · `game_empty.pack`)입니다. 도메인은 이름이 아니라 팩 안(마운트 도메인)에 적는다. */
        static string makePackFileName( string_view domain );
        /**
         * @brief 팩 하나의 쿠킹 기록 id(`packs/<타깃>/<스테이징 해시>/<팩 이름>`)입니다.
         * @details 기록 자리(`<프로젝트>/Saved/CookRecord`)는 프리셋이 함께 쓰는데 팩의 입력인 스테이징(`--cooked-dir`)은 프리셋마다 다르다 — 스테이징마다 기록을 따로 둔다.
         *          나눠 쓰면 다른 프리셋의 기록이 새것이라 이 스테이징을 보지 않고 그 팩을 DDC 에서 되살렸다(첫 배포 굽기의 빠진 layer_bake).
         */
        static string makeRecordID( string_view target, string_view cookedDir, string_view packName );
        /**
         * @brief 바이너리 @p assetID 를 요청한 출처 묶음(`PackCookParams::_mapRequestedShaderBinary`) 가운데 에셋이 모두 @p setIncluded 에 든 것이 있으면 true 입니다(빈 묶음은 늘 든다).
         * @details 참이면 팩이 그 바이너리를 버릴 때 경고다 — 닿은 파이프라인 · 머티리얼이 쓰는데 원본 셰이더가 리치에서 빠졌다(참조 칸 표의 빈틈).
         */
        [[nodiscard]] static bool isRequestedByReachedOrigin( const unordered_map<string, vector<vector<string>>>& mapRequested, const unordered_set<string>& setIncluded, const string& assetID );

        /**
         * @brief 도메인 기준 경로 @p relativePath 를 팩에 담을지 판정합니다(스테이징 산출물은 `shouldIncludeStagedFile` 이 본다).
         * @param shaderFolder 담을 셰이더 바이너리 폴더 — 다른 백엔드의 `shaders/bin/<폴더>` 는 뺀다
         * @param buildTarget 빌드 타깃 — 그 타깃이 빼는 에셋 종류는 뺀다(빈 값이면 빼지 않는다)
         */
        static bool shouldIncludeFile( string_view relativePath, const PackConfig& config, string_view shaderFolder, string_view buildTarget );
        /**
         * @brief 스테이징 산출물(도메인 기준 @p relativePath)을 팩에 담을지 판정합니다 — 빌드 타깃이 빼는 종류와 다른 백엔드의 셰이더 바이너리만 뺀다.
         * @details 셰이더 쿠킹본도 스테이징에 있다(`<쿠킹 폴더>/<도메인>/shaders/bin/<백엔드>`, 쿠커는 모든 백엔드를 굽는다).
         */
        static bool shouldIncludeStagedFile( string_view relativePath, string_view shaderFolder, string_view buildTarget );

        /**
         * @brief 모든 도메인의 팩을 베이크합니다.
         * @param outPackCount 쓰거나 기록에서 다시 쓴 팩 수입니다.
         * @return 팩 하나라도 실패하면(이유는 오류로 남긴다) false 입니다. 굽을 도메인이 하나도 없어도 false 입니다.
         */
        [[nodiscard]] static bool cookAllPacks( const PackConfig& config, const PackCookParams& params, const CookStore& store, uint32& outPackCount );
    };
} // namespace sw
