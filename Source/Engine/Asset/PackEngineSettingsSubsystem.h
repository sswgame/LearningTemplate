/**
 * @file Engine/Asset/PackEngineSettingsSubsystem.h
 * @brief 기동 순서의 PackEngineSettings 자리입니다 — 팩을 마운트한 뒤 활성 팩 `gamesettings.xml` 의 `<Engine>` 요소를 읽고 그 값을 소유합니다.
 * @details 팩 마운트(`AssetManager`, Resource 단계)에 기대므로 Resource 폴더에 있다(값의 형식 `PackEngineSettings` 는 Config). 테스트 하네스도 같은 단계를 세운다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    struct PackEngineSettings;
} // namespace sw

namespace sw
{
    /**
     * @class PackEngineSettingsSubsystem
     * @brief 기동 순서의 PackEngineSettings 자리입니다 — 팩을 마운트한 뒤(`AssetManager`) 활성 팩의 `<Engine>` 요소를 읽고 그 값을 소유합니다.
     * @details 읽지 못하면 기동을 멈춘다(대신 쓸 값이 없다). 창 · 사용자 설정 · 텔레메트리 · UI · 렌더러가 이 뒤에 선다(각자 의존으로 적는다).
     *          호스트는 소유한 값을 범위에 `bind` 해 서비스 표(`engine::getSubsystem<PackEngineSettings>()`)가 찾게 한다.
     */
    class SW_API PackEngineSettingsSubsystem : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "PackEngineSettings";

        /** @brief 읽지 않은 값을 만들어 소유합니다. 값은 `initializeSubsystem` 이 활성 팩에서 읽는다(호스트는 그 전에 값을 범위에 `bind` 한다). */
        PackEngineSettingsSubsystem();
        ~PackEngineSettingsSubsystem() override;

        PackEngineSettingsSubsystem( const PackEngineSettingsSubsystem& )            = delete;
        PackEngineSettingsSubsystem& operator=( const PackEngineSettingsSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 소유한 팩 설정입니다(호스트가 범위에 `bind` 한다). */
        PackEngineSettings& getSettings() const { return *_settings; }

    private:
        unique_ptr<PackEngineSettings> _settings;
    };
} // namespace sw
