/**
 * @file ShaderReflectionLibrary.h
 * @brief 오프라인에서 구운 셰이더 리플렉션 매니페스트 (RHI별 단일 파일)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

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

        /**
         * @brief 리플렉션을 구합니다 — 구운 매니페스트가 정본이고, 개발 빌드는 런타임 리플렉션으로 폴백합니다.
         * @details `tryGet` 은 "매니페스트에 있는가" 만 답한다. 없거나 소스보다 오래됐을 때 무엇을 할지는
         *          **정책**이고, 정책은 한 곳에만 있어야 한다. 예전엔 ShaderBindingLayoutCache 만 폴백을
         *          갖고 Material 은 그냥 XML 순서 패킹으로 남았다 — 그러면 셰이더는 24바이트 원소를 읽는데
         *          엔진은 256바이트(상수버퍼 크기) stride 로 올려, 한 배치의 두 번째 머티리얼부터 통째로
         *          어긋난다. 화면에는 "두 큐브가 같은 색" 으로, DX11 디버그 레이어에는
         *          "structure stride 256 vs 24" 로 나온다.
         *
         *          매니페스트 신선도 판정은 **초 단위**다(FileUtil::getFileTimestamp). 베이크 직후 소스를
         *          같은 초에 건드리기만 해도 RHI 폴더 넷 중 일부만 신선 판정을 받는 일이 실제로 있었다 —
         *          그래서 폴백이 없으면 백엔드마다 다른 결과가 나온다.
         * @return 리플렉션을 얻으면 true. Shipping 에서는 매니페스트에 없으면 false (런타임 컴파일 없음).
         */
        static bool getOrReflect( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );

        /** @brief 캐시를 비웁니다 (셰이더 재베이킹 후 등). */
        static void clearCache();
    };
} // namespace sw
