/**
 * @file ShaderSource.h
 * @brief 셰이더 소스의 신원(`ShaderSourceID` — 리소스 경로), 그 소스의 쿠킹된 바이너리 자리(`ShaderBinaryLocation`), 소스와 include 를 디스크로 푸는 해석기(`ShaderSourceResolver` · `ShaderIncludeResolver`)입니다.
 * @details 예전에는 컴파일 요청의 경로 칸에 런타임은 리소스 id, 쿠커는 절대 경로를 넣고, "소스 → `shaders/bin/<rhi>`" 규칙을 세 곳(셰이더 캐시 · 리플렉션 매니페스트 · 쿠커)이
 *          글자 `shaders/` 를 찾아 따로 셈했다(`myshaders/` 같은 폴더에서 셋이 다른 답을 냈다). 이제 소스는 늘 리소스 경로이고, 바이너리 자리는 순수 함수 하나가 정한다.
 *          상용 엔진: 언리얼 가상 셰이더 경로(`/Engine/Private/…`)와 `AddShaderSourceDirectoryMapping`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/FileSystem/AssetID.h"

#include <optional>

namespace sw
{
    class AssetPaths;
    enum class ShaderTargetFormat : uint8;

    /**
     * @class ShaderSourceID
     * @brief 셰이더 소스 하나의 리소스 경로입니다(`<도메인>/…/shaders/<상대>.hlsl` — `engine/shaders/forward_lit.hlsl`). 빈 값은 "소스 없음" 입니다.
     * @details 디스크 경로가 아니다 — 문자열에서 암시적으로 만들 수 없어(`AssetID` 와 같다) 쿠커가 절대 경로를 넣던 자리가 컴파일을 지나지 못한다.
     *          경로 첫 칸(도메인) 뒤에 `shaders` 라는 폴더 칸이 있어야 한다(`getShadersFolder`). 그 폴더가 include 검색 · 쿠킹 스탬프 · 바이너리 자리의 기준이다.
     */
    class SW_API ShaderSourceID
    {
    public:
        ShaderSourceID() = default;
        /** @brief 리소스 경로 글 @p resourceID 를 셰이더 소스로 씁니다. 형식 검사는 쓰는 자리(`getShadersFolder` · 해석기)가 한다. */
        explicit ShaderSourceID( string_view resourceID )
            : _id{ resourceID }
        {
        }

        /** @brief 비었으면 true 입니다. */
        [[nodiscard]] bool isEmpty() const { return _id.isEmpty(); }
        /** @brief 리소스 id 입니다. */
        [[nodiscard]] const AssetID& id() const { return _id; }
        /** @brief 리소스 경로 글입니다. */
        [[nodiscard]] const string& str() const { return _id.str(); }
        /** @brief 리소스 경로 글(끝에 0)입니다. */
        [[nodiscard]] const utf8* c_str() const { return _id.c_str(); }

        /**
         * @brief 소스가 든 `shaders` 폴더의 리소스 경로입니다(`engine/shaders/sub/x.hlsl` → `engine/shaders`). 첫 `shaders` 폴더 칸까지이고 대소문자는 가리지 않습니다.
         * @return `shaders` 칸이 없거나 맨 앞(도메인 없음)이거나 그 뒤에 파일이 없으면 빈 값입니다 — 부르는 쪽이 오류로 멈춘다.
         */
        [[nodiscard]] std::optional<string_view> getShadersFolder() const;
        /** @brief 파일 이름의 스템을 소문자로 반환합니다(쿠킹된 바이너리 이름의 앞부분, `ShaderCooker::computeBinaryFileName` 의 입력). */
        [[nodiscard]] string getStemLower() const;

        [[nodiscard]] bool operator==( const ShaderSourceID& other ) const { return _id == other._id; }
        [[nodiscard]] bool operator!=( const ShaderSourceID& other ) const { return _id != other._id; }

    private:
        AssetID _id;
    };

    /**
     * @struct ShaderBinaryLocation
     * @brief 셰이더 소스 하나의 쿠킹된 바이너리 폴더입니다(`<shaders 폴더>/bin/<rhi>` — 리소스 경로). 쿠커는 쿠킹 스테이징의 같은 상대 자리에 쓰고, 런타임은 팩에서 이 id 로 읽습니다.
     * @details 규칙 하나다 — 셰이더 캐시(쿠킹본 읽기) · 리플렉션 매니페스트 · 쿠커 출력이 모두 이 함수로 자리를 정한다(예전에는 세 벌이 글자 `shaders/` 를 따로 찾았다).
     */
    struct SW_API ShaderBinaryLocation
    {
        string _binDirectory; ///< `engine/shaders/bin/dx12` 같은 리소스 경로(끝에 `/` 없음)

        /** @brief @p source 의 @p format 바이너리 폴더입니다. 입력만으로 정해진다. @p source 에 `shaders` 폴더 칸이 없으면 빈 값입니다. */
        [[nodiscard]] static std::optional<ShaderBinaryLocation> make( const ShaderSourceID& source, ShaderTargetFormat format );
        /** @brief 폴더 안 파일 @p fileName 의 리소스 id 입니다. */
        [[nodiscard]] AssetID makeFileID( string_view fileName ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCompileInput
     * @brief 해석이 끝난 컴파일 입력입니다 — 주 소스, 그 소스가 든 `shaders` 폴더, include 검색 순서(모두 절대 경로).
     * @details 컴파일러(DXC · FXC) · 쿠킹 계획 · 셰이더 캐시 지문이 이 값 하나를 쓴다. 예전에는 셋이 경로 글을 받아 저마다 "절대 경로면 그 파일, 아니면 리소스 id" 로 다시 풀었다.
     */
    struct ShaderCompileInput
    {
        AbsolutePath         _source;          ///< 주 소스 파일(`.hlsl`)
        AbsolutePath         _shadersFolder;   ///< 소스가 든 도메인의 `shaders` 폴더 — 쿠킹 스탬프 키 · 가림 검사의 기준
        vector<AbsolutePath> _listIncludeRoot; ///< `#include` 검색 순서: 소스 폴더 → include 루트 도메인(engine · common)의 `shaders`
    };

    /**
     * @struct ShaderIncludeRootDomain
     * @brief include 루트 도메인 하나입니다(`engine` → `Content/engine/shaders`). 디스크 자리는 리소스 경로 해석(`AssetPaths::toDiskPath`)이 정한다 — 붙인 디스크 도메인도 같은 규칙이다.
     */
    struct ShaderIncludeRootDomain
    {
        string       _domain;        ///< 도메인 이름(스탬프 헤더 키의 `<도메인>:` 앞부분)
        AbsolutePath _shadersFolder; ///< 그 도메인의 `shaders` 폴더
    };

    /**
     * @class ShaderIncludeResolver
     * @brief `#include` 를 푸는 규칙 하나입니다. DXC include 처리기 · FXC include 처리기 · 쿠킹 스탬프 · 가림 검사가 같은 루트 목록을 함께 씁니다.
     * @details 예전에는 include 루트를 셋이 따로 풀었다 — 컴파일러는 리소스 경로로, 스탬프 · 가림 검사는 "리소스 루트/<도메인>/shaders" 로 이어 붙여 붙인 디스크 도메인의 헤더가 스탬프에 안 보였다.
     *          값 형식이라 병렬 컴파일 · 쿠킹에서 함께 읽어도 된다(읽기 전용).
     */
    class SW_API ShaderIncludeResolver
    {
    public:
        explicit ShaderIncludeResolver( vector<ShaderIncludeRootDomain> listRootDomain );

        /** @brief include 루트 도메인입니다(검색 순서 — engine, common). */
        [[nodiscard]] const vector<ShaderIncludeRootDomain>& getRootDomains() const { return _listRootDomain; }
        /** @brief 이름 그대로 적힌 include @p includeName 을 @p input 의 검색 순서로 찾아 읽습니다(FXC — 이름만 준다). 못 찾으면 빈 값입니다. */
        [[nodiscard]] std::optional<AbsolutePath> findInclude( const ShaderCompileInput& input, string_view includeName, vector<uint8>& outBytes ) const;
        /**
         * @brief 컴파일러가 고른 후보 경로 @p candidatePath 를 읽습니다(DXC — 검색 폴더마다 후보를 묻는다). 엄격한 절대 경로인 정규 파일만 받습니다. 없는 후보는 로그 없이 빈 값입니다(다음 폴더로 넘어간다).
         */
        [[nodiscard]] std::optional<AbsolutePath> readInclude( string_view candidatePath, vector<uint8>& outBytes ) const;

    private:
        vector<ShaderIncludeRootDomain> _listRootDomain;
    };

    /**
     * @class ShaderSourceResolver
     * @brief `ShaderSourceID` 를 컴파일 입력(`ShaderCompileInput`)으로 푸는 해석기 하나입니다. include 규칙(`ShaderIncludeResolver`)을 품는다.
     * @details 리소스 경로 해석(`AssetPaths`)만 쓴다 — 작업 폴더 · 절대 경로 추측이 없다. 컴파일 · 쿠킹 계획 · 셰이더 캐시 · `--verify-shaders` 가 같은 해석기를 만든다.
     */
    class SW_API ShaderSourceResolver
    {
    public:
        /** @brief @p assetPaths 의 우선순위 설정에서 include 루트 도메인(engine · common)의 `shaders` 폴더를 풉니다. 설정에 없거나 폴더가 없으면 오류를 남기고 빈 값입니다. */
        [[nodiscard]] static std::optional<ShaderSourceResolver> make( const AssetPaths& assetPaths );

        /** @brief include 규칙입니다. */
        [[nodiscard]] const ShaderIncludeResolver& getIncludeResolver() const { return _includeResolver; }
        /** @brief @p source 를 풉니다. `shaders` 칸이 없거나 파일이 없거나 소스가 자기 `shaders` 폴더 밖으로 풀리면 @p outError 에 까닭을 담고 빈 값입니다. */
        [[nodiscard]] std::optional<ShaderCompileInput> resolve( const ShaderSourceID& source, string& outError ) const;
        /** @brief 도메인 @p domain 의 `shaders` 폴더입니다(있는 폴더만). */
        [[nodiscard]] std::optional<AbsolutePath> findDomainShadersFolder( string_view domain ) const;

    private:
        ShaderSourceResolver( const AssetPaths& assetPaths, ShaderIncludeResolver includeResolver );

        const AssetPaths*     _pAssetPaths;
        ShaderIncludeResolver _includeResolver;
    };
} // namespace sw
