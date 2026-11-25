/**
 * @file ComputeShaderParameters.h
 * @brief 컴퓨트 셰이더 매개변수 구조체 — 셰이더가 선언한 자원을 이름으로 적고, PSO 를 만들 때 이름 → 레지스터 표를 한 번 풀어 둡니다.
 * @details 패스 몸통은 레지스터 번호를 적지 않는다(`IRHIPassCommandList` 의 번호 걸기는 비공개다). 셰이더 형식 하나가 매개변수 서수(enum)와
 *          이름 · 종류 표(`kParameters`)를 두고, PSO 생성이 리플렉션 레이아웃에서 그 표를 풀어 `ComputeShaderBindings` 로 PSO 옆에 둔다.
 *          기록은 서수로 값을 채우고(`ComputeShaderParameters::set`) 풀어 둔 번호로 건다 — 기록 중 이름 찾기 · 맵 조회가 없다.
 *          셰이더가 선언했는데 표에 없는 자원은 PSO 를 만들 때 오류로 멈춘다(그 PSO 는 만들지 않는다). 표에 있는데 셰이더(이 변형)가 읽지 않는 이름은 걸지 않는다.
 *          언리얼 셰이더 매개변수 구조체(`BEGIN_SHADER_PARAMETER_STRUCT` + `FShaderParameterBindings`)와 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/RHIDescriptorIndex.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIPassCommandList;
    class ShaderBindingLayout;
} // namespace sw

namespace sw
{
    /** @brief 매개변수 하나의 종류입니다 — 거는 명령과 리플렉션 종류가 갈린다. */
    enum class ComputeShaderParameterKind : uint8
    {
        ConstantBuffer, ///< `cbuffer` (b#)
        Buffer,         ///< `StructuredBuffer` · `ByteAddressBuffer` (t#)
        UAV,            ///< `RWStructuredBuffer` · `RWByteAddressBuffer` · `RWTexture2D` (u#)
    };

    /** @brief 셰이더 형식의 매개변수 표 한 줄입니다. `_ordinal` 은 그 형식의 `Parameter` 값이고 줄 번호와 같아야 한다(`isComputeShaderParameterTableOrdered`). */
    struct ComputeShaderParameterInfo
    {
        uint8                      _ordinal;
        const utf8*                _pName; ///< 셰이더의 선언 이름(리플렉션 이름)
        ComputeShaderParameterKind _kind;
    };

    /** @brief 셰이더 형식 하나의 매개변수 표입니다(그 형식의 `kParameters`). 표가 없는 셰이더(루트 상수 · bindless 만 읽는다)는 빈 표입니다. */
    struct ComputeShaderParameterTable
    {
        const ComputeShaderParameterInfo* _pInfo{ nullptr };
        uint32                            _count{ 0 };

        constexpr ComputeShaderParameterTable() = default;
        constexpr ComputeShaderParameterTable( const ComputeShaderParameterInfo* pInfo, uint32 count )
            : _pInfo{ pInfo }
            , _count{ count }
        {
        }
        template <size_t N>
        constexpr ComputeShaderParameterTable( const ComputeShaderParameterInfo ( &arrInfo )[N] )
            : _pInfo{ arrInfo }
            , _count{ static_cast<uint32>( N ) }
        {
        }
    };

    /** @brief 표의 줄 번호와 서수가 같은지입니다 — 셰이더 형식마다 `static_assert` 로 검사한다(서수로 값을 넣는 자리가 다른 줄을 채우지 않게). */
    template <size_t N>
    constexpr bool isComputeShaderParameterTableOrdered( const ComputeShaderParameterInfo ( &arrInfo )[N] )
    {
        for ( size_t index = 0; index < N; ++index )
        {
            if ( arrInfo[index]._ordinal != index )
                return false;
        }
        return true;
    }

    /**
     * @class ComputeShaderBindings
     * @brief 컴퓨트 PSO 하나에서 풀어 둔 매개변수 서수 → 레지스터 표입니다. PSO 를 만들 때 `resolve` 가 한 번 채우고 PSO 옆에 둡니다.
     * @details 패스 명령 면의 번호 걸기(`bindCompute*`)를 부를 수 있는 유일한 자리다(`IRHIPassCommandList` 의 벗).
     */
    class SW_API ComputeShaderBindings
    {
    public:
        static constexpr uint32 kMaxParameters = 12;
        static constexpr uint8  kAbsentSlot    = 0xFF; ///< 이 변형의 셰이더가 읽지 않는 매개변수

        ComputeShaderBindings();

        /**
         * @brief @p layout(컴퓨트 PSO 의 리플렉션)에서 @p table 의 이름을 풀어 @p outBindings 를 채웁니다.
         * @details 셰이더가 선언한 버퍼 · 상수 버퍼 · RW 자원(space 0, 배열이 아닌 것, 루트 상수 블록 밖)이 표에 없거나 종류가 다르면 오류를 남기고 false 다 —
         *          부르는 쪽은 그 PSO 를 쓰지 않는다(걸지 않은 칸을 셰이더가 읽는 모양을 만들 수 없다).
         * @param label 오류에 남길 셰이더 경로입니다.
         */
        [[nodiscard]] static bool resolve( const ShaderBindingLayout& layout, const ComputeShaderParameterTable& table, string_view label, ComputeShaderBindings& outBindings );

        /** @brief 이 표로 풀었는지입니다(셰이더 형식과 PSO 가 맞는지 기록이 본다). */
        bool isResolvedFor( const ComputeShaderParameterInfo* pInfo ) const { return _pInfo == pInfo; }

    private:
        /**
         * @brief @p pso 를 걸고 풀어 둔 번호로 @p pValues(서수 순, 무효 번호는 걸지 않는다)를 겁니다. 이 표가 @p pInfo 로 풀리지 않았으면 오류를 남기고 false 입니다.
         * @details 템플릿이 아닌 몸통이다 — 셰이더 형식마다 명령 면 헤더를 끌지 않는다.
         */
        [[nodiscard]] bool apply( IRHIPassCommandList& commands, RHIPipelineStateHandle pso, const ComputeShaderParameterInfo* pInfo, const uint32* pValues ) const;

    private:
        template <class TShader>
        friend class ComputeShaderParameters;

        const ComputeShaderParameterInfo* _pInfo{ nullptr };
        uint32                            _count{ 0 };
        uint8                             _arrSlot[kMaxParameters]; ///< 서수 → 레지스터(`kAbsentSlot` 은 걸지 않는다)
    };

    /**
     * @struct ComputePipeline
     * @brief 컴퓨트 PSO 핸들과 그 PSO 에서 풀어 둔 매개변수 표입니다(소유하지 않는 값). PSO 저장소가 내준다.
     */
    struct ComputePipeline
    {
        RHIPipelineStateHandle _pso{ 0 };
        ComputeShaderBindings  _bindings{};

        bool isValid() const { return _pso != 0; }
    };

    /**
     * @class ComputeShaderParameters
     * @brief 셰이더 형식 @p TShader 의 매개변수 값입니다 — 서수로 채우고 `apply` 가 PSO 를 걸고 풀어 둔 번호로 겁니다.
     * @details @p TShader 는 `enum Parameter : uint8 { …, Count }` 와 `static constexpr ComputeShaderParameterInfo kParameters[Count]` 를 둔다.
     *          채우지 않은 서수는 걸지 않는다(셰이더가 그 칸을 읽지 않는 실행 · 자리표가 필요 없는 칸).
     */
    template <class TShader>
    class ComputeShaderParameters
    {
    public:
        using Parameter = typename TShader::Parameter;
        static_assert( TShader::Count <= ComputeShaderBindings::kMaxParameters, "compute shader parameter table is larger than ComputeShaderBindings::kMaxParameters" );
        static_assert( isComputeShaderParameterTableOrdered( TShader::kParameters ), "kParameters rows must follow the Parameter order" );

        ComputeShaderParameters()
        {
            for ( uint32& value : _arrValue )
                value = static_cast<uint32>( kInvalidDescriptorIndex );
        }

        /** @brief 상수 버퍼 · 읽기 버퍼 매개변수를 채웁니다. */
        void set( Parameter parameter, RHIBufferSRVIndex value )
        {
            SW_ASSERT( TShader::kParameters[parameter]._kind != ComputeShaderParameterKind::UAV );
            _arrValue[parameter] = static_cast<uint32>( value );
        }
        /** @brief 쓰기(UAV) 매개변수를 채웁니다. */
        void set( Parameter parameter, RHIUnorderedAccessIndex value )
        {
            SW_ASSERT( TShader::kParameters[parameter]._kind == ComputeShaderParameterKind::UAV );
            _arrValue[parameter] = static_cast<uint32>( value );
        }

        /**
         * @brief @p pipeline 의 PSO 를 걸고 채운 매개변수를 겁니다(같은 PSO 를 다시 걸면 슬롯은 그대로다). PSO 가 이 셰이더 형식의 표로 풀리지 않았으면 오류로 걸지 않습니다.
         * @return 걸었으면 true — false 면 디스패치하지 않는다.
         */
        [[nodiscard]] bool apply( IRHIPassCommandList& commands, const ComputePipeline& pipeline ) const
        {
            return pipeline._bindings.apply( commands, pipeline._pso, TShader::kParameters, _arrValue );
        }

    private:
        uint32 _arrValue[TShader::Count]; ///< 서수 순 값(무효 번호는 걸지 않는다)
    };
} // namespace sw
