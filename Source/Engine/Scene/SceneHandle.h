/**
 * @file SceneHandle.h
 * @brief 씬 매니저가 올린 씬을 가리키는 핸들입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 씬 매니저가 올린 씬 하나를 가리킵니다(`SceneManager::resolveScene`). 씬이 내려가면 풀리지 않는다 — 번호는 다시 쓰지 않는다.
     * @details 로드의 답(`requestLoadFuture`)이 이 형식이다. 포인터를 주면 답을 받는 프레임과 여는 프레임 사이에 다른 씬이 열려 사라진 씬을 만질 수 있었다.
     */
    struct SceneHandle
    {
        uint64 _serial{ 0 }; ///< 씬 매니저가 씬을 받을 때 매긴 번호(0 은 없음 — 로드 실패 · 밀려남)

        bool isValid() const { return _serial != 0; }
    };
} // namespace sw
