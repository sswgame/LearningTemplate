/**
 * @file ShaderReflection.h
 * @brief 셰이더 바이트코드 리플렉션 데이터입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /// @brief CB · UBO 멤버입니다(이름, 오프셋, 크기).
    struct ShaderVariableInfo
    {
        string _name;
        /** @brief 머티리얼 패킹용 HLSL · SPIR-V 타입 라벨입니다(예: Float4, Uint, Bool). */
        string _type;
        uint32 _offset{ 0 };
        uint32 _size{ 0 };
    };

    /// @brief 상수 · 스토리지 버퍼 블록입니다.
    struct ShaderBufferInfo
    {
        string                     _name;
        vector<ShaderVariableInfo> _listVariable;
        uint32                     _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32                     _bindPoint{ 0 };     ///< 바인딩 슬롯(register bN / binding=N)
        uint32                     _totalSize{ 0 };
    };

    /// @brief 텍스처 · 샘플러 · UAV 바인딩 슬롯입니다.
    struct ShaderResourceBinding
    {
        string _name;
        string _type;
        uint32 _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32 _bindPoint{ 0 };
        uint32 _bindCount{ 0 };
    };

    /**
     * @brief 정점 셰이더 입력 하나입니다(시맨틱과 location).
     * @details DX 는 시맨틱 이름으로 정점 버퍼에 묶고, Vulkan · OpenGL 은 **선언 순서로 매긴 location** 으로 묶습니다.
     *          그래서 같은 HLSL 이 DX 에서는 맞고 두 백엔드에서만 다른 속성을 읽을 수 있습니다. 계약 검사가
     *          이 목록을 `constant::arrVertexAttribute` 와 대조합니다. 시스템 값(SV_VertexID 등)은 들어오지 않습니다.
     */
    struct ShaderVertexInputInfo
    {
        string _semantic;           ///< 시맨틱 이름(인덱스 제외, 예: "TEXCOORD")
        uint32 _semanticIndex{ 0 }; ///< 시맨틱 인덱스(TEXCOORD0 → 0)
        uint32 _location{ 0 };      ///< SPIR-V Location / DX 입력 시그니처 레지스터(선언 순서)
    };

    /// @brief 한 셰이더의 리플렉션 결과입니다(버퍼 + 바인딩).
    struct ShaderReflectionData
    {
        vector<ShaderBufferInfo>      _listConstantBuffer;
        vector<ShaderResourceBinding> _listResource;
        /// @brief 정점 스테이지의 사용자 입력(시맨틱 · location)입니다. 다른 스테이지는 비어 있습니다.
        vector<ShaderVertexInputInfo> _listVertexInput;
        /**
         * @brief StructuredBuffer<T> 의 **원소 레이아웃**입니다. 이름은 버퍼 변수 이름, 멤버는 T 의 필드, _totalSize 는 원소 stride 입니다.
         * @details GPUScene 머티리얼 데이터(g_SwMaterials)는 cbuffer 가 아니라 구조버퍼 원소라, 머티리얼 패커가 여기서
         *          오프셋과 stride 를 읽습니다. stride 는 원래 백엔드마다 다를 수 있습니다(DX 자연 패킹 / SPIR-V std430).
         *          지금은 SPIR-V 도 `-fvk-use-dx-layout` 으로 DX 규칙을 따르지만, 값은 여전히 그 백엔드의 리플렉션에서 읽습니다.
         */
        vector<ShaderBufferInfo> _listStructuredElement;
    };

    /// @brief DXC · SPIR-V 리플렉션으로 ShaderReflectionData 를 채웁니다.
    class SW_API ShaderReflection
    {
    public:
        /**
         * @brief 바이트코드에서 리플렉션 데이터를 뽑습니다.
         */
        static ShaderReflectionData reflect( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
    };
} // namespace sw
