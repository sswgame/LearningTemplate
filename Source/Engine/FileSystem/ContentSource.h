/**
 * @file ContentSource.h
 * @brief 콘텐츠를 쿠킹본과 소스 중 어디서 읽는가의 정책 하나입니다. 팩 매니저가 들고(`PackManager::getContentSource`), 읽는 쪽은 `AssetIO::getContentSource` 로 묻는다.
 * @details 예전에는 "배포는 쿠킹본만" 을 읽는 파일마다 `#if SW_SHIPPING` 으로 갈라(씬 · 프리팹 · 셰이더 · 셰이더 리플렉션 · 팩 매니저) 배포 읽기 경로를 배포 빌드에서만
 *          돌려 볼 수 있었고, 개발 구성의 씬은 옆에 남은 쿠킹본을 소스와 맞는지 보지 않고 읽었다. 이제 빌드 스위치는 기본값 한 곳(`kBuildContentSource`)이고,
 *          개발 구성도 `-cooked-content` 로 배포와 같은 경로를 돈다. 둘을 섞는 값("쿠킹본이 있으면 쿠킹본, 없으면 소스")은 두지 않는다 — 쿠킹본이 소스와 어긋나도
 *          모르고 읽는다(언리얼도 쿠킹 안 한 실행은 소스만, 쿠킹한 실행은 쿠킹본만 읽는다 — `FPlatformProperties::RequiresCookedData`).
 *          셰이더 쿠킹본을 소스 실행이 쓰는 것은 섞기가 아니다 — 소스 내용 해시로 맞는지 보고 쓰는 캐시다(`ShaderCooker::isCookedOutputCurrent`, 언리얼 DDC).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 콘텐츠를 어디서 읽는가입니다. 호스트가 기동 때 한 번 고른다(`AssetManager::initializeSubsystem`). */
    enum class ContentSource : uint8
    {
        Cooked, ///< 쿠킹한 팩만 읽는다. 낱개 파일 · 소스(씬 XML · 셰이더 HLSL)를 읽지 않고, 쿠킹본이 없으면 오류다 — 배포 구성의 값
        Source, ///< 소스 트리(`Content/`)만 읽는다. 팩(쿠킹의 산출물)은 마운트하지 않는다 — 개발 실행과 쿠킹(배포 구성의 쿠킹도)의 값
    };

#if defined( SW_SHIPPING )
    inline constexpr ContentSource kBuildContentSource = ContentSource::Cooked; ///< 이 빌드가 따로 고르지 않을 때의 값 — 빌드 스위치는 여기 한 곳이다
#else
    inline constexpr ContentSource kBuildContentSource = ContentSource::Source;
#endif
} // namespace sw
