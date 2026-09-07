/**
 * @file ShaderReflection.h
 * @brief 셰이더 바이트코드 리플렉션 데이터
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /// @brief CB/UBO 멤버 (이름, 오프셋, 크기)
    struct ShaderVariableInfo
    {
        string _name;
        /** @brief HLSL/SPIR-V type label for Material packing (e.g. Float4, Uint, Bool). */
        string _type;
        uint32 _offset{ 0 };
        uint32 _size{ 0 };
    };

    /// @brief 상수/스토리지 버퍼 블록
    struct ShaderBufferInfo
    {
        string                     _name;
        vector<ShaderVariableInfo> _listVariable;
        uint32                     _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32                     _bindPoint{ 0 };     ///< 바인딩 슬롯 (register bN / binding=N)
        uint32                     _totalSize{ 0 };
    };

    /// @brief 텍스처/샘플러/UAV 바인딩 슬롯
    struct ShaderResourceBinding
    {
        string _name;
        string _type;
        uint32 _registerSpace{ 0 }; ///< DX12 register space / Vulkan descriptor set
        uint32 _bindPoint{ 0 };
        uint32 _bindCount{ 0 };
    };

    /// @brief 한 셰이더의 리플렉션 결과 (버퍼+바인딩)
    struct ShaderReflectionData
    {
        vector<ShaderBufferInfo>      _listConstantBuffer;
        vector<ShaderResourceBinding> _listResource;
        /**
         * @brief StructuredBuffer<T> 의 **원소 레이아웃** — 이름은 버퍼 변수 이름, 멤버는 T 의 필드, _totalSize 는 원소 stride.
         * @details GPUScene 머티리얼 데이터(g_SwMaterials)는 cbuffer 가 아니라 구조버퍼 원소라, 머티리얼 패커가 여기서
         *          오프셋과 stride 를 읽는다. stride 는 백엔드마다 다르다(DX 자연 패킹 / SPIR-V std430).
         */
        vector<ShaderBufferInfo> _listStructuredElement;
    };

    /// @brief DXC/SPIR-V 리플렉션을 ShaderReflectionData로 채움
    class SW_API ShaderReflection
    {
    public:
        /**
         * @brief 바이트코드에서 리플렉션 데이터를 추출합니다
         */
        static ShaderReflectionData reflect( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
    };
} // namespace sw
