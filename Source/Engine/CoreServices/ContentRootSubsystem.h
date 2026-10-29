/**
 * @file Engine/CoreServices/ContentRootSubsystem.h
 * @brief 리소스 루트를 찾고 돌릴 게임을 고릅니다 — 엔진을 세우는 호스트(App · Server · 테스트 하네스)의 기동 단계입니다.
 * @details 리소스 경로 상태(`AssetPaths` — 경로 표 · 검색 우선순위 · 붙인 디스크 도메인 · 해석 캐시)의 **유일한 주인**이다. 코어 범위가 이 객체 안의
 *          `AssetPaths` 를 그 타입으로 걸어(`addCoreSubsystems`) 엔진 코드는 `engine::getSubsystem<AssetPaths>()` 로 닿고, 서비스 표가 서기 전의 기동 코드는
 *          `getAssetPaths()` 의 참조를 받는다. 언리얼의 `UAssetManager` 검색 경로 · `IPlatformFile` 마운트가 객체 상태인 것과 같은 자리다.
 *          크래시 핸들러 뒤에 선다 — 루트를 찾지 못했을 때의 진단이 로거에 남고, 그 사이에 죽어도 콜 스택이 남는다. 게임 선택(Dev `-game=`)은 모듈 카탈로그 · 게임 프리셋을 읽기 전이어야 한다. 크래시 보고 프로세스는 리소스 루트를
 *          쓰지 않으므로 아무것도 하지 않는다(못 찾아도 서야 한다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/FileSystem/AssetPaths.h"

namespace sw
{
    class CommandLineSubsystem;
    class CrashHandlerSubsystem;

    /** @class ContentRootSubsystem @brief 리소스 루트 · 게임 선택 서브시스템입니다. */
    class SW_API ContentRootSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ContentRoot";

        /** @brief 둘 다 같은 범위의 것이다(이것보다 먼저 서고 나중에 사라진다). */
        ContentRootSubsystem( const CommandLineSubsystem& commandLine, const CrashHandlerSubsystem& crashHandler );

        ContentRootSubsystem( const ContentRootSubsystem& )            = delete;
        ContentRootSubsystem& operator=( const ContentRootSubsystem& ) = delete;

        /** @brief 리소스 루트를 못 찾거나 이 빌드에 없는 게임이면 `Failed` 입니다(진단은 로거에 남는다). */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 이 서브시스템이 소유한 리소스 경로 상태입니다. 서비스 표가 서기 전의 기동 코드(`Engine::initialize` 의 호스트 타깃)가 이것으로 받는다. */
        AssetPaths&       getAssetPaths() { return _assetPaths; }
        const AssetPaths& getAssetPaths() const { return _assetPaths; }

    private:
        const CommandLineSubsystem&  _commandLine;
        const CrashHandlerSubsystem& _crashHandler;
        AssetPaths                   _assetPaths; ///< 경로 상태 전부 — 이 프로세스의 엔진에서 하나다
    };
} // namespace sw
