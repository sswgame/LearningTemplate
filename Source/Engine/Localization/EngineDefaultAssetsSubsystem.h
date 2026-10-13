/**
 * @file Engine/Localization/EngineDefaultAssetsSubsystem.h
 * @brief 기동 순서의 EngineDefaultAssets 자리입니다 — 엔진 데이터(`EngineDefaultAssets`)를 읽고 엔진 문화권 표 · 문자열을 올리며, 그 데이터를 소유합니다.
 * @details 플레이어 설정(언어)은 이 뒤(`UserSettings`)에 적용되고, 게임은 자기 프로젝트를 위에 올린다. 명령줄 `-language=` 가 있으면 그 언어로 시작한다.
 *          테스트 하네스는 데이터를 읽지 않고 소유만 한다(`TestEngineDefaultAssetsSubsystem`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    struct EngineDefaultAssets;

    /**
     * @class EngineDefaultAssetsSubsystem
     * @brief 엔진 데이터와 엔진 문화권 · 문자열을 올리는 서브시스템입니다.
     */
    class SW_API EngineDefaultAssetsSubsystem : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EngineDefaultAssets";

        /** @brief @p defaultAssets 를 넘겨받습니다(서비스 표에는 호스트가 이미 묶었다). */
        explicit EngineDefaultAssetsSubsystem( unique_ptr<EngineDefaultAssets> defaultAssets );
        ~EngineDefaultAssetsSubsystem() override;

        EngineDefaultAssetsSubsystem( const EngineDefaultAssetsSubsystem& )            = delete;
        EngineDefaultAssetsSubsystem& operator=( const EngineDefaultAssetsSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

    protected:
        EngineDefaultAssets& getDefaultAssets() const { return *_defaultAssets; }

    private:
        unique_ptr<EngineDefaultAssets> _defaultAssets;
    };
} // namespace sw
