/**
 * @file EditorNetPlaySettings.h
 * @brief 멀티 프로세스 재생 구성(`EditorNetPlaySettings`)과 그 구성을 이름 붙여 저장하는 사용자 프리셋 파일의 형식입니다. ImGui 에 의존하지 않습니다.
 * @details 리플렉션 타입이라 파일 읽기 · 쓰기가 프로퍼티 그래프를 따른다(`EditorNetPlayPresetStore`). 칸을 더하면 저장도 따라오므로 손으로 직렬화하지 않는다.
 *          모든 칸이 `Required` 다 — 사용자 프리셋 파일에 칸이 빠졌으면 그 파일은 깨진 것으로 보고(`EditorUserFile`) 조용히 코드 기본값을 넣지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 재생의 네트워크 모드입니다(언리얼 PIE 의 Net Mode). */
    ENUM()
    enum class EditorNetMode : uint8
    {
        Standalone = 0,  ///< 에디터 안에서 재생한다. 이 세션을 쓰지 않으며 `start` 가 false 를 반환한다
        DedicatedServer, ///< `Server` 하나와 `App` 클라이언트 N 개
        ListenHost       ///< 첫 `App` 이 리슨 호스트이고 나머지가 `App` 클라이언트이다. 플레이어 수에 호스트가 포함된다
    };

    /** @brief 메뉴 · 로그에 보이는 이름입니다(정의는 `EditorNetPlaySession.cpp`). */
    const utf8* toString( EditorNetMode mode );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 세션 하나의 구성입니다. 패널의 프리셋이 이 구성을 저장합니다.
     * @details 창 배치 필드(`_bArrangeWindows` …)은 `App` 프로세스(리슨 호스트 · 클라이언트)마다 창 크기와 위치 인자(`-W` · `-H` · `-WinX` · `-WinY`)를
     *          나란히 준다(`EditorNetPlaySession::makeWindowArguments`). 언리얼 PIE 가 클라이언트 창을 겹치지 않게 놓는 것에 해당한다.
     */
    REFLECT()
    struct EditorNetPlaySettings
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _serverExecutablePath{}; ///< 비어 있으면 에디터 실행 파일 옆의 `Server`
        PROPERTY( Required )
        string _clientExecutablePath{}; ///< 비어 있으면 에디터 실행 파일 옆의 `App`
        PROPERTY( Required )
        string _clientArguments{}; ///< 클라이언트마다 붙이는 인자(백엔드 스위치). 창 크기 · 위치는 창 배치 칸이 준다
        PROPERTY( Required )
        string _traceDirectory{}; ///< 비어 있으면 `Saved/Traces/netplay_<유닉스 밀리초>`
        PROPERTY( Required, Min = 1.0 )
        float32 _stopTimeoutSeconds{ 15.0f };
        PROPERTY( Required, Min = 1 )
        int32 _clientCount{ 2 }; ///< 플레이어 수. 전용 서버이면 클라이언트 수이고, 리슨 호스트이면 호스트를 포함한 수이다
        PROPERTY( Required, Min = 64 )
        int32 _windowWidth{ 960 }; ///< 창 배치를 켜면 `App` 창마다 주는 클라이언트 영역 너비(픽셀)
        PROPERTY( Required, Min = 64 )
        int32 _windowHeight{ 540 }; ///< 위와 같다(높이)
        PROPERTY( Required, Min = 1 )
        int32 _windowColumnCount{ 2 }; ///< 창을 한 줄에 놓는 수. 넘으면 다음 줄로 내려간다
        PROPERTY( Required )
        uint16 _port{ 0 }; ///< 0 이면 비어 있는 UDP 포트를 고른다
        PROPERTY( Required )
        EditorNetMode _mode{ EditorNetMode::Standalone };
        PROPERTY( Required )
        bool _bRecordTraces{ true };
        PROPERTY( Required )
        bool _bArrangeWindows{ true }; ///< `App` 창을 나란히 놓는다. 끄면 창 크기 · 위치 인자를 주지 않는다(창은 플랫폼이 놓는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 이름 붙인 구성 하나입니다(언리얼 Play 설정의 사용자 프리셋). */
    REFLECT()
    struct EditorNetPlayPreset
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{}; ///< 비어 있지 않고 파일 안에서 하나뿐이다
        PROPERTY( Required )
        EditorNetPlaySettings _settings{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 사용자 프리셋 파일(`Saved/Editor/NetPlayPresets.json`, 커밋하지 않는다) 하나의 내용입니다.
     * @details 결정 기록 D7-3.3 — 언리얼 `EditorPerProjectUserSettings`, 유니티 `UserSettings` 와 같은 사용자별 파일이다.
     */
    REFLECT()
    struct EditorNetPlayPresetFile
    {
        REFLECT_BODY();

        PROPERTY( Required )
        vector<EditorNetPlayPreset> _listPreset{};
        PROPERTY( Required )
        string _selectedPresetName{}; ///< 마지막으로 고른 프리셋. 비어 있으면 고른 것이 없다. 비어 있지 않으면 `_listPreset` 에 있어야 한다
    };
} // namespace sw::editor
