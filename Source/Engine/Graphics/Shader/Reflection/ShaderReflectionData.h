/**
 * @file ShaderReflectionData.h
 * @brief 셰이더 바이트코드 리플렉션 데이터입니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /**
     * @brief 바인딩 하나가 가리키는 리소스 종류입니다. 리플렉터(DX · SPIR-V · WebGPU)가 바로 이 열거로 내고, 바인딩 레이아웃 · 계약 검사가 그대로 읽는다.
     * @details 예전에는 리플렉터가 글자("Texture", "StorageBuffer", "UAV")로 내고 읽는 쪽이 글자를 비교해 다시 분류했다 — 철자 하나가 틀리면 조용히 `Unknown` 이었다.
     */
    ENUM()
    enum class ShaderBindingKind : uint8
    {
        ConstantBuffer,
        Texture,
        Sampler,
        StructuredBuffer,
        RwStructuredBuffer,
        RwTexture,
        Unknown
    };

    /**
     * @brief 셰이더 코드가 CB 멤버를 실제로 읽는지입니다(DXC · FXC 리플렉션의 `D3D_SVF_USED`).
     * @details SPIR-V · GLSL 리플렉션과 쿠킹한 매니페스트는 알려 주지 않아 `Unknown` 이다 — 그때 검사(패스 입력 계약 ⊆ 셰이더가 읽는 역할)는 하지 않는다.
     */
    enum class ShaderVariableUsage : uint8
    {
        Unknown = 0,
        Used,
        Unused,
    };

    /// @brief CB 와 UBO 멤버입니다(이름, 오프셋, 크기, 쓰임).
    struct ShaderVariableInfo
    {
        string _name;
        /** @brief 머티리얼 패킹용 HLSL 과 SPIR-V 타입 라벨입니다(예: Float4, Uint, Bool). */
        string              _type;
        uint32              _offset{ 0 };
        uint32              _size{ 0 };
        ShaderVariableUsage _usage{ ShaderVariableUsage::Unknown }; ///< 여러 스테이지를 합친 레이아웃에서는 한 스테이지라도 읽으면 `Used`
    };
} // namespace sw

namespace sw
{
    /// @brief 상수 버퍼와 스토리지 버퍼 블록입니다.
    struct ShaderBufferInfo
    {
        string                     _name;
        vector<ShaderVariableInfo> _listVariable;
        uint32                     _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32                     _bindPoint{ 0 };     ///< 바인딩 슬롯(register bN / binding=N)
        uint32                     _totalSize{ 0 };
    };
} // namespace sw

namespace sw
{
    /// @brief 텍스처, 샘플러, UAV 바인딩 슬롯입니다.
    struct ShaderReflectedBinding
    {
        string            _name;
        ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
        uint32            _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32            _bindPoint{ 0 };
        uint32            _bindCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 정점 셰이더 입력 하나입니다(시맨틱과 location).
     * @details DX 는 시맨틱 이름으로 정점 버퍼에 연결하고, Vulkan 과 OpenGL 은 **선언 순서로 매긴 location** 으로 연결합니다.
     *          그래서 같은 HLSL 이 DX 에서는 맞고 두 백엔드에서만 다른 속성을 읽을 수 있습니다. 계약 검사가
     *          이 목록을 `constant::render::getMeshVertexAttributes` 와 대조합니다. 시스템 값(SV_VertexID 등)은 들어오지 않습니다.
     */
    struct ShaderVertexInputInfo
    {
        string _semantic;           ///< 시맨틱 이름(인덱스 제외, 예: "TEXCOORD")
        uint32 _semanticIndex{ 0 }; ///< 시맨틱 인덱스(TEXCOORD0 → 0)
        uint32 _location{ 0 };      ///< SPIR-V Location / DX 입력 시그니처 레지스터(선언 순서)
    };
} // namespace sw

namespace sw
{
    /// @brief 한 셰이더의 리플렉션 결과입니다(버퍼와 바인딩).
    struct SW_API ShaderReflectionData
    {
        vector<ShaderBufferInfo>       _listConstantBuffer;
        vector<ShaderReflectedBinding> _listResource;
        /// @brief 정점 스테이지의 사용자 입력(시맨틱과 location)입니다. 다른 스테이지는 비어 있습니다.
        vector<ShaderVertexInputInfo> _listVertexInput;
        /**
         * @brief StructuredBuffer<T> 의 **원소 레이아웃**입니다. 이름은 버퍼 변수 이름, 멤버는 T 의 필드, _totalSize 는 원소 stride 입니다.
         * @details GPUScene 머티리얼 데이터(g_SwMaterials)는 cbuffer 가 아니라 구조 버퍼 원소라, 머티리얼 패커가 여기서
         *          오프셋과 stride 를 읽습니다. stride 는 원래 백엔드마다 다를 수 있습니다(DX 자연 패킹 / SPIR-V std430).
         *          지금은 SPIR-V 도 `-fvk-use-dx-layout` 으로 DX 규칙을 따르지만, 값은 여전히 그 백엔드의 리플렉션에서 읽습니다.
         */
        vector<ShaderBufferInfo> _listStructuredElement;

        /**
         * @brief 바이트코드에서 리플렉션 데이터를 뽑습니다(DXC · FXC · SPIR-V · WebGPU 꾸러미).
         * @return 빈 바이트코드 · 읽을 수 없는 형식 · 이 플랫폼에 없는 리플렉터는 오류입니다 — 빈 결과로 물러나지 않는다(빈 레이아웃으로 PSO 가 서면 슬롯이 조용히 빠진다).
         */
        [[nodiscard]] static expected<ShaderReflectionData> reflect( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
    };
} // namespace sw
