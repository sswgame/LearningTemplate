/**
 * @file NetEmulationPresetTable.h
 * @brief 회선 흉내 프리셋 표(`engine/settings/netemulation.presets.xml`)입니다. 개발 명령 · 에디터 회선 조절 · 부하 시나리오가 이 표 하나를 읽습니다.
 * @details 언리얼 `DefaultEngine.ini` 의 `[PacketSimulationProfile.*]`, 유니티 Network Simulator 프리셋 자산과 같은 자리입니다. 값은 한 방향(보내는 쪽)이고,
 *          표는 이름 · 표시 이름 · 여섯 값만 듭니다. 흉내 자체(`NetEmulationConditions`)는 Core 가 하고 이 표는 그 조건을 만듭니다(Core 는 리플렉션 아래 티어라 표를 읽지 못합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct NetEmulationConditions;

    /** @brief 회선 프리셋 하나입니다(한 방향). 단위는 전역 변수 `gv_netEmu*` 와 같습니다. */
    REFLECT()
    struct SW_API NetEmulationPresetDef
    {
        REFLECT_BODY();

        PROPERTY()
        string _name; ///< 명령 · 시나리오가 적는 이름(대소문자 무시, 예: `lte`)

        PROPERTY()
        string _label; ///< 에디터 단추에 보이는 이름(예: `LTE`)

        PROPERTY( Min = 0 )
        int32 _latencyMs{ 0 }; ///< 한쪽 지연(ms)

        PROPERTY( Min = 0 )
        int32 _jitterMs{ 0 }; ///< ± 흔들림(ms)

        PROPERTY( Min = 0, Max = 100 )
        float32 _lossPercent{ 0.0f };

        PROPERTY( Min = 0, Max = 100 )
        float32 _duplicatePercent{ 0.0f };

        PROPERTY( Min = 0, Max = 100 )
        float32 _reorderPercent{ 0.0f };

        PROPERTY( Min = 0 )
        int32 _bandwidthKilobytesPerSecond{ 0 }; ///< 0 이면 제한 없음

        /** @brief 이 프리셋의 조건입니다(나머지 칸은 `NetEmulationConditions` 기본값). */
        NetEmulationConditions makeConditions() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 회선 프리셋 표입니다. 쓰는 쪽이 필요할 때 읽어 들고 있습니다(파일이 작고 개발 도구만 씁니다).
     * @details 읽을 때 모르는 이름 · 범위 밖 값 · 빈 표 · 이름 중복을 오류로 알립니다(`ReflectedXMLFile`). 코드에 내장 프리셋이 없습니다.
     */
    REFLECT()
    struct SW_API NetEmulationPresetTable
    {
        REFLECT_BODY();

        static constexpr const utf8* kResourcePath = "engine/settings/netemulation.presets.xml"; ///< 엔진 팩의 표

        PROPERTY()
        vector<NetEmulationPresetDef> _listPreset;

        /** @brief @p path 의 표를 읽습니다. 실패 이유는 로그에 남기고 false 입니다(그때 표는 비어 있습니다). */
        [[nodiscard]] bool loadFromResource( string_view path = kResourcePath );
        /** @brief 이름(대소문자 무시)으로 프리셋을 찾습니다. 없으면 nullptr 입니다. */
        const NetEmulationPresetDef* findPreset( string_view name ) const;
        /** @brief 이름으로 조건을 만듭니다. 없는 이름이면 false 이고 @p outConditions 는 그대로입니다. */
        [[nodiscard]] bool tryGetConditions( string_view name, NetEmulationConditions& outConditions ) const;
        /** @brief 프리셋 이름을 쉼표로 이은 글입니다(도움말 · 오류 문구). */
        string makePresetNames() const;
    };
} // namespace sw
