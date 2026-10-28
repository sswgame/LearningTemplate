/**
 * @file RHIDxResultCheck.h
 * @brief D3D11 · D3D12 · DXGI 호출이 돌려준 HRESULT 를 받는 두 길입니다(DX11, DX12 공유, 에디터 ImGui 백엔드도 씀).
 * @details `HRESULT` 는 SDK 의 `long` 이라 `[[nodiscard]]` 를 붙일 수 없고, COM 메서드 선언도 고칠 수 없습니다. 그래서 결과는 이 두 함수로만
 *          받습니다 — 이어 갈 수 있으면 `isHrSuccess`(오류 로그 + false, `[[nodiscard]]`), 이어 갈 수 없으면 `requireHr`(`SW_FATAL`).
 *          형식으로 못 막는 남은 틈(결과를 아예 받지 않은 호출)은 `[[nodiscard]]` 밖이라 호출하는 쪽이 지킵니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /**
     * @brief HRESULT 가 성공인지 봅니다. 실패면 무엇이 실패했는지 오류로 남기고 false 입니다.
     * @param pWhat 실패한 호출 이름(예: `"CreateShaderResourceView"`)입니다.
     */
    [[nodiscard]] inline bool isHrSuccess( HRESULT result, const utf8* pWhat )
    {
        if ( SUCCEEDED( result ) )
            return true;
        SW_LOG_ERROR( "%# failed (HRESULT 0x%#)", pWhat, static_cast<uint32>( result ) );
        return false;
    }

    /** @brief HRESULT 가 실패면 이유와 함께 멈춥니다. 실패하면 이어 갈 수 없는 호출(명령 리스트 닫기 · 리셋)에 씁니다. */
    inline void requireHr( HRESULT result, const utf8* pWhat )
    {
        if ( FAILED( result ) )
            SW_FATAL( "%# failed (HRESULT 0x%#)", pWhat, static_cast<uint32>( result ) );
    }
} // namespace sw
#endif
