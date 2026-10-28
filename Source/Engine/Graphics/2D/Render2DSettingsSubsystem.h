/**
 * @file Engine/Graphics/2D/Render2DSettingsSubsystem.h
 * @brief 기동 순서의 Render2DSettings 자리입니다 — 엔진 파일(`engine/data/render2d.xml`)을 읽고 그 값을 소유합니다.
 * @details 읽는 쪽은 서비스 표(`engine::getSubsystem<Render2DSettings>()`)로 찾는다. 예전의 정적 활성본(`Render2DSettings::getActive`)을 대신한다 - 엔진마다
 *          표가 따로 있고, 테스트는 서비스 범위에 다른 표를 걸어 볼 수 있다. 팩 마운트(`AssetManager`)에 기대므로 그 뒤에 서고, 2D 컴포넌트를 읽는
 *          단계(헤드리스 쿠킹 · 렌더러)는 이 서브시스템에 의존으로 매인다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class Render2DSettings;
} // namespace sw

namespace sw
{
    /**
     * @class Render2DSettingsSubsystem
     * @brief 2D 렌더 설정을 엔진 파일에서 읽어 소유하는 서브시스템입니다.
     * @details 읽지 못하면 기동을 멈춘다(대신 쓸 표가 없다 - 기본 표를 지어내지 않는다). 호스트는 소유한 값을 범위에 `bind` 해 서비스 표가 찾게 한다.
     */
    class SW_API Render2DSettingsSubsystem : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Render2DSettings";

        /** @brief 레이어가 없는 빈 표를 만들어 소유합니다. 값은 `initializeSubsystem` 이 엔진 파일에서 읽는다(호스트는 그 전에 표를 범위에 `bind` 한다). */
        Render2DSettingsSubsystem();
        ~Render2DSettingsSubsystem() override;

        Render2DSettingsSubsystem( const Render2DSettingsSubsystem& )            = delete;
        Render2DSettingsSubsystem& operator=( const Render2DSettingsSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 소유한 2D 렌더 설정입니다(호스트가 범위에 `bind` 한다). */
        Render2DSettings& getSettings() const { return *_settings; }

        /**
         * @brief 엔진 파일에서 표를 다시 읽습니다(렌더러 초기화 · 설정 핫 리로드). 게임 스레드에서만 부릅니다.
         * @return 못 읽으면 오류를 남기고 false 이며 표는 그대로입니다. 읽으면 같은 자리의 표가 새 값이 된다(서비스 표가 쥔 주소는 바뀌지 않는다).
         */
        [[nodiscard]] bool reload();

    private:
        unique_ptr<Render2DSettings> _settings;
    };
} // namespace sw
