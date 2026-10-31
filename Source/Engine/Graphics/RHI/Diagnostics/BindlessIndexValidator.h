/**
 * @file BindlessIndexValidator.h
 * @brief 셰이더에 넘기기 직전의 bindless 인덱스가 살아 있는 슬롯인지 바인드 시점에 검사합니다(Debug와 Dev 진단용 — 모든 구성에서 컴파일되고 배포는 `GPUDiagnosticsBuild` 가 끈다).
 * @details 해제한 번호나 발급한 적 없는 번호를 셰이더가 읽으면 DX12 와 Vulkan 은 무효 디스크립터를 읽어 디바이스를 잃고, 로그에는
 *          "제출 실패" 한 줄만 남는다. 이 검사는 그 번호를 CPU 에서 미리 잡아 패스 이름, 필드 이름, 값을 오류 로그로 남긴다.
 *          생존 판정은 디바이스(`IRHIDevice::isBindlessIndexLive`)가, 언제 묻고 무엇을 남기는지는 이 클래스가 정한다. 백엔드는 별도 DLL 이라
 *          Engine 의 전역 변수를 읽지 못한다. 수준은 `-gv_validateBindlessIndex` 로 정한다. 0 은 끔, 1 은 죽은 번호(Debug 기본), 2 는 "없음" 번호가 셰이더로 가는 위치까지 남긴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    enum class RHIBindlessIndexSpace : uint8;

    class IRHIDevice;

    /** @brief 바인드 시점 검사에 필요한 정보입니다. 묻는 디바이스와 보고에 적을 패스 이름을 담습니다. 디바이스가 nullptr 이면 검사하지 않습니다. */
    struct BindlessIndexCheckSite
    {
        const IRHIDevice* _pDevice{ nullptr };
        hashed_string     _passName;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct BindlessIndexValidator
     * @brief 바인드 시점의 bindless 인덱스 생존 검사와 그 보고입니다. 같은 위치(패스, 필드, 원소)는 한 번만 남깁니다.
     * @details 병렬 기록 스레드에서 동시에 불러도 된다. 보고 기록은 잠금 하나가 지키고, 잠금은 죽은 번호를 찾았을 때만 잡는다.
     */
    struct SW_API BindlessIndexValidator
    {
        /** @brief 검사가 켜져 있으면(`-gv_validateBindlessIndex` 가 1 이상) true 입니다. 꺼져 있으면 부르는 쪽이 생존 질의 자체를 건너뜁니다. */
        static bool isEnabled();

        /** @brief "없음" 번호가 셰이더로 가는 자리까지 남기는 수준(`-gv_validateBindlessIndex=2`)이면 true 입니다. */
        static bool isTracingAbsent();

        /**
         * @brief @p index 가 `kInvalidDescriptorIndex` 이거나 @p space 에서 살아 있으면 true 입니다. 아니면 한 번 오류 로그를 남기고 false 입니다.
         * @param fieldName 셰이더 쪽 이름(`g_ShadowMapIndex`, `SwMaterials`)이며 로그에 그대로 적힌다
         * @param element   같은 이름의 몇 번째인가(머티리얼 텍스처 서수). 하나뿐이면 0
         */
        static bool validate( const BindlessIndexCheckSite& site, RHIBindlessIndexSpace space, RHIDescriptorIndex index, const hashed_string& fieldName,
                              uint32 element = 0 );

        /**
         * @brief 죽은 번호 하나를 보고합니다. 같은 위치(패스, 필드, 원소)를 처음 볼 때만 로그를 남기고 true 를 돌려줍니다.
         * @details `validate` 가 생존 질의 뒤에 부른다. 디바이스 없이 보고 규칙만 확인하는 테스트도 이것을 직접 부른다.
         */
        static bool report( RHIBindlessIndexSpace space, RHIDescriptorIndex index, const hashed_string& passName, const hashed_string& fieldName, uint32 element );

        /**
         * @brief "없음"(`kInvalidDescriptorIndex`)이 셰이더로 가는 위치 하나를 정보 로그로 남깁니다(`-gv_validateBindlessIndex=2`). 처음 볼 때만 true 입니다.
         * @details 셰이더는 그 번호를 분기로 걸러야 하는데, 드라이버가 디스크립터 읽기를 분기 앞으로 끌어올리면 배열 밖을 읽어 디바이스를 잃는다.
         *          손실이 났을 때 어느 패스와 필드가 후보인지 고르는 데 쓴다.
         */
        static bool noteAbsent( const hashed_string& passName, const hashed_string& fieldName, uint32 element );

        /** @brief 지금까지 오류 로그를 남긴(죽은 번호) 자리 수입니다(테스트와 진단용). */
        static uint32 getReportCount();

        /** @brief 지금까지 "없음" 정보 로그를 남긴 자리 수입니다(테스트와 진단용). */
        static uint32 getAbsentNoteCount();

        /** @brief 남긴 위치 기록(죽은 번호와 없음)을 비웁니다. 같은 위치를 다시 보고하게 됩니다(테스트용). */
        static void clearReports();
    };
} // namespace sw
