/**
 * @file GimmickStateBundle.h
 * @brief 씬의 모든 기믹 회로 상태(`GimmickCircuitState`)를 소유 오브젝트 키와 함께 한 덩어리로 묶습니다. 롤백 저장과 네트워크 스냅샷이 씁니다.
 * @details 회로는 고정 스텝의 결정적 상태 기계라서, 이 번들을 게임의 `IRollbackGame::saveState` / `loadState` 에 싣거나 네트워크 스냅샷의
 *          엔티티(`GimmickSnapshotUtil`, 네트워크 키트)로 보내면 문과 엘리베이터와 타이머가 같은 걸음에서 이어진다. 레벨 세이브는 이것을 쓰지 않는다
 *          (컴포넌트의 `_state` 가 오브젝트 상태와 함께 자기 기술형으로 저장된다).
 *          **키는 소유 오브젝트 이름의 해시**다. 오브젝트 id 는 기계마다 다를 수 있지만, 같은 씬을 읽은 두 기계의 오브젝트 이름은 매니저가 같은 규칙으로 유일하게 만든다.
 *          번들은 키 오름차순이라 같은 씬이면 같은 바이트다.
 *
 *          바이트는 고정 배치형(`FixedLayout`, 같은 빌드끼리만)이다. 직접 작성한 코덱, 매직, 버전을 두지 않는다. 머리의 스키마 해시가 다른 빌드의 바이트를 거절한다.
 *          게임이 번들을 디스크에 남기려면 같은 타입을 `BinarySerializer` 로 쓴다(형식을 하나 더 만들지 않는다).
 *          지금 이 번들을 부르는 게임 코드는 없고 테스트뿐이다. 롤백이나 스냅샷을 싣는 게임이 생기면 그 워크로드로 크기와 시간을 다시 측정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitState.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 회로 하나의 상태입니다. */
    REFLECT()
    struct SW_GF_API GimmickStateEntry
    {
        REFLECT_BODY();

        PROPERTY()
        uint64 _ownerKey{ 0 }; ///< 소유 오브젝트 이름의 해시(`GimmickStateBundle::makeOwnerKey`)
        PROPERTY()
        GimmickCircuitState _state{}; ///< `GimmickCircuitComponent::getState`
    };
} // namespace sw

namespace sw
{
    /** @brief 씬의 회로 상태 번들입니다. 키 오름차순으로 보관합니다. */
    REFLECT()
    struct SW_GF_API GimmickStateBundle
    {
        REFLECT_BODY();

        /** @brief 항목 수의 상한입니다. `_listEntry` 의 `MaxCount` 와 같은 값입니다. */
        static constexpr uint32 kMaxEntryCount = 65536;

        PROPERTY( MaxCount = 65536 )
        vector<GimmickStateEntry> _listEntry{};

        /** @brief 오브젝트 이름의 키입니다(대소문자를 가리지 않는 해시). */
        static uint64 makeOwnerKey( const hashed_string& objectName ) { return static_cast<uint64>( objectName.getHash() ); }

        /** @brief 씬의 회로(컴포넌트 레지스트리)에서 지금 상태를 모읍니다. 만들어지지 않은 회로(`isBuilt` 가 거짓)는 건너뜁니다. 기존 내용은 지웁니다. */
        void collect( const GameObjectManager& manager );
        /**
         * @brief 번들의 상태를 씬의 회로에 되돌립니다(액추에이터 자세도 다시 적용). 같은 빌드, 같은 회로의 상태만 적용합니다(`GimmickCircuitComponent::restoreState`).
         * @details 오브젝트가 없거나, 회로가 없거나, 회로와 정확히 맞지 않는 항목은 적용하지 않고 @p pOutSkippedCount 에 센다. 그 수의 의미는 부르는 쪽이 정한다.
         *          자기 기계가 저장한 롤백 상태라면 0 이 아닌 수는 결함이고, 다른 기계의 스냅샷이라면 아직 회로를 만들지 않은 오브젝트일 수 있다.
         * @return 적용한 회로 수입니다.
         */
        uint32 apply( GameObjectManager& manager, uint32* pOutSkippedCount = nullptr ) const;

        /** @brief 고정 배치형 바이트로 씁니다(`_listEntry` 순서 그대로). 선언한 범위 밖 값(상한을 넘는 항목 수 등)이 있으면 false 이고 @p outBytes 는 비어 있습니다. */
        [[nodiscard]] bool write( vector<uint8>& outBytes ) const;
        /** @brief 고정 배치형 바이트를 읽습니다. `Ok` 가 아니면 번들은 그대로입니다. */
        [[nodiscard]] FixedReadResult read( const uint8* pData, size_t size );
        [[nodiscard]] FixedReadResult read( const vector<uint8>& bytes ) { return read( bytes.data(), bytes.size() ); }

        /** @brief 번들의 고정 배치형 바이트 해시입니다. 두 기계의 회로가 같은 상태인지 비교합니다. */
        uint64 computeHash() const;
        /** @brief 키의 항목입니다. 없으면 nullptr 입니다. */
        const GimmickStateEntry* findEntry( uint64 ownerKey ) const;
    };
} // namespace sw
