/**
 * @file IRHITimestampProvider.h
 * @brief 디바이스의 선택 능력인 GPU 타임스탬프 계측입니다. 이 능력을 가진 디바이스만 구현하고, 쓰는 쪽은 `IRHIDevice::findTimestampProvider` 로 묻습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct RHITimestampFrame;

    /**
     * @class IRHITimestampProvider
     * @brief GPU 타임스탬프 쿼리와 GPU 시계 읽기입니다. 기록은 `IRHICommandList::writeTimestamp` 가 합니다.
     * @details 디바이스가 이 인터페이스를 함께 상속하고, 생성자에서 `IRHIDevice::_pTimestampProvider` 에 자기를 적는다.
     *          쓰는 쪽은 `findTimestampProvider()` 가 nullptr 이면 계측을 건너뛴다. 묻는 비용은 멤버 하나를 읽는 것뿐이다.
     *          계측 정책(언제 켜는가)은 엔진이 정하고, 이 인터페이스는 메커니즘만 제공한다. 백엔드는 별도 모듈이라 엔진 전역(프로파일러)을 볼 수 없다.
     */
    class IRHITimestampProvider
    {
    public:
        /**
         * @brief 타임스탬프 계측을 켜고 끕니다. **끄면 백엔드는 아무 자원도 만들지 않고 아무것도 읽지 않습니다.**
         * @details 계측에는 비용이 든다. 쿼리 힙과 풀이 필요하고, 프레임마다 리셋, resolve, 읽기가 따라붙는다.
         *          그래서 "측정할 사람이 있을 때만" 켠다. 켠 다음 프레임부터 슬롯이 열린다(자원은 그때 만든다).
         */
        virtual void setTimestampEnabled( bool bEnabled ) = 0;

        /**
         * @brief 이번 프레임에 쓸 수 있는 타임스탬프 슬롯 수입니다. 0 이면 드라이버가 지원하지 않거나 아직 자원을 만들지 않았습니다.
         * @details 슬롯은 프레임마다 0 부터 다시 쓴다. 기록은 `IRHICommandList::writeTimestamp` 가 한다.
         */
        virtual uint32 getTimestampSlotCount() const = 0;

        /**
         * @brief **이미 끝난 프레임**의 타임스탬프를 읽습니다. 슬롯마다 기준점 이후의 마이크로초와 기준점의 GPU 시계(나노초)가 들어 있습니다.
         * @return 읽을 값이 있으면 true 입니다. GPU 가 아직 끝내지 않았으면 false 이며, 다음 프레임에 다시 물으면 됩니다.
         * @details 결과는 몇 프레임 늦는다. **기다리지 않는다.** 기다리면 측정하려던 그 파이프라인을
         *          멈춰 세워 측정값이 거짓이 된다.
         *          이번 프레임에 기록하지 않은 슬롯은 **음수**로 돌아온다. 백엔드마다 기록하지 않은 슬롯에 남는
         *          값이 다르기 때문이다(DX12 와 DX11 은 지난 사이클 값, Vulkan 과 GL 은 미가용).
         *          호출하는 쪽은 음수가 하나라도 섞인 구간을 통째로 버려야 한다.
         *          같은 값이 엔진 테이블(`GPU.<패스>`)과 외부 프로파일러(Tracy GPU 타임라인)로 간다. 쿼리는 한 벌이다.
         */
        [[nodiscard]] virtual bool readTimestamps( RHITimestampFrame& outFrame ) = 0;

        /**
         * @brief GPU 시계의 **현재** 값을 타임스탬프와 같은 영역의 나노초로 읽습니다. 외부 프로파일러가 GPU 시각을 CPU 시계에 맞출 때 씁니다.
         * @return 읽지 못하면 false 입니다.
         * @details 값은 이 함수가 돌아오는 순간의 GPU 시계다. 백엔드마다 비용이 다르다(`isGPUClockReadCheap` 참고).
         *          렌더 스레드(GL 은 컨텍스트를 가진 스레드)에서 부른다.
         */
        [[nodiscard]] virtual bool readGPUClockNanos( int64& outGPUNanos ) = 0;

        /**
         * @brief `readGPUClockNanos` 가 기다리지 않으면 true 입니다(DX12 `GetClockCalibration`, GL `GL_TIMESTAMP`).
         * @details false 인 백엔드(DX11, Vulkan)는 타임스탬프를 하나 내고 GPU 가 그것을 지날 때까지 기다린다. 그래서 컨텍스트를 열 때 한 번만 부르고
         *          주기적으로 다시 맞추지 않는다.
         */
        virtual bool isGPUClockReadCheap() const = 0;

    protected:
        /** @brief 이 인터페이스로는 지우지 않습니다. 수명은 디바이스가 정합니다. */
        ~IRHITimestampProvider() = default;
    };
} // namespace sw
