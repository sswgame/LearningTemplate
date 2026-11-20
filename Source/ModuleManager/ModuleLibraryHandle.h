/**
 * @file ModuleManager/ModuleLibraryHandle.h
 * @brief 로드한 모듈 라이브러리(게임 · 에디터 · 키트 DLL)를 가리키는 불투명 핸들 `ModuleLibraryHandle` 입니다.
 * @details 모듈 경계에는 `void*` 가 둘 있었다 — 라이브러리 핸들(OS 로더의 `HMODULE` · `dlopen` 값)과 인스턴스 핸들(`GameHandle` · `EditorHandle`, 모듈이 만든 객체).
 *          둘 다 `void*` 라 한쪽을 다른 쪽 자리에 넘겨도 컴파일됐다. 라이브러리 핸들을 이 형식으로 감싸 섞이면 컴파일에서 멈추게 한다(언리얼 `FModuleManager` 가
 *          DLL 핸들을 `IModuleInterface` 인스턴스와 다른 형식으로 드는 것과 같다). OS 값은 로더(`ModuleLibraryUtil` — Core, 모듈 밖 라이브러리도 다룬다)와 이 형식이
 *          만나는 자리(리로드 매니저 · 헤드리스 에디터 모듈)에서만 꺼내고 넣는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Module/ModuleLibraryUtil.h"

namespace sw
{
    /**
     * @class ModuleLibraryHandle
     * @brief 파일 머리말 참고. 소유하지 않는다 — 라이브러리를 연 쪽(`LiveReloadManager` · 헤드리스 경로)이 내린다. 기본값은 빈 핸들(정적 링크 · 로드 전)입니다.
     */
    class ModuleLibraryHandle
    {
    public:
        constexpr ModuleLibraryHandle() noexcept = default;
        /** @brief OS 로더가 돌려준 값(`ModuleLibraryUtil::loadLibraryAt`)을 감쌉니다. 실패한 로드(nullptr)는 빈 핸들입니다. */
        constexpr explicit ModuleLibraryHandle( void* pOSHandle ) noexcept
            : _pOSHandle{ pOSHandle }
        {
        }

        /** @brief 로드된 라이브러리를 가리키면 true 입니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _pOSHandle != nullptr; }
        /** @brief 라이브러리의 심볼 @p symbolName 주소입니다. 빈 핸들이거나 없으면 nullptr 입니다. */
        [[nodiscard]] void* findSymbol( string_view symbolName ) const { return isValid() ? ModuleLibraryUtil::getDynamicSymbol( _pOSHandle, symbolName ) : nullptr; }
        /** @brief OS 값입니다 — 로더(`ModuleLibraryUtil`)에 되돌려 줄 때만 꺼낸다(로그의 주소 표시 포함). */
        [[nodiscard]] constexpr void* getOSHandle() const noexcept { return _pOSHandle; }

    private:
        void* _pOSHandle{ nullptr };
    };
} // namespace sw
