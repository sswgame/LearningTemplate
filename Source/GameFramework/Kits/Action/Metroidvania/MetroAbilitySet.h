/**
 * @file MetroAbilitySet.h
 * @brief 능력 잠금 — 얻은 능력(대시 · 2단 점프 · 벽 점프 · 갈고리 …)이 기반 `PlatformerMotor2D` 의 설정을 바꾸고, `GameFlags` 에 플래그를 켜 `AreaGraph` 의 길 잠금을 엽니다.
 * @details 몸은 늘 "잠긴 기본 설정"(벽 점프 · 공중 점프 · 대시 없음)에서 시작하고, 얻은 능력의 `_motor` 값을 얻은 순서대로 더해 새 설정을 만듭니다.
 *          같은 능력 집합이면 늘 같은 설정입니다(결정적). 세이브는 `GameFlags` 하나로 충분합니다 — `restoreFromFlags` 가 플래그에서 능력을 되살립니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Movement/PlatformerMotor2D.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class GameFlags;
    class MetroidvaniaCatalog;

    /**
     * @class MetroAbilitySet
     * @brief 캐릭터 하나가 얻은 능력과, 거기서 나온 몸 설정입니다.
     */
    class SW_GF_API MetroAbilitySet
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MABL" );
        static constexpr uint32 kStateVersion = 1;

        MetroAbilitySet();

        /** @brief 카탈로그와 기본 몸 설정을 둡니다. 기본 설정의 벽 점프 · 공중 점프 · 공중 대시 · 대시는 잠깁니다(능력이 연다). 얻은 능력은 비웁니다. */
        void initialize( const MetroidvaniaCatalog* pCatalog, const PlatformerSettings& baseSettings );

        /** @brief 능력을 얻습니다 — 플래그를 켭니다. 새로 얻었으면 true, 이미 있거나 모르는 능력이면 false 입니다. */
        bool grantAbility( const hashed_string& abilityId, GameFlags& flags );
        /** @brief 세이브에서 — 플래그가 켜진 능력을 카탈로그 순서로 되살립니다(지금 능력은 비운다). 되살린 수입니다. */
        int32 restoreFromFlags( const GameFlags& flags );
        bool  hasAbility( const hashed_string& abilityId ) const;

        /** @brief 잠긴 기본 설정에 얻은 능력의 값을 더한 몸 설정입니다. */
        void applyToSettings( PlatformerSettings& outSettings ) const;
        /** @brief `applyToSettings` 결과를 몸에 둡니다(능력을 얻은 뒤 한 번 부른다). */
        void applyToMotor( PlatformerMotor2D& motor ) const;
        /** @brief 얻지 않은 동작의 입력을 지웁니다 — 기반 몸은 땅 대시를 끄는 설정이 없어 여기서 막습니다. */
        PlatformerInput filterInput( const PlatformerInput& input ) const;
        /** @brief 대시가 열렸는가(`dash` 값이 0 이 아닌 능력을 얻었다)입니다. */
        bool canDash() const;

        const vector<hashed_string>& getAbilities() const { return _listAbility; }

        /** @brief 얻은 능력 id(얻은 순서)를 씁니다. 카탈로그 · 기본 몸 설정은 `initialize` 의 것, 능력 플래그는 빌린 `GameFlags` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다(몸에 거는 것은 부르는 쪽이 `applyToMotor`). 카탈로그에 없는 능력 · 겹친 능력이거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 능력의 `<Motor>` 에 쓸 수 있는 이름인가입니다(카탈로그가 읽을 때 오타를 알린다). */
        static bool isMotorSettingName( const hashed_string& name );

    private:
        const MetroidvaniaCatalog* _pCatalog;
        PlatformerSettings         _baseSettings;
        vector<hashed_string>      _listAbility; ///< 얻은 순서
    };
} // namespace sw
