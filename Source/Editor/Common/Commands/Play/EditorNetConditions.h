/**
 * @file EditorNetConditions.h
 * @brief 회선 조절(서버 디버깅 S4)의 패널 값과 자식 프로세스에 보내는 명령 글을 정의합니다. ImGui 에 의존하지 않습니다.
 * @details Multiplayer 창 Conditions 탭과 개발 명령 `netplay.command` 가 쓰고, `EditorTest` 가 프리셋 표 · 명령 글 · 대상 읽기를 테스트한다.
 *          조건 자체(프리셋 값, 덮어쓰기 글의 키)는 Core `NetEmulationConditions` 가 정본이고, 여기는 패널 단위(ms · % · KB/s)로 옮기는 일만 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct NetEmulationConditions;
} // namespace sw

namespace sw::editor
{
    /** @brief 회선 조건을 적용할 방향입니다. 대상 프로세스에서 본 방향이다 — 클라이언트의 Up 은 클라이언트 → 서버입니다. */
    enum class EditorNetLinkDirection : uint8
    {
        Up = 0, ///< 보내는 쪽(`NetEmulationTransport::setDefaultConditions`)
        Down,   ///< 받는 쪽(`NetEmulationTransport::setIncomingConditions`)
        Both
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 한 방향 회선의 패널 값입니다(슬라이더 단위). */
    struct EditorNetLinkSettings
    {
        float32 _latencyMilliseconds{ 0.0f };
        float32 _jitterMilliseconds{ 0.0f };
        float32 _lossPercent{ 0.0f };
        int32   _bandwidthKilobytesPerSecond{ 0 }; ///< 0 이면 제한 없음
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패널의 프리셋 단추 하나입니다. `_pName` 은 Core 프리셋 이름(`NetEmulationConditions::tryGetPreset`)입니다. */
    struct EditorNetPresetRow
    {
        const utf8* _pName;
        const utf8* _pLabel;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorNetConditionsUtil
     * @brief 프리셋 표, 패널 값 ↔ 조건 변환, 자식 프로세스에 보내는 `net.emulation` 명령 글을 만듭니다.
     */
    struct EditorNetConditionsUtil
    {
        /** @brief 패널 프리셋 단추(언리얼 Net Emulation 프로필 · 유니티 Network Simulator 프리셋의 자리). 값은 Core 표에 있다. */
        static constexpr EditorNetPresetRow kArrPresetRow[] = {
            {         "off",          "Off"},
            {         "lan",          "LAN"},
            {        "wifi",        "Wi-Fi"},
            {         "lte",          "LTE"},
            {     "badwifi",    "Bad Wi-Fi"},
            {"disconnected", "Disconnected"},
        };
        /** @brief 슬라이더 상한입니다(한 방향). */
        static constexpr float32 kMaxLatencyMilliseconds         = 1000.0f;
        static constexpr float32 kMaxJitterMilliseconds          = 500.0f;
        static constexpr int32   kMaxBandwidthKilobytesPerSecond = 10240;
        /** @brief 스파이크(모두 버리기)의 기본 길이(초)입니다. */
        static constexpr float32 kDefaultSpikeSeconds = 3.0f;

        /** @brief 조건에서 패널 값을 만듭니다(지연 · 흔들림 · 손실 · 대역폭 — 중복 · 순서 바꿈은 패널에 없다). */
        static EditorNetLinkSettings makeLinkSettings( const NetEmulationConditions& conditions );
        /** @brief 패널 값을 조건으로 바꿉니다. 패널에 없는 칸은 기본값입니다. */
        static NetEmulationConditions makeConditions( const EditorNetLinkSettings& settings );

        /** @brief `net.emulation <up|down|both> latencyMs=… …` — 그 방향을 이 값으로 둡니다(앞 상태와 상관없이). */
        static string makeLinkCommand( EditorNetLinkDirection direction, const EditorNetLinkSettings& settings );
        /** @brief `net.emulation both <프리셋>` — 두 방향을 프리셋으로 둡니다(양쪽이 같은 값이면 왕복이 그 회선이다). */
        static string makePresetCommand( string_view presetName );
        /** @brief `net.emulation spike <초>` — 그동안 보내고 받는 것을 모두 버립니다. */
        static string      makeSpikeCommand( float32 seconds );
        static const utf8* toString( EditorNetLinkDirection direction );

        /**
         * @brief 명령 대상 글(`server` · `client<N>` · `clients` · `all` · 프로세스 번호)을 프로세스 번호 구간으로 읽습니다.
         * @details 0 번이 서버이고 클라이언트 N 은 N 번이다. 구간이 비거나 없는 번호면 false 입니다.
         */
        [[nodiscard]] static bool tryParseTarget( string_view text, int32 processCount, int32& outFirstIndex, int32& outLastIndex );
    };
} // namespace sw::editor
