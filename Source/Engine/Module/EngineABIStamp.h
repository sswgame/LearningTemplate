/**
 * @file EngineABIStamp.h
 * @brief 이 Engine 이 빌드될 때의 Core · Engine 헤더 지문입니다(핫 리로드의 엔진 ABI 스탬프).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    namespace engine
    {
        /**
         * @brief 이 Engine 이 빌드될 때의 Core · Engine 헤더 지문입니다(`swEngineABIStamp:<sha1>`).
         * @details 모듈도 같은 값을 박습니다(`sw_register_dynamic_module` 이 넣는 생성 소스). 핫 리로드는 섀도 복사본을 올리기 **전에**
         *          파일 바이트에서 모듈의 지문을 찾아 이것과 대조하고, 다르면 옛 모듈을 유지합니다 — 돌고 있는 엔진과 다른 헤더로 빌드된
         *          모듈은 구조체 배치 · vtable 이 어긋나 조용히 망가지기 때문입니다(`LiveReloadManager::prepareShadowCopy`).
         *          지문은 `Scripts/generate/engine_abi_stamp.py` 가 헤더 내용으로 만듭니다.
         */
        SW_API const utf8* getEngineABIStamp();
        /**
         * @brief 올린 모듈 라이브러리 @p pLibraryHandle 이 이 엔진과 같은 헤더로 빌드됐는지 봅니다 — 빌드가 모듈마다 박은 `sw_moduleEngineABIStamp_<@p moduleName>` 를 이 엔진의 것과 견준다.
         * @details 모든 동적 모듈(게임 · 에디터 · 키트 · RHI 백엔드)을 올리는 길이 이것 하나로 대조한다. 지문은 Core · Engine · ModuleAPI 헤더 내용이라 C-ABI 표(GameAPI · EditorAPI)의
         *          항목 · 순서 · 시그니처와 경계를 넘는 구조체 배치를 함께 덮는다 — 표마다 따로 매기던 스탬프는 이 지문이 덮는 사본이었다. 언리얼 모듈의 `BuildId` 대조 하나와 같은 자리다.
         * @return 같으면 true, 스탬프가 없거나 다르면 이유(`Module.EngineABIStampMismatch`)를 남기고 false 입니다.
         */
        [[nodiscard]] SW_API bool isModuleBuiltWithThisEngine( void* pLibraryHandle, string_view moduleName );
    } // namespace engine
} // namespace sw
