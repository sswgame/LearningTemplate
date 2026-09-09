#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

    REFLECT()
    struct SW_API WindowConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _title{ "SWEngine" };

        PROPERTY()
        string _clearColor{ "0.12 0.15 0.18 1.0" };

        PROPERTY()
        uint32 _width{ 1280 };

        PROPERTY()
        uint32 _height{ 720 };

        PROPERTY()
        RHIBackend _defaultRHI{ RHIBackend::DirectX12 };

        PROPERTY()
        bool _bVSync{ false };
    };

    REFLECT()
    struct SW_API EngineConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _engineData{ "engine/data/enginedata.xml" };

        PROPERTY()
        WindowConfig _window;

        /** @brief 한 프레임이 인정하는 최대 가변 델타(초). 디버거 정지 같은 긴 멈춤을 잘라낸다. */
        PROPERTY()
        float32 _maxFrameDeltaTime{ 0.1f };

        /** @brief 고정 주기 업데이트 한 스텝의 길이(초). 기본 60Hz. */
        PROPERTY()
        float32 _fixedDeltaTime{ 1.0f / 60.0f };

        /**
         * @brief 한 프레임이 돌릴 수 있는 고정 스텝 수 상한.
         * @details 상한을 넘긴 잔여 시간은 버린다 — 남기면 느린 프레임이 더 많은 스텝을 불러
         *          더 느려지는 되먹임이 된다. FrameTimeline 이 이 값을 집행한다.
         */
        PROPERTY()
        uint32 _maxFixedStepPerFrame{ 6 };

        PROPERTY()
        vector<string> _listResourcePriority{ "game", "common", "engine", "editor" };
    };
} // namespace sw
