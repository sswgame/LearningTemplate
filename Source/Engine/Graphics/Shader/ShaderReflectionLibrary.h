/**
 * @file ShaderReflectionLibrary.h
 * @brief 오프라인에서 구운 셰이더 리플렉션 매니페스트 (RHI별 단일 파일)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"

namespace sw
{
    /**
     * @class ShaderReflectionLibrary
     * @brief 셰이더 바인딩 리플렉션을 **런타임이 아니라 쿠킹 시점에** 뽑아 두고 읽어 쓰는 라이브러리.
     * @details DXIL 리플렉션은 `dxcompiler.dll`(IDxcUtils::CreateReflection)을 필요로 한다 — D3D12 에는
     *          독립 리플렉션 API 가 없다. 그걸 런타임에 하면 배포물에 셰이더 컴파일러를 같이 넣어야 하고,
     *          없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 간다(실제로 그랬다).
     *          상용 엔진(Unreal 의 셰이더 라이브러리 등)이 그렇듯, 바이트코드를 구울 때 리플렉션도 같이
     *          구워 배포하고 런타임은 읽기만 한다.
     *
     *          파일은 **RHI 폴더마다 하나**다 — `<domain>/shaders/bin/<rhi>/reflection.manifest`.
     *          셰이더마다 사이드카를 두면 팩 엔트리·조회·압축 해제가 셰이더 수만큼 늘어난다. 변형이
     *          수천 개로 늘어도 파일 하나면 I/O 가 일정하다.
     */
    class SW_API ShaderReflectionLibrary
    {
    public:
        /// @brief 매니페스트 한 개의 내용 (베이킹 시 채우고 파일로 굽는다).
        using EntryMap = unordered_map<string, ShaderReflectionData>;

        /** @brief 매니페스트 파일 이름 (RHI 폴더 안에 놓인다). */
        static const utf8* getManifestFileName();

        /**
         * @brief 매니페스트를 파일로 굽습니다 (베이커 전용).
         * @param absDirectory 매니페스트를 놓을 절대 디렉터리 (`.../shaders/bin/<rhi>`)
         */
        static bool save( const EntryMap& mapEntry, string_view absDirectory );

        /**
         * @brief 셰이더 하나의 리플렉션을 조회합니다.
         * @details 매니페스트는 경로별로 한 번만 읽어 캐시한다. 팩/낱개 파일 모두 투명하게 읽는다.
         * @return 매니페스트에 없으면 false — 호출부가 폴백(개발 빌드 한정)을 결정한다.
         */
        static bool tryGet( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );

        /** @brief 캐시를 비웁니다 (셰이더 재베이킹 후 등). */
        static void clearCache();
    };
} // namespace sw
