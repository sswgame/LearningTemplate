/**
 * @file ShaderBindingValidatorIssue.h
 * @brief 쿠킹된 셰이더의 리플렉션이 바인딩 계약(binding_slots.hlsli)과 맞는지 검사합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"

namespace sw
{
    struct ShaderReservedBinding;
    enum class ShaderTargetFormat : uint8;

    /// @brief 계약 위반 하나입니다. 어느 리소스가 어떻게 어긋났는지 담습니다.
    struct SW_API ShaderBindingValidatorIssue
    {
        string _resource; ///< 리소스 이름(없으면 빈 문자열)
        string _message;  ///< 사람이 읽는 설명(기대값과 실제값 포함)

        // 바인딩 계약 검증기입니다.
        // 백엔드 불일치는 대개 "셰이더가 선언한 위치 ≠ 엔진이 바인딩하는 위치"이고, 어느 쪽도
        /**
         * @brief 한 스테이지의 리플렉션을 계약과 대조합니다.
         * @param reflection 바이트코드 리플렉션 결과
         * @param targetFormat 어느 백엔드용 바이너리인가. 같은 HLSL 도 백엔드마다 기대 위치가 다릅니다
         * @param shaderLabel 로그용 이름(경로 등)
         * @param pOutIssue 위반 목록을 받을 곳(선택)
         * @return 위반 수입니다. 0 이면 계약과 일치합니다. 위반은 SW_LOG_ERROR 로도 남깁니다.
         */
        static uint32 validate( const ShaderReflectionData& reflection, ShaderTargetFormat targetFormat, string_view shaderLabel,
                                vector<ShaderBindingValidatorIssue>* pOutIssue = nullptr );

        /**
         * @brief 형식 @p targetFormat 의 셰이더 단계 하나가 쓸 수 있는 텍스처 단위 수입니다(`Config/Engine/CookContract.json` 의 stage_texture_unit_limit).
         * @return 텍스처가 고정 칸이 아닌 백엔드(bindless 배열 · 기기마다 다른 한도)면 없음입니다.
         */
        static optional<uint32> findStageTextureUnitLimit( ShaderTargetFormat targetFormat );

        /// @brief 계약 테이블입니다. 예약 리소스 모두를 담습니다(테스트와 문서용).
        static const vector<ShaderReservedBinding>& getReservedBindings();
    };
} // namespace sw

namespace sw
{
    /// @brief 예약 리소스가 한 백엔드에서 있어야 할 위치입니다. `_bDeclared == false` 면 그 백엔드에는 선언 자체가 없어야 합니다.
    struct ShaderReservedLocation
    {
        uint32 _space{ 0 };         ///< DX: register space / Vulkan: descriptor set / GL: 항상 0
        uint32 _bind{ 0 };          ///< DX: register 번호 / Vulkan: binding / GL: binding(UBO, 텍스처 유닛, SSBO)
        bool   _bDeclared{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 예약 리소스 하나의 계약 위치입니다. 백엔드별로 "어디에 있어야 하는가"를 담습니다.
     * @details 값은 모두 ShaderBindingSlots.h(= binding_slots.hlsli)에서 옵니다. 네이티브 bindless(DX12/Vulkan)는
     *          종류별 무제한 배열 번호(= DX12 space = Vulkan binding)이고, 에뮬(DX11/GL)은 register 슬롯입니다.
     */
    struct ShaderReservedBinding
    {
        string                 _name; ///< 리소스 이름(칸 번호를 붙인 이름은 표가 지어 든다)
        ShaderBindingKind      _kind{ ShaderBindingKind::Unknown };
        ShaderReservedLocation _dx11;
        ShaderReservedLocation _dx12;
        ShaderReservedLocation _vulkan;
        ShaderReservedLocation _opengl;
    };
} // namespace sw
