/**
 * @file RHIRenderResource.h
 * @brief GPU 리소스를 든 CPU 객체의 공통 바탕입니다. 디바이스가 **죽기 전에** 통보를 받습니다.
 *
 * @details 언리얼의 `FRenderResource` 와 같은 구성입니다. RHI 리소스를 드는 객체는 태어날 때 전역 목록에 자기를 등록하고,
 *          디바이스 수명 이벤트(`IRHIDevice::shutdown` · 소멸)가 그 목록 전체에 **밀어 넣습니다.** 언리얼은 여기에 더해 RHI
 *          리소스가 참조 카운트(`TRefCountPtr`)라 핸들을 든 쪽이 있으면 리소스가 살아 있고 댕글링이 아예 불가능한데,
 *          우리 RHI 핸들은 생 `uint64` 라 그쪽은 네 백엔드 재작업이 필요합니다. 그래서 **통보 축만** 먼저 가져왔습니다.
 *
 *          이 패턴의 요점은 "죽은 뒤에 물어보지 않는다" 입니다. 예전에는 전역 세대 번호(`RHI::getDeviceGeneration()`)를 두고
 *          해제할 때마다 "내 디바이스가 아직 살아 있나" 를 **되물었습니다.** 그 질문이 필요했던 이유는 디바이스가 먼저 죽어도
 *          아무도 알려 주지 않았기 때문입니다. 지금은 죽기 전에 알려 주므로 질문도, 세대 번호도 없습니다.
 *
 * @note 두 가지 통보를 구분합니다. 이것이 이 파일에서 가장 중요한 구분입니다:
 *       - `releaseRhi( pDevice )`: 디바이스가 **아직 살아 있습니다.** 자기 GPU 리소스를 제대로 돌려주고 핸들을 비웁니다.
 *       - `forgetRhi( pDevice )`: 디바이스가 **이미 없습니다.** 돌려줄 곳이 없으니 핸들만 비웁니다(destroy 하면 해제 후 사용입니다).
 *       정상 경로는 언제나 전자입니다. 후자는 `shutdown()` 없이 사라진 디바이스에 대한 안전망입니다.
 *       되살리는 절반은 `initRhi( pDevice )` 이고, 새 디바이스가 선 직후 `initAllFor` 가 같은 목록에 밀어 넣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class RHIRenderResource
     * @brief GPU 리소스를 드는 객체가 상속합니다. 생성 즉시 전역 목록에 등록됩니다.
     * @note 등록·해제는 어느 스레드에서든 일어날 수 있어(메시는 게임 스레드, 업로드는 워커) 목록은 잠금으로 지킵니다.
     *       목록은 **소유하지 않습니다.** 수명은 각 객체의 것이고, 목록은 통보할 대상만 압니다.
     */
    class SW_API RHIRenderResource
    {
    public:
        RHIRenderResource();
        virtual ~RHIRenderResource();

        RHIRenderResource( const RHIRenderResource& )            = delete;
        RHIRenderResource& operator=( const RHIRenderResource& ) = delete;

        /**
         * @brief 살아 있는 디바이스에 GPU 리소스를 돌려주고 핸들을 비웁니다.
         * @param pDevice 이 리소스를 만들어 준 디바이스. 아직 유효합니다.
         */
        virtual void releaseRhi( IRHIDevice* pDevice ) = 0;

        /**
         * @brief 디바이스가 이미 사라졌을 때, 그 디바이스의 핸들만 비웁니다.
         * @param pDevice 사라진 디바이스. **내 것이 아니면 아무것도 하지 않아야 합니다.**
         * @details GPU 메모리는 디바이스가 내려가며 함께 갔습니다. 여기서 destroy 를 부르면 해제 후 사용입니다.
         *          디바이스 주소를 받는 이유는 `releaseRhi` 와 같습니다. 테스트처럼 디바이스가 여럿 살아 있는
         *          자리에서, 남이 죽었다고 내 핸들까지 비우면 멀쩡한 리소스를 잃습니다.
         */
        virtual void forgetRhi( IRHIDevice* pDevice ) = 0;

        /**
         * @brief 새 디바이스에 자기 GPU 리소스를 다시 만듭니다.
         * @details 언리얼 `FRenderResource::InitRHI` 와 같은 자리입니다. 기본은 아무것도 하지 않습니다. 그리는 순간에
         *          알아서 다시 올라가는 리소스는 여기 낄 이유가 없습니다.
         *          **이미 올라가 있으면 그대로 true 를 반환해야 합니다.** 통보 순서는 정해져 있지 않아서, 머티리얼이
         *          먼저 살아나며 자기 텍스처를 올려 놓은 뒤에 그 텍스처가 이 통보를 받는 일이 실제로 일어납니다.
         */
        virtual bool initRhi( IRHIDevice* pDevice );

        /** @brief 이 디바이스의 리소스를 든 객체 전부에게 `releaseRhi` 를 보냅니다. 디바이스가 살아 있을 때 부릅니다. */
        static void releaseAllFor( IRHIDevice* pDevice );

        /** @brief 이 디바이스의 리소스를 든 객체 전부에게 `forgetRhi` 를 보냅니다. 디바이스가 이미 사라진 뒤의 안전망입니다. */
        static void forgetAllFor( IRHIDevice* pDevice );

        /** @brief 등록된 객체 전부에게 `initRhi` 를 보냅니다. 디바이스가 새로 생긴 직후에 부릅니다. */
        static void initAllFor( IRHIDevice* pDevice );

        /** @brief 현재 등록된 객체 수입니다(진단·테스트용). */
        static uint32 getRegisteredCount();
    };
} // namespace sw
