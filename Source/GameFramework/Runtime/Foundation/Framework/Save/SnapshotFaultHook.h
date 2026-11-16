/**
 * @file GameFramework/Runtime/Foundation/Framework/Save/SnapshotFaultHook.h
 * @brief 상태 스냅샷 복원에 결함을 주입하는 자리입니다 — 스냅샷은 무엇이 주입하는지 모르고 이 창구만 묻습니다.
 * @details 구현은 개발 전용 소스(`Save/Dev/SnapshotFaultLaunchHook.cpp` — 테스트 전역 변수 `gv_saveRestoreBreakReplace`)가 정적 등록으로 하나 건다.
 *          배포는 그 소스를 짓지 않아 창구가 없고 복원은 결함 없이 돈다(`IGameSettingsOverride` 와 같은 갈림 — 런타임은 일반 창구만 두고 개발 쪽이 스스로 붙는다).
 *          언리얼이 저장 실패 흉내를 개발 콘솔 변수로 두고 배포에서 닫는 것과 같은 자리다(기억에 의존).
 *          GameFramework 는 리로드하지 않는 공유 모듈이라 등록은 이미지 수명 동안 하나다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Registry/RegistrationList.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class ISnapshotFaultHook @brief 스냅샷 복원에 결함을 주입하는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_GF_API ISnapshotFaultHook
    {
    public:
        virtual ~ISnapshotFaultHook() = default;

        /** @brief 교체 단계(검증을 지난 뒤 활성 씬을 비운 자리)를 지게 할지 답합니다 — 깨진 복원 뒤 세이브 막기 · 씬 다시 열기를 시나리오가 밟는 길이다. */
        [[nodiscard]] virtual bool shouldBreakReplace() const = 0;

        /** @brief 등록된 창구입니다. 개발 전용 소스가 없으면(배포) nullptr 입니다. */
        [[nodiscard]] static ISnapshotFaultHook* find();

        /** @brief 창구를 겁니다(정적 등록자가 부른다). 이미 다른 창구가 걸려 있으면 공용 거절 로그를 남기고 false 입니다 — 이미지에 하나다. */
        [[nodiscard]] static bool attach( ISnapshotFaultHook* pFaultHook );
        /** @brief 걸린 창구를 뗍니다(건 이미지가 내려갈 때). 걸려 있지 않으면 할 일이 없다. */
        static void detach( ISnapshotFaultHook* pFaultHook );

        /** @struct Registration @brief 개발 전용 소스가 정적 객체로 창구를 겁니다(이미지에 하나) — 공통 틀 `StaticRegistrar` 라 건 이미지가 내려가면 뗀다. */
        struct Registration final : StaticRegistrar<ISnapshotFaultHook*>
        {
            explicit Registration( ISnapshotFaultHook& faultHook )
                : StaticRegistrar{ &faultHook, &ISnapshotFaultHook::attach, &ISnapshotFaultHook::detach }
            {
            }
        };
    };
} // namespace sw
