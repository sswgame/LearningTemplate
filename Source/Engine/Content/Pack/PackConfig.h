/**
 * @file PackConfig.h
 * @brief 팩 쿠킹 설정(`Config/Engine/PackConfig.json`)의 형식입니다. 값은 모두 그 파일에 있고 이 헤더는 형식만 가집니다.
 * @details 팩 쿠커(`PackCooker`, `App --cook-packs`)만 읽습니다. 배포물에는 들어가지 않습니다(`Config/` 는 팩 대상이 아니다).
 *          모든 칸이 `Required` 입니다 — 빠진 칸은 기본값으로 대신하지 않고 로드 오류입니다(예전 파이썬 쿠커의 `KeyError` 를 형식이 한다).
 *          - `_compression._codec` 은 `PackCompressionKind` 의 이름입니다(`Zlib` · `LZ4` · `Zstd` · `RLE` · `None`). 모든 코덱이 엔진 안에 있어 도구 설치와 관계없습니다.
 *            실측(1MB 모사 데이터): Zlib 기준 대비 LZ4 는 크지만 해제가 3배 빠르고, Zstd 는 가장 작다. `_level` 0 = 코덱 기본값.
 *          - 제외 패턴은 `StringUtil::matchesPathPattern`(에디터 임포트 규칙과 같은 마디 매칭: `/` 없는 패턴은 파일 이름에, 있으면 경로 마디별로, `*` 는 폴더를 넘지 않고 마디 `**` 만 넘는다. 대소문자 무시)으로 도메인 기준 경로에 맞춥니다. 제외 폴더는 경로 성분 이름입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Config/PackCookRuleTypes.h"
#include "Engine/Content/Pack/PackTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct PackEntryStorageRule
     * @brief 팩 항목의 저장 방식(`PackEntryStorage`)을 고르는 한 곳입니다 — 쿠커(`PackCooker`) · 모드 팩 빌더(`ModPackBuilder`)가 함께 부른다(패치 빌더는 새 빌드 팩이 고른 방식을 옮긴다).
     * @details 확장자가 @p listStoredExtension 에 있으면 `Stored`, 아니면 `Blocks`. 범위로 읽는 텍스처(`.dds` — 밉 스트리밍이 밉 구간만 읽는다, MR-3)가 `Blocks` 로 고르면 오류다:
     *          BC · ASTC 는 이미 압축이라 블록 압축의 이득이 작고 범위를 읽을 때마다 푸는 비용만 남는다(언리얼 IoStore 도 텍스처 벌크 데이터를 압축하지 않는 설정이 기본이다).
     *          빌더마다 규칙을 따로 쓰면 한 쪽만 어기는 모양이 생긴다 — 그래서 이 함수 하나다.
     */
    struct SW_API PackEntryStorageRule
    {
        static constexpr string_view kRangeReadExtension = ".dds"; ///< 밉 구간을 범위로 읽는 형식(`TextureMipLayout`)

        /** @brief 팩 안 경로 @p relativePath 의 저장 방식입니다. `.dds` 가 `Stored` 목록에 없으면 오류(`PackEntryStorageRule.CompressedTextureEntry`)를 남기고 실패입니다. */
        [[nodiscard]] static expected<PackEntryStorage> select( string_view relativePath, const vector<string>& listStoredExtension );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackCompressionConfig
     * @brief 팩 압축 코덱 · 레벨 · 블록 크기 · 쓰기 창입니다. 코덱은 팩 단위입니다(리더가 헤더의 코덱 하나로 모든 블록을 푼다).
     */
    REFLECT()
    struct SW_API PackCompressionConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        PackCompressionKind _codec{ PackCompressionKind::None }; ///< 팩 코덱

        PROPERTY( Required, Min = 0 )
        int32 _level{ 0 }; ///< 코덱 레벨(0 = 코덱 기본값)

        PROPERTY( Required, Min = 65536, Max = 262144 )
        uint32 _blockSize{ 0 }; ///< 압축 전 블록 크기(2 의 거듭제곱, `kPackMinBlockSize` ~ `kPackMaxBlockSize`). 범위 읽기의 단위이자 CRC 단위다 — 언리얼 IoStore 의 64 KiB 압축 블록 자리

        PROPERTY( Required, Min = 1 )
        uint64 _writeWindowBytes{ 0 }; ///< 쓰기가 이만큼의 원본을 모아 병렬로 압축한다. 쓰기의 최고 메모리가 이 값의 몇 배다(콘텐츠 크기와 무관)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackLayoutConfig
     * @brief 팩 페이로드 배치(`PackStreamWriter` 의 `PackLayoutMode`)와 슬롯 배치의 빈 자리 한도입니다.
     * @details `Slot` 은 같은 폴더에 앞 빌드의 팩이 있으면 그 팩을 앞 빌드로 읽어 안 바뀐 항목의 자리를 지킨다(쿠커가 덮어쓸 팩 경로를 앞 빌드로 준다).
     */
    REFLECT()
    struct SW_API PackLayoutConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        PackLayoutMode _mode{ PackLayoutMode::Sorted }; ///< 페이로드 배치

        PROPERTY( Required, Min = 0, Max = 100 )
        int32 _maxHolePercent{ 25 }; ///< `Slot` 에서 페이로드 영역의 빈 자리가 이 비율(%)을 넘으면 앞 빌드를 버리고 촘촘히 다시 배치한다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackChunkRule
     * @brief 이름 붙은 청크 하나입니다 — 패턴에 맞는 항목은 도메인의 기본 팩이 아니라 이 청크의 팩(`<도메인>.<청크>.<번호>.pack`)에 담는다(언리얼 IoStore 의 청크 · 컨테이너 자리).
     * @details 패턴은 도메인 기준 경로에 `StringUtil::matchesPathPattern` 으로 맞추고 규칙 순서대로 처음 맞는 것이 이긴다. 이름은 파일 이름에 들어가 글자 · 숫자 · `_` · `-` 만 쓴다.
     */
    REFLECT()
    struct SW_API PackChunkRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 청크 이름(`streaming_audio`) — 파일 이름의 한 마디

        PROPERTY( Required )
        vector<string> _listPattern; ///< 도메인 기준 경로 패턴(`audio/music/**`)
    };

    /**
     * @struct PackSplitConfig
     * @brief 도메인 팩을 여러 파일로 나누는 규칙입니다(`PackCooker::splitIntoParts`).
     * @details 청크마다 항목을 경로 해시 순으로 놓고 원본 바이트 합이 `_maxPackBytes` 를 넘기 전에 다음 번호의 팩으로 넘긴다 — 같은 입력이면 같은 나눔이다.
     *          항목 하나가 한도보다 크면 그 항목만 든 팩이 된다. 같은 도메인의 형제 팩은 항목이 겹치지 않는다(겹치면 마운트가 거절한다 — `PackManager`).
     */
    REFLECT()
    struct SW_API PackSplitConfig
    {
        REFLECT_BODY();

        /**
         * @brief 팩 파일 하나의 크기 한도입니다. 나누기는 항목마다 섹터로 올린 크기와 색인 줄로 어림해 몫을 고르고(`PackCooker::estimateEntryPackBytes`), 쓰기 끝에 실제 크기가 넘으면 오류다.
         * @details 형식의 한계가 아니다 — 페이로드 오프셋은 56 비트(`PackFileEntryOnDisk::kMaxDataOffset`)이고 32 비트 칸은 항목 하나의 크기(1 GiB 한도)뿐이다.
         *          설정 값 4 GiB − 1 은 FAT32 볼륨(SD 카드 · 외장 저장소)의 파일 크기 상한이다.
         */
        PROPERTY( Required, Min = 65536 )
        uint64 _maxPackBytes{ 0 };

        PROPERTY( Required )
        vector<PackChunkRule> _listChunkRule; ///< 이름 붙은 청크(빈 목록이면 모두 기본 청크). 기동 때 마운트한다

        /**
         * @brief 씬 청크로 미루는 씬 파일 패턴(리소스 id 기준 — `game/<게임>/maps/<이름>.scene.xml` 꼴의 와일드카드)입니다. 빈 목록이면 씬 청크가 없다(모두 기동 필수).
         * @details 맞는 씬 가운데 다른 뿌리(엔진 코드 · 게임 설정의 첫 씬 · 항상 담기)가 참조로 닿지 않는 씬마다 그 씬만 쓰는 에셋을 그 씬의 청크에, 둘 이상이 쓰는 것을
         *          공유 청크에 둔다(`PackSceneChunkPlan`). 그 청크는 기동 때 마운트하지 않고 씬을 열 때 마운트한다(`packchunks.xml` — `PackChunkLoader`).
         *          언리얼 청크 pak · 유니티 Addressables 의 씬 그룹 자리다.
         */
        PROPERTY( Required )
        vector<string> _listScenePattern;
    };

    /**
     * @struct PackExcludeRule
     * @brief 이름 붙은 제외 규칙 하나입니다(폴더 · 패턴).
     */
    REFLECT()
    struct SW_API PackExcludeRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 규칙 이름(로그 · 문서용)

        PROPERTY( Required )
        vector<string> _listExcludeDirectory; ///< 뺄 폴더 이름

        PROPERTY( Required )
        vector<string> _listExcludePattern; ///< 뺄 파일 패턴(빈 목록이면 패턴 제외 없음)

        PROPERTY( Required )
        bool _bRecursive{ SW_FALSE }; ///< 참이면 어느 깊이의 폴더든, 거짓이면 도메인 바로 아래 폴더만 뺀다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackRequiredDataPath
     * @brief 게임 설정 `<RequiredData kind pack>` 의 종류 하나가 팩 안에서 차지하는 자리(`data/items.xml` · 폴더 `data/appearance`)입니다.
     * @details 런타임은 종류 등록 줄의 경로 규칙으로 자리를 만든다(`GamePackDataUtil::tryMakeKindPath`). 팩 쿠킹은 게임 · 키트 모듈을 올리기 전에 돌아 그 등록을 보지 못하므로
     *          같은 자리를 데이터로 적는다(`datacataloginheritance.xml` 과 같은 까닭). 둘이 같은지는 `PackCookRulesTest.RequiredDataPathsMatchTheKindRegistry` 가 본다.
     */
    REFLECT()
    struct SW_API PackRequiredDataPath
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _kind; ///< 데이터 종류 이름(`SW_ASSET_TYPE` 의 이름 — `items`)

        PROPERTY( Required )
        string _path; ///< 팩 루트 기준 자리(파일이면 그 파일, 폴더면 그 아래 전부)
    };

    /**
     * @struct PackEntryCatalog
     * @brief 항목 단위로 고르는 카탈로그 파일 하나입니다 — 쓰는 항목이 가리키는 에셋만 담는다(파일 자체는 통째로 담긴다. 런타임이 표를 읽어 id 로 찾는다).
     * @details 파일을 통째로 쓰겠다고 말한 길(폴더 참조 · 항상 담기 · `#` 없는 파일 참조)로 닿으면 모든 항목이 쓰인다. 필수 데이터(`<RequiredData>`)나 `파일#id` 로 닿으면 이름이 불린 항목만 쓰인다.
     */
    REFLECT()
    struct SW_API PackEntryCatalog
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _kind; ///< 항목 id 의 이름 공간(`PackEntryIDField::_kind` 가 가리킨다)

        PROPERTY( Required )
        string _path; ///< 팩 루트 기준 파일(`data/items.xml`) — 어느 팩에 있든 같다

        PROPERTY( Required )
        string _entryElement; ///< 루트 아래 항목 요소 이름(`Item`)

        PROPERTY( Required )
        string _idAttribute; ///< 항목 id 속성(`id`)
    };

    /**
     * @struct PackCookRules
     * @brief 무엇을 담을지의 뿌리 규칙입니다 — 쓰는 것만 담는다(`PackReachResult`).
     * @details 담는 것은 뿌리(항상 담기 묶음 · 게임 설정의 필수 데이터)에서 참조 칸(`AssetReferenceFields`)과 곁 파일(`SW_ASSET_COMPANION`)을 따라 닿는 파일이다.
     *          언리얼 쿠커가 맵 · Primary Asset 에서 참조된 패키지만 쿠킹하는 것과 같다. 닿는 참조가 가리키는 파일이 없으면 쿠킹 오류다(조용히 빼지 않는다).
     */
    REFLECT()
    struct SW_API PackCookRules
    {
        REFLECT_BODY();

        PROPERTY( Required )
        vector<PackAlwaysCookRule> _listAlwaysCook; ///< 참조가 없어도 담는 묶음(뿌리)

        PROPERTY( Required )
        string _gameSettingsPath; ///< 게임 팩 루트 기준 게임 설정 파일(`data/gamesettings.xml`) — 그 `<RequiredData>` 가 다른 팩의 데이터를 뿌리로 더한다

        PROPERTY( Required )
        vector<PackRequiredDataPath> _listRequiredData; ///< 필수 데이터 종류의 팩 안 자리. 게임 설정이 이 표에 없는 종류를 적으면 쿠킹 오류다

        PROPERTY( Required )
        vector<string> _listUnfollowedField; ///< 따라가지 않는 경로 칸 — 런타임이 읽지 않는 개발 도구 입력(현지화 수집 뿌리 `assetRoots`)

        PROPERTY( Required )
        vector<PackEntryCatalog> _listEntryCatalog; ///< 항목 단위로 고르는 카탈로그

        PROPERTY( Required )
        vector<PackEntryIDField> _listEntryIDField; ///< 카탈로그 항목을 id 로 가리키는 칸

        /**
         * @brief 게임 하나의 쿠킹 규칙(`GameConfig::_cookRules`)을 이 규칙 뒤에 합칩니다 — 언리얼 `DefaultGame.ini` 의 쿠킹 설정이 엔진 기본을 덮는 모양이다.
         * @details 합치기 규칙은 여기 하나다. 합친 규칙은 팩 단위다(`PackRuleScope`) — 모든 게임을 한 번에 구워도 팩마다 "엔진 규칙 + 그 게임 규칙만" 으로 판정한다.
         *          - 항상 담기: 게임 규칙의 패턴은 그 게임 팩 루트(@p gamePackRoot, `game/nilecity`) 기준이라 앞에 루트를 붙이고, 그 게임 팩에만 닿는다.
         *          - 엔진 규칙과 이름(항상 담기) · 칸 이름(id 칸)이 같으면 게임 쪽이 이긴다 — 엔진 규칙은 그 게임 팩에서만 물러나고 다른 팩에는 그대로 닿는다.
         */
        void mergeGameRules( string_view gamePackRoot, const GameCookRules& gameRules );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackConfig
     * @brief `Config/Engine/PackConfig.json` 이 담는 팩 쿠킹 설정입니다. 파일 머리말 참고.
     * @details `ConfigBaseLayer` 라 파일이 모든 키를 적는다(`ConfigFileSchemaTest`).
     */
    REFLECT( Meta = "ConfigBaseLayer" )
    struct SW_API PackConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 프로젝트 루트 기준 설정 파일 경로입니다. */
        static constexpr const utf8* kConfigPath = "Config/Engine/PackConfig.json";

        /**
         * @brief 셰이더 원본(`.hlsl` · `.hlsli`)인가입니다 — `_bExcludeRawShaderSource` 가 켜진 팩(배포)에는 없고 쿠킹한 바이너리만 있다.
         * @details 팩 쿠커가 뺄 때와 기본 에셋 검사가 "배포 팩에 없는 것이 맞다" 고 볼 때 같은 판정을 쓴다(확장자를 두 곳에 적지 않는다).
         */
        static bool isRawShaderSource( string_view path ) { return StringUtil::endsWith( path, ".hlsl", true ) || StringUtil::endsWith( path, ".hlsli", true ); }

        PROPERTY( Required )
        PackCompressionConfig _compression; ///< 팩 압축

        PROPERTY( Required )
        PackLayoutConfig _layout; ///< 페이로드 배치(항목 추가 · 삭제에 뒤 항목이 밀리지 않게 하는 슬롯 배치)

        PROPERTY( Required )
        PackSplitConfig _split; ///< 도메인 팩 나누기(청크 · 크기 한도)

        PROPERTY( Required )
        bool _bExcludeRawShaderSource{ false }; ///< 셰이더 원본(`.hlsl` · `.hlsli`)을 팩에서 뺀다 — 배포물은 쿠킹한 바이너리만 읽는다

        PROPERTY( Required )
        vector<string> _listGlobalExcludeDirectory; ///< 어느 깊이에서든 뺄 폴더 이름

        PROPERTY( Required )
        vector<string> _listGlobalExcludePattern; ///< 뺄 파일 패턴

        PROPERTY( Required )
        vector<PackExcludeRule> _listRule; ///< 이름 붙은 제외 규칙

        PROPERTY( Required )
        PackCookRules _cookRules; ///< 담을 뿌리 규칙 — 제외를 지난 파일 가운데 뿌리에서 참조로 닿는 것만 담는다
    };
} // namespace sw
