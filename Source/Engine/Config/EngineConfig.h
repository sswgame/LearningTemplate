#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{

    /**
     * @struct WindowConfig
     * @brief 주 창과 기본 렌더링 백엔드 설정입니다. 창 제목은 팩 설정(`PackEngineSettings::_windowTitle`)이 정합니다.
     * @details 값은 `Config/Engine/EngineConfig.json` 에 있습니다(이 헤더는 형식만). 예외는 `_defaultRHI` 하나로, 출처가 쿠킹 표 `default_rhi_backend` 입니다.
     */
    REFLECT()
    struct SW_API WindowConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _clearColor{}; ///< 백버퍼 클리어 색(공백 또는 쉼표로 구분한 RGBA)

        PROPERTY( Min = 1 )
        uint32 _width{}; ///< 클라이언트 영역 너비(픽셀)

        PROPERTY( Min = 1 )
        uint32 _height{}; ///< 클라이언트 영역 높이(픽셀)

        PROPERTY()
        RHIBackend _defaultRHI{ RHIBackend::SW_RHI_BACKEND_DEFAULT }; ///< 명령줄이 고르지 않았을 때 쓸 백엔드(값의 출처는 쿠킹 표 `default_rhi_backend` — 파일에 적으면 그것이 이긴다)

        PROPERTY()
        bool _bVSync{}; ///< 수직 동기화 여부
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TextureFormatFamilySettings
     * @brief 압축 형식 묶음 하나입니다(BC · ASTC · ETC2). 용도마다 쓸 RHIFormat 이름을 적습니다 — 이 층은 RHI 를 모르므로 글이고 `TextureFormatFamily` 이 풀며, 모르는 이름은 오류입니다.
     * @details `_suffix` 는 쿠킹 변형의 파일 접미사입니다(`a.dds` → `a.astc.dds`). 비면 임포트한 DDS 자체가 이 묶음입니다. BC 의 색 형식은 임포트 규칙이 정해 여기 값은 장치 지원을 묻는 대표입니다.
     */
    REFLECT()
    struct SW_API TextureFormatFamilySettings
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        string _suffix{};
        PROPERTY()
        string _color{}; ///< 색 · 일반 데이터(임포트 규칙의 BC7 · BC3 · BC1)
        PROPERTY()
        string _normal{}; ///< 2채널 노멀맵(BC5)
        PROPERTY()
        string _mask{}; ///< 단일 채널(BC4)
        PROPERTY()
        string _hdr{}; ///< HDR(BC6H)
    };

    /** @struct TextureFormatTargetSettings @brief 배포 대상 하나의 형식 묶음 우선순위입니다. 실행 때 장치가 지원하는 첫 묶음을 고릅니다(웹은 두 벌 — 데스크톱 브라우저 BC, 모바일 브라우저 ASTC). */
    REFLECT()
    struct SW_API TextureFormatTargetSettings
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        vector<string> _listPlatform{}; ///< 이 대상을 쓰는 빌드 플랫폼(`build::kPlatformName` — Windows · Linux · Android · Web). 플랫폼 하나는 대상 하나에만 든다
        PROPERTY()
        vector<string> _listFamily{};
    };

    /** @struct TextureFormatSettings @brief 텍스처 형식 쿠킹 표입니다. Godot 의 플랫폼별 압축 형식 파일 · 언리얼 `TextureFormatPriority` 자리입니다(기억에 의존한 비교). */
    REFLECT()
    struct SW_API TextureFormatSettings
    {
        REFLECT_BODY();

        PROPERTY()
        vector<TextureFormatTargetSettings> _listTarget{}; ///< 실행은 빌드 플랫폼의 대상을 고르고(`findPlatformTarget`), 쿠킹은 모든 대상의 묶음을 굽는다(`resolveCookFamilies`)
        PROPERTY()
        vector<TextureFormatFamilySettings> _listFamily{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct EngineConfig
     * @brief `Config/Engine/EngineConfig.json` 이 담는 엔진 기동 설정입니다.
     * @details 이 헤더는 형식(타입 · 범위)만 정하고 값은 모두 그 파일에 있습니다(언리얼 `BaseEngine.ini` 와 같은 바탕 층). 그래서 파일은 모든 키를 적고
     *          (`ConfigFileSchemaTest` 가 빠진 키를 막는다), 빠진 숫자 키는 범위 검사(`Min`)에서 로드 오류가 됩니다. 기동은 파일을 읽은 뒤에만 이 값을 봅니다
     *          (소유자는 `ConfigManager` 이고 읽는 쪽은 `engine::getSubsystem<EngineConfig>()` 다 - 정적 활성본이 없어 표마다 값이 따로 있고, 다시 읽기는 제자리다).
     *          파일을 읽기 전에 필요한 리소스 우선순위는 구성 때 베이크한 같은 파일의 사본(`AssetPaths::getDefaultSearchPriority`)에서 읽습니다.
     *          `ConfigBaseLayer` 메타는 프로젝트 설정 창이 이 파일을 "다른 값만" 이 아니라 모든 키로 쓰게 합니다(`ProjectSettingsDocument`).
     */
    REFLECT( Meta = "ConfigBaseLayer" )
    struct SW_API EngineConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        WindowConfig _window; ///< 창·백엔드 설정

        /** @brief 한 프레임이 인정하는 최대 가변 델타(초)입니다. 디버거 정지 같은 긴 멈춤을 잘라 냅니다. */
        PROPERTY( Min = 0.001, Units = s )
        float32 _maxFrameDeltaTime{};

        /**
         * @brief 고정 주기 한 스텝의 길이(초)입니다. 기본은 60Hz 입니다.
         * @details 고정 스텝 값의 출처는 이것 하나다 — 게임 `fixedUpdate` 와 씬 물리가 같이 쓴다(물리는 `PhysicsSettings::_subStepCount` 로 이 스텝을 나눈다,
         *          유니티 `Time.fixedDeltaTime` 과 같다).
         */
        PROPERTY( Min = 0.001, Units = s )
        float32 _fixedDeltaTime{};

        /**
         * @brief 한 프레임이 돌릴 수 있는 고정 스텝 수의 상한입니다.
         * @details 상한을 넘긴 남은 시간은 버립니다. 남기면 느린 프레임이 더 많은 스텝을 불러
         *          더 느려지는 악순환이 됩니다. FixedTimestep 이 이 값을 적용합니다.
         */
        PROPERTY( Min = 1 )
        uint32 _maxFixedStepPerFrame{};

        PROPERTY()
        vector<string> _listResourcePriority{}; ///< 리소스 팩 탐색 우선순위(앞이 먼저)

        /** @brief 데이터 상속(`_base`, `DataInheritance`) 사슬의 깊이 상한입니다. 넘으면 그 파일은 로드 오류입니다. */
        PROPERTY( Min = 1 )
        uint32 _dataInheritanceMaxDepth{};

        /**
         * @brief 패치 목록 서명 확인용 공개 키(ES256 P-256)의 x 좌표입니다(64 자리 16 진). `_patchTrustPublicKeyYHex` 와 함께 비어 있으면 서명을 요구하지 않습니다(스토어가 무결성을 맡는 채널).
         * @details 공개 키는 비밀이 아니다. 개인 키는 이 파일에 두지 않고 패치를 만드는 쪽이 `-patch-sign-key <PEM 경로>` 로만 받는다(`PackPatchTrustSource`).
         */
        PROPERTY()
        string _patchTrustPublicKeyXHex{};

        /** @brief 패치 목록 서명 확인용 공개 키의 y 좌표입니다(64 자리 16 진). */
        PROPERTY()
        string _patchTrustPublicKeyYHex{};

        PROPERTY()
        TextureFormatSettings _textureFormats{}; ///< 대상별 텍스처 압축 형식 목록
    };

    SW_SERVICE_SLOT_CONST( EngineConfig, Required );
} // namespace sw
