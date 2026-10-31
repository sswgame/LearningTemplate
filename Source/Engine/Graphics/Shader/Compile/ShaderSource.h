/**
 * @file ShaderSource.h
 * @brief 셰이더 소스의 신원(`ShaderSourceID` — 리소스 경로)과 그 소스의 쿠킹된 바이너리 자리(`ShaderBinaryLocation`)입니다.
 * @details 예전에는 컴파일 요청의 경로 칸에 런타임은 리소스 id, 쿠커는 절대 경로를 넣고, "소스 → `shaders/bin/<rhi>`" 규칙을 세 곳(셰이더 캐시 · 리플렉션 매니페스트 · 쿠커)이
 *          글자 `shaders/` 를 찾아 따로 셈했다(`myshaders/` 같은 폴더에서 셋이 다른 답을 냈다). 이제 소스는 늘 리소스 경로이고, 바이너리 자리는 순수 함수 하나가 정한다.
 *          상용 엔진: 언리얼 가상 셰이더 경로(`/Engine/Private/…`)와 `AddShaderSourceDirectoryMapping`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"

#include <optional>

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /**
     * @class ShaderSourceID
     * @brief 셰이더 소스 하나의 리소스 경로입니다(`<도메인>/…/shaders/<상대>.hlsl` — `engine/shaders/forwardlit.hlsl`). 빈 값은 "소스 없음" 입니다.
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
