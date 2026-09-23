#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

    /**
     * @struct WindowConfig
     * @brief 주 창과 기본 렌더링 백엔드 설정입니다.
     */
    REFLECT()
    struct SW_API WindowConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _title{ "SWEngine" }; ///< 창 제목

        PROPERTY()
        string _clearColor{ "0.12 0.15 0.18 1.0" }; ///< 백버퍼 클리어 색(공백 또는 쉼표로 구분한 RGBA)

        PROPERTY()
        uint32 _width{ 1280 }; ///< 클라이언트 영역 너비(픽셀)

        PROPERTY()
        uint32 _height{ 720 }; ///< 클라이언트 영역 높이(픽셀)

        PROPERTY()
        RHIBackend _defaultRHI{ RHIBackend::DirectX12 }; ///< 명령줄이 고르지 않았을 때 쓸 백엔드

        PROPERTY()
        bool _bVSync{ false }; ///< 수직 동기화 여부
    };

    /**
     * @struct EngineConfig
     * @brief `Config/Engine/EngineConfig.json` 이 담는 엔진 기동 설정입니다.
     */
    REFLECT()
    struct SW_API EngineConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _engineData{ "engine/data/enginedata.xml" }; ///< 엔진 셸 부트스트랩 XML(리소스 경로)

        PROPERTY()
        WindowConfig _window; ///< 창·백엔드 설정

        /**
         * @brief 한 프레임이 인정하는 최대 가변 델타(초)입니다. 디버거 정지 같은 긴 멈춤을 잘라 냅니다.
         * @note 아래 셋은 0 이하여도 기동을 막지 않습니다. `FrameTimeline::configure` 가 그 자리에서
         *       내장 기본값으로 바꿉니다. 설정 파일 하나 때문에 프레임 루프가 서지 못하는 일을 막으려는 것입니다.
         */
        PROPERTY()
        float32 _maxFrameDeltaTime{ 0.1f };

        /** @brief 고정 주기 업데이트 한 스텝의 길이(초)입니다. 기본은 60Hz 입니다. */
        PROPERTY()
        float32 _fixedDeltaTime{ 1.0f / 60.0f };

        /**
         * @brief 한 프레임이 돌릴 수 있는 고정 스텝 수의 상한입니다.
         * @details 상한을 넘긴 남은 시간은 버립니다. 남기면 느린 프레임이 더 많은 스텝을 불러
         *          더 느려지는 악순환이 됩니다. FrameTimeline 이 이 값을 적용합니다.
         */
        PROPERTY()
        uint32 _maxFixedStepPerFrame{ 6 };

        PROPERTY()
        vector<string> _listResourcePriority{ "game", "common", "engine", "editor" }; ///< 리소스 팩 탐색 우선순위(앞이 먼저)
    };
} // namespace sw
