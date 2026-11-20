/**
 * @file ReloadSource.h
 * @brief 리로드 결과를 적은 길(설정 · 에셋 · 셰이더 · 모듈)입니다. 이름은 리플렉션(`ENUM`)이 낸다(`getEnumName`).
 * @details `ReloadTracker.h` 는 불투명 선언만 든다 — 리플렉션 매크로 헤더를 집계의 모든 사용자에게 끌어오지 않으려고 열거자를 쓰는 단위만 이 헤더를 넣는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @enum ReloadSource @brief 결과를 적은 리로드 길입니다. 이름(표 · 감시 값 `Reload.Count.<길>`)은 열거자 이름이다. 순서를 바꾸면 `ReloadTracker.h` 의 `kReloadSourceCount` 와 함께. */
    ENUM()
    enum class ReloadSource : uint8
    {
        Config, ///< `ConfigReloadSubsystem`(호스트 설정 · 등록된 설정 처리기)
        Asset,  ///< 에디터 `AssetHotReload`(에셋 캐시 · 원본 임포트)
        Shader, ///< `ShaderRecompiler`(셰이더 재컴파일)
        Module, ///< `LiveReloadManager`(모듈 빌드 · 교체)
    };
} // namespace sw
