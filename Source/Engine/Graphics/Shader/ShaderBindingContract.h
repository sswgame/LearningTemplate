/**
 * @file ShaderBindingContract.h
 * @brief 구운 셰이더의 리플렉션이 바인딩 계약(bindingslots.hlsli)과 맞는지 검사합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /// @brief 계약 위반 하나 — 어느 리소스가 어떻게 어긋났는지.
    struct ShaderBindingContractIssue
    {
        string _resource; ///< 리소스 이름 (없으면 빈 문자열)
        string _message;  ///< 사람이 읽는 설명 (기대값/실제값 포함)
    };

    /**
     * @brief 예약 리소스 하나의 계약 위치 — 백엔드별로 "어디에 있어야 하는가".
     * @details 값은 전부 ShaderBindingSlots.h(= bindingslots.hlsli)에서 온다. 백엔드가 그 리소스를 선언하지
     *          않는 경우(예: Vulkan 은 네이티브 bindless 라 g_SwSlot# 이 없다) kNotDeclared 다.
     */
    struct ShaderReservedBinding
    {
        static constexpr uint32 kNotDeclared = 0xFFFFFFFFu;

        const utf8*       _name{ nullptr };
        ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
        uint32            _dxRegister{ 0 };           ///< DX11/DX12 register 번호 (b/t/u/s 는 _kind 가 정한다)
        uint32            _dxSpace{ 0 };              ///< DX12 register space (DX11 은 항상 0)
        uint32            _vkSet{ kNotDeclared };     ///< Vulkan descriptor set
        uint32            _vkBinding{ kNotDeclared }; ///< Vulkan binding
        uint32            _glBinding{ kNotDeclared }; ///< OpenGL binding (UBO / 텍스처 유닛 / SSBO — set 은 무시된다)
    };

    /**
     * @class ShaderBindingContract
     * @brief 바인딩 계약 검증기.
     * @details 지금까지의 백엔드 불일치는 전부 "셰이더가 선언한 위치 ≠ 엔진이 거는 위치" 였고, 어느 쪽도
     *          검증 에러를 내지 않아 화면이 검게 나오거나 비는 것으로만 드러났다. 이 검사는 그 어긋남을
     *          **바이너리를 읽는 순간**(PSO 레이아웃 빌드·베이킹·테스트) 에 이름과 숫자로 보고한다.
     *
     *          검사 항목 (백엔드별 이름공간 기준):
     *           1. 예약 리소스(PassCB, MaterialCB, g_SwInstances, g_SwSlot#, g_SwMaterialTex#, …)의 종류와
     *              (space/set, register/binding) 이 계약 표와 같은가. 그 백엔드에 없어야 할 선언이 있는가.
     *           2. 같은 이름공간에서 두 리소스가 한 자리를 차지하는가 — DX: (레지스터 종류, space, 번호),
     *              Vulkan: (set, binding), GL: (UBO/텍스처 유닛/SSBO, binding). GL 은 set 을 버리므로 여기서
     *              MaterialCB(set 10, binding 0) 가 PassCB(binding 0) 와 겹치던 사고가 잡힌다.
     *           3. Vulkan: 참조한 set 이 파이프라인 레이아웃 범위 안이고 그 set 의 디스크립터 타입과 맞는가.
     *           4. OpenGL: 모든 리소스가 set 0 인가 (0 이 아니면 작성자가 세트 의미를 가정한 것이다).
     */
    class SW_API ShaderBindingContract
    {
    public:
        /**
         * @brief 한 스테이지의 리플렉션을 계약과 대조합니다.
         * @param reflection 바이트코드 리플렉션 결과
         * @param targetFormat 어느 백엔드용 바이너리인가 — 같은 HLSL 도 백엔드마다 기대 위치가 다르다
         * @param shaderLabel 로그용 이름 (경로 등)
         * @param pOutIssue 위반 목록을 받을 곳 (선택)
         * @return 위반 수. 0 이면 계약과 일치. 위반은 SW_LOG_ERROR 로도 남긴다.
         */
        static uint32 validate( const ShaderReflectionData& reflection, ShaderTargetFormat targetFormat, string_view shaderLabel,
                                vector<ShaderBindingContractIssue>* pOutIssue = nullptr );

        /// @brief 계약 표 — 예약 리소스 전부 (테스트·문서용).
        static const vector<ShaderReservedBinding>& getReservedBindings();

        /// @brief 프로세스 시작 이후 validate 가 보고한 위반 누계 (앱/테스트가 "한 번도 없었는가" 를 확인할 때).
        static uint32 getTotalViolationCount();
    };
} // namespace sw
