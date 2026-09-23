/**
 * @file ShaderReflectionLibrary.h
 * @brief 오프라인에서 구운 셰이더 리플렉션 매니페스트입니다(RHI 별 파일 하나).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

namespace sw
{
    struct ShaderCompileDesc;

    /**
     * @class ShaderReflectionLibrary
     * @brief 셰이더 바인딩 리플렉션을 **런타임이 아니라 쿠킹 시점에** 뽑아 두고 읽어 쓰는 라이브러리입니다.
     * @details DXIL 리플렉션은 `dxcompiler.dll`(IDxcUtils::CreateReflection)을 필요로 합니다. D3D12 에는
     *          독립 리플렉션 API 가 없습니다. 그것을 런타임에 하면 배포물에 셰이더 컴파일러를 같이 넣어야 하고,
     *          없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 갑니다(실제로 그랬습니다).
     *          상용 엔진(Unreal 의 셰이더 라이브러리 등)처럼, 바이트코드를 구울 때 리플렉션도 같이
     *          구워 배포하고 런타임은 읽기만 합니다.
     *
     *          파일은 **RHI 폴더마다 하나**입니다: `<domain>/shaders/bin/<rhi>/reflection.manifest`.
     *          셰이더마다 사이드카를 두면 팩 엔트리 · 조회 · 압축 해제가 셰이더 수만큼 늘어납니다. 변형이
     *          수천 개로 늘어도 파일 하나면 I/O 가 일정합니다.
     */
    class SW_API ShaderReflectionLibrary
    {
    public:
        /// @brief 매니페스트 한 개의 내용입니다(베이킹할 때 채우고 파일로 굽습니다).
        using EntryMap = unordered_map<string, ShaderReflectionData>;

        /** @brief 매니페스트 파일 이름입니다(RHI 폴더 안에 놓입니다). */
        static const utf8* getManifestFileName();

        /**
         * @brief 매니페스트를 파일로 굽습니다(베이커 전용).
         * @param absDirectory 매니페스트를 놓을 절대 디렉터리(`.../shaders/bin/<rhi>`)
         */
        static bool save( const EntryMap& mapEntry, string_view absDirectory );

        /**
         * @brief 셰이더 하나의 리플렉션을 조회합니다.
         * @details 매니페스트는 경로별로 한 번만 읽어 캐시합니다. 팩 · 낱개 파일을 가리지 않고 읽습니다.
         * @return 매니페스트에 없거나 (개발 빌드에서) 지금 소스와 맞지 않으면 false 입니다. 부르는 쪽이 폴백(개발 빌드 한정)을 정합니다.
         */
        static bool tryGet( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );

        /**
         * @brief 리플렉션을 구합니다. 구운 매니페스트가 기준이고, 개발 빌드는 런타임 리플렉션으로 폴백합니다.
         * @details `tryGet` 은 "쓸 수 있는 매니페스트 항목이 있는가" 만 답합니다. 없거나 낡았을 때 무엇을 할지는
         *          **정책**이고, 정책은 한 곳에만 있어야 합니다. 예전에는 ShaderBindingLayoutCache 만 폴백을
         *          갖고 Material 은 그냥 XML 순서 패킹으로 남았습니다. 그러면 셰이더는 24바이트 원소를 읽는데
         *          엔진은 256바이트(상수버퍼 크기) stride 로 올려, 한 배치의 두 번째 머티리얼부터 통째로
         *          어긋납니다. 화면에는 "두 큐브가 같은 색" 으로, DX11 디버그 레이어에는
         *          "structure stride 256 vs 24" 로 나옵니다.
         *
         *          매니페스트의 신선도는 `bake.stamp` 의 내용 해시로 봅니다(ShaderCache 와 같은 기준). 예전의 초 단위
         *          파일 시간 판정에서는 베이크 직후 소스를 같은 초에 건드리기만 해도 RHI 폴더 넷 중 일부만 신선 판정을
         *          받는 일이 실제로 있었습니다. 폴백이 없으면 그런 때 백엔드마다 다른 결과가 나옵니다.
         * @return 리플렉션을 얻으면 true 입니다. Shipping 에서는 매니페스트에 없으면 false 입니다(런타임 컴파일 없음).
         */
        static bool getOrReflect( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );

        /** @brief 캐시를 비웁니다(셰이더를 다시 구운 뒤 등). */
        static void clearCache();
    };
} // namespace sw
