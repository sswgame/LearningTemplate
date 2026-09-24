/**
 * @file EngineAbiStamp.h
 * @brief 이 Engine 이 빌드될 때의 Core · Engine 헤더 지문입니다(핫 리로드의 엔진 ABI 도장).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    namespace engine
    {
        /**
         * @brief 이 Engine 이 빌드될 때의 Core · Engine 헤더 지문입니다(`swEngineAbiStamp:<sha1>`).
         * @details 모듈도 같은 값을 굽습니다(`sw_registerDynamicModule` 이 넣는 생성 소스). 핫 리로드는 섀도 복사본을 올리기 **전에**
         *          파일 바이트에서 모듈의 지문을 찾아 이것과 대조하고, 다르면 옛 모듈을 유지합니다 — 돌고 있는 엔진과 다른 헤더로 빌드된
         *          모듈은 구조체 배치 · vtable 이 어긋나 조용히 망가지기 때문입니다(`LiveReloadManager::prepareShadowCopy`).
         *          지문은 `Scripts/setup/GenerateEngineAbiStamp.py` 가 헤더 내용으로 만듭니다.
         */
        SW_API const utf8* getEngineAbiStamp();
    } // namespace engine
} // namespace sw
