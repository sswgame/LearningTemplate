/**
 * @file ModuleImageUtil.h
 * @brief 동적으로 올린 모듈 이미지를 내리는 단일 창구입니다 — 그 이미지의 코드를 쥔 등록을 먼저 떼고 내립니다.
 *
 * 게임 · 에디터 모듈(`ModuleHost`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`LiveReloadManager`)가 모두 이 창구를 지난다.
 * 등록부 목록은 `IModuleUnloadListener` 가 들고, 여기서는 그 목록을 훑고 결과를 로그로 남긴 뒤 이미지를 내린다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct ModuleImageUtil
     * @brief 모듈 이미지의 코드 떼기와 이미지 내리기입니다.
     */
    struct SW_API ModuleImageUtil
    {
        /**
         * @brief 모듈 이미지 [@p pBegin, @p pEnd) 의 코드를 가리키는 등록을 뗍니다. 이미지를 내리거나 언로드를 미루기 **전에** 부릅니다.
         * @details 모듈보다 오래 사는 등록부는 `IModuleUnloadListener` 를 상속해 만들어질 때 스스로 목록에 오르고, 여기서는 그 목록을 훑습니다
         *          — 이벤트 버스(구독과 그 모듈이 만든 채널 항목) · 전역 로그 리스너 · Undo 스택(들어 있으면 통째로 비운다) · 창의 처리기 ·
         *          에셋 캐시 등록부. 등록부를 하나 더하는 자리는 그 등록부의 상속 한 줄이고 이 함수는 고치지 않습니다. 뗀 것은 리스너 이름과
         *          함께 경고로 남깁니다 — 모듈이 스스로 떼지 않고 남긴 것이라 모듈 쪽 버그의 실마리입니다.
         * @param pOutKeepImageMapped 주면, 이 이미지를 **내리면 안 되는지** 받습니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 true —
         *                            채널의 브로드캐스트 함수와 멀티캐스트의 해제자(`shared_ptr` 제어 블록)가 그 이미지의 코드라, 내리면 다음
         *                            발행 · 디스패처 소멸이 내려간 코드로 뛴다. 그런 이미지는 프로세스 끝까지 올려 둔다(떼어 낼 방법이 없다).
         * @return 모든 리스너에서 뗀 것의 수입니다. 모듈이 제대로 정리했으면 0 입니다.
         */
        static uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped = nullptr );

        /**
         * @brief `FileUtil::loadDynamicLibrary` 로 올린 모듈 이미지 @p pHandle 을 내립니다.
         * @details 내리기 전에 그 이미지의 코드를 쥔 등록을 뗍니다(`releaseModuleCode`). 떼어 낼 수 없는 것이 남으면 내리지 않고 false 를
         *          돌려줍니다. 그 이미지가 끌어온 의존 이미지(GameFramework 같은 공유 모듈)는 **내리지 않습니다** — 의존 이미지가 언제 함께
         *          내려가는지는 로더만 알아 그 코드를 미리 뗄 수 없기 때문입니다. Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 잡아 원래
         *          그렇고, 리눅스는 `DT_NEEDED` 참조가 함께 풀려 내려가므로 여기서 고정합니다(`FileUtil::pinDynamicLibraryDependencies`).
         *          핫 리로드(`LiveReloadManager`)는 떼기와 내리기 사이에 언로드를 미루므로 `releaseModuleCode` 만 씁니다.
         * @return 이미지를 내렸으면 true 입니다.
         */
        [[nodiscard]] static bool unloadModuleImage( string_view moduleName, void* pHandle );
    };
} // namespace sw
