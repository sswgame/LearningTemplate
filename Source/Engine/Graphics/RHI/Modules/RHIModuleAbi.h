/**
 * @file RHIModuleAbi.h
 * @brief RHI MODULE 의 C ABI 버전과 스탬프입니다(Engine 과 RHI_* 가 일치해야 합니다).
 *
 * IRHIDevice / IRHIResource / IRHICommandList / IRHICommandContext 의 public 기록 표면이나
 * 레이아웃(멤버 · 가상 함수)이 바이너리 비호환으로 바뀌면 kRHIModuleAbiVersion 과 kRHIModuleAbiStamp 를
 * **함께** 올립니다(둘이 어긋나면 컴파일되지 않습니다). Engine 과 함께 모든 RHI_* 모듈을 다시 빌드하십시오.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    /** @brief 숫자 버전입니다. 도장의 `v<N>` 과 같아야 합니다(아래 static_assert 가 견줍니다). */
    inline constexpr uint32 kRHIModuleAbiVersion = 25;
    /**
     * @brief 불투명 표면 지문입니다. 커맨드 리스트 · 디바이스 ABI 가 바뀌면 `v<N>` 을 올리고 문자열을 바꿉니다.
     * @details 올려야 하는 변경 — 어느 것이든 vtable 이나 레이아웃이 바뀝니다:
     *          - IRHIDevice · IRHIResource · IRHICommandList · IRHICommandContext 의 가상 함수를 더하거나 빼거나 시그니처를 바꿀 때
     *          - 가상 ↔ 비가상 전환(`waitIdle` · `shutdown` 은 비가상 템플릿 메서드이고 백엔드는 `...Internal` 훅만 채웁니다)
     *          - 경계를 넘는 구조체(RHITextureDesc · RHITextureUploadDesc · RHIRenderPassBeginInfo · RHINativeHandles 등)의 필드가 바뀔 때
     *          - 백엔드 디바이스가 멤버로 품는 공유 타입(RHI/Support 의 FrameResourceRing 등)의 레이아웃이 바뀔 때
     *          RHINativeHandles 는 이 도장이 Engine ↔ RHI_* 를 덮고, Engine ↔ 부르는 모듈(에디터)은 구조체의 `_version` · `_byteSize` 로 대조합니다.
     *          판마다 무엇이 바뀌었는지는 git log 에 있습니다.
     */
    inline constexpr auto kRHIModuleAbiStamp = "rhi-cl-v25-2026-10";

    namespace RHIModuleAbiInternal
    {
        /** @brief 도장 `rhi-cl-v<N>-…` 의 N 을 읽습니다. 숫자 버전과 도장이 어긋나지 않게 컴파일 때 견줍니다. */
        constexpr uint32 readStampVersion( const utf8* pStamp )
        {
            uint32 index{ 0 };
            while ( pStamp[index] != '\0' && pStamp[index] != 'v' )
                ++index;
            if ( pStamp[index] == 'v' )
                ++index;
            uint32 version{ 0 };
            while ( pStamp[index] >= '0' && pStamp[index] <= '9' )
            {
                version = version * 10u + static_cast<uint32>( pStamp[index] - '0' );
                ++index;
            }
            return version;
        }
    } // namespace RHIModuleAbiInternal
    static_assert( RHIModuleAbiInternal::readStampVersion( kRHIModuleAbiStamp ) == kRHIModuleAbiVersion,
                   "kRHIModuleAbiVersion and the v<N> in kRHIModuleAbiStamp must be bumped together" );

    using PFN_CreateRHIDevice        = IRHIDevice* (*)();
    using PFN_GetRHIModuleAbiVersion = uint32 ( * )();
    using PFN_GetRHIModuleAbiStamp   = const utf8* (*)();
} // namespace sw
