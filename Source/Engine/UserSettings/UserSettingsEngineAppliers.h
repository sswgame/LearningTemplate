/**
 * @file UserSettingsEngineAppliers.h
 * @brief 엔진이 등록하는 사용자 설정 적용기(오디오 버스, 입력, 언어, 화면)와 화면 요청(`DisplaySettingsRequest`)을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UserSettings/UserSettingApplierRegistry.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    class CommandLineManager;
    class GlobalVariableManager;
    class IAudioService;
    class InputMap;
    class LocalizationManager;

    /** @brief 엔진 적용기가 값을 넣는 대상입니다. 테스트는 자기 것을 넘기고, 엔진은 서비스를 넘깁니다. 비어 있는 대상의 적용기는 아무것도 하지 않습니다. */
    struct UserSettingsTargets
    {
        GlobalVariableManager*    _pGlobalVariableManager{ nullptr };
        const CommandLineManager* _pCommandLineManager{ nullptr }; ///< 있으면 명령줄로 준 전역 변수(`-gv_*`)는 기동 적용이 덮지 않는다(명령줄이 이긴다)
        InputMap*                 _pInputMap{ nullptr };
        IAudioService*            _pAudioService{ nullptr };
        LocalizationManager*      _pLocalizationManager{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 화면 설정(창 방식, 해상도, VSync)이 요청하는 상태입니다.
     * @details 적용기는 창과 스왑체인을 직접 건드리지 않고 여기에 적기만 한다. 실제 변경은 호스트(App)가 프레임 맨 앞(OS 리사이즈와 같은 위치)에서
     *          `UserSettingsManager::consumeDisplayRequest` 로 꺼내 처리한다(렌더 스레드를 기다린 뒤 VSync 를 바꾸고, 창 크기를 알려 App::onResize 를 부른다).
     *          `_bHas*` 는 플레이어가 고른(기본값이 아닌) 값이 있는지 나타낸다. 기동 때 엔진 설정(EngineConfig)이나 명령줄보다 우선할지 정한다.
     */
    struct DisplaySettingsRequest
    {
        uint32            _width{ 1280 };
        uint32            _height{ 720 };
        WindowDisplayMode _mode{ WindowDisplayMode::Windowed };
        bool              _bVSync{ false };
        bool              _bHasResolution{ false };
        bool              _bHasMode{ false };
        bool              _bHasVSync{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingsEngineAppliers
     * @brief 엔진 적용기 모음입니다. `UserSettingsManager` 가 하나 가지고 레지스트리에 등록합니다.
     * @details 이름(스키마의 `target="applier:…"`):
     *          - `audio.busVolume`(param 은 버스), `audio.mute`
     *          - `input.mouseSensitivity`, `input.gamepadSensitivity`, `input.invertX`, `input.invertY`, `input.stickDeadzone`,
     *            `input.toggleMode`(param 은 액션, 값은 `hold`/`toggle`), `input.keyBinding`(키 바인딩 설정의 기본 대상)
     *          - `localization.language`(빈 값은 게임 기본 언어 그대로)
     *          - `display.windowMode`(`windowed`/`borderless`), `display.resolution`(`<너비>x<높이>`), `display.vsync`
     *          선택지 공급자: `localization.languages`(읽은 언어 목록), `display.resolutions`(기본 모니터가 내는 해상도 — 못 읽으면 스키마의 고정 선택지).
     */
    class SW_API UserSettingsEngineAppliers
    {
    public:
        UserSettingsEngineAppliers();

        /** @brief 대상을 정하고 적용기와 공급자를 @p registry 에 등록합니다. */
        void initialize( const UserSettingsTargets& targets, UserSettingApplierRegistry& registry );
        /** @brief 대상을 잊습니다. */
        void shutdown();

        const UserSettingsTargets&    getTargets() const { return _targets; }
        const DisplaySettingsRequest& getDisplayRequest() const { return _displayRequest; }
        /** @brief 실행 중에 바뀐 화면 요청이 있으면 꺼내고 true 입니다(한 번만). */
        bool consumeDisplayRequest( DisplaySettingsRequest& outRequest );

        /** @brief `1920x1080` 을 읽습니다. */
        [[nodiscard]] static bool tryParseResolution( string_view text, uint32& outWidth, uint32& outHeight );
        /** @brief `windowed` 와 `borderless` 를 읽습니다. */
        [[nodiscard]] static bool tryParseWindowMode( string_view text, WindowDisplayMode& outMode );

    private:
        [[nodiscard]] bool applyBusVolume( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyMute( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyMouseSensitivity( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyGamepadSensitivity( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyInvertX( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyInvertY( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyStickDeadzone( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyToggleMode( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyKeyBinding( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyLanguage( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyWindowMode( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyResolution( const UserSettingApplyParams& context );
        [[nodiscard]] bool applyVSync( const UserSettingApplyParams& context );
        void               collectLanguages( vector<UserSettingOption>& outListOption );
        void               collectResolutions( vector<UserSettingOption>& outListOption );
        /** @brief `1920x1080` 형식의 글을 만듭니다. */
        static string formatResolution( int32 width, int32 height );

        static bool isToggleModeName( string_view value );
        static bool isWindowModeName( string_view value );
        static bool isResolutionText( string_view value );

    private:
        UserSettingsTargets    _targets;
        DisplaySettingsRequest _displayRequest;
        vector<int2>           _listDisplayResolution; ///< 모니터 모드의 해상도(`collectResolutions` 가 처음 한 번 읽는다)
        bool                   _bDisplayDirty;
        bool                   _bResolutionProbed{ false };
    };
} // namespace sw
