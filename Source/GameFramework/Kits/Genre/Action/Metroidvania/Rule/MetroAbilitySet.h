/**
 * @file MetroAbilitySet.h
 * @brief 능력 잠금을 다룹니다. 얻은 능력(대시, 2단 점프, 벽 점프, 갈고리 등)이 기반 `PlatformerMotor2D` 의 설정을 바꾸고, `GameFlags` 에 플래그를 켜서 `AreaGraph` 의 길 잠금을 엽니다.
 * @details 몸은 항상 "잠긴 기본 설정"(벽 점프, 공중 점프, 대시 없음)에서 시작한다. 얻은 능력의 `_motor` 값을 얻은 순서대로 더해서 새 설정을 만든다.
 *          같은 능력 집합이면 항상 같은 설정이 나온다(결정적). 세이브는 `GameFlags` 하나로 충분하며, `restoreFromFlags` 가 플래그에서 능력을 복원한다.
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

        /** @brief 카탈로그와 기본 몸 설정을 설정합니다. 기본 설정의 벽 점프, 공중 점프, 공중 대시, 대시는 잠겨 있고(능력이 연다), 얻은 능력은 비웁니다. */
        void initialize( const MetroidvaniaCatalog* pCatalog, const PlatformerSettings& baseSettings );

        /** @brief 능력을 얻고 플래그를 켭니다. 새로 얻었으면 true 를 반환하고, 이미 있거나 모르는 능력이면 false 를 반환합니다. */
        bool grantAbility( const hashed_string& abilityID, GameFlags& flags );
        /** @brief 세이브에서 복원합니다. 플래그가 켜진 능력을 카탈로그 순서로 복원하며(지금 능력은 비웁니다), 복원한 수를 반환합니다. */
        int32 restoreFromFlags( const GameFlags& flags );
        bool  hasAbility( const hashed_string& abilityID ) const;

        /** @brief 잠긴 기본 설정에 얻은 능력의 값을 더한 몸 설정을 반환합니다. */
        void applyToSettings( PlatformerSettings& outSettings ) const;
        /** @brief `applyToSettings` 결과를 몸에 적용합니다(능력을 얻은 뒤 한 번 호출합니다). */
        void applyToMotor( PlatformerMotor2D& motor ) const;
        /** @brief 얻지 않은 동작의 입력을 지웁니다. 기반 몸에는 땅 대시를 끄는 설정이 없어서 여기서 막습니다. */
        PlatformerInput filterInput( const PlatformerInput& input ) const;
        /** @brief 대시가 열렸는지(`dash` 값이 0이 아닌 능력을 얻었는지) 반환합니다. */
        bool canDash() const;

        const vector<hashed_string>& getAbilities() const { return _listAbility; }

        /** @brief 얻은 능력 id(얻은 순서)를 씁니다. 카탈로그와 기본 몸 설정은 `initialize` 에서 받은 것이고 능력 플래그는 빌려 온 `GameFlags` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다(몸에 적용하는 것은 호출하는 쪽이 `applyToMotor` 로 합니다). 카탈로그에 없는 능력이거나 겹친 능력이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 능력의 `<Motor>` 에 쓸 수 있는 이름인지 반환합니다(카탈로그가 읽을 때 오타를 알립니다). */
        static bool isMotorSettingName( const hashed_string& name );

    private:
        const MetroidvaniaCatalog* _pCatalog;
        PlatformerSettings         _baseSettings;
        vector<hashed_string>      _listAbility; ///< 얻은 순서
    };
} // namespace sw
