/**
 * @file GimmickStateBundle.h
 * @brief 씬의 모든 기믹 회로 상태 바이트(`GimmickCircuit::saveState`)를 소유 오브젝트 키와 함께 한 덩어리로 묶고 푸는 코덱입니다 — 롤백 저장 · 네트워크 스냅샷 · 세이브가 같이 씁니다.
 * @details 회로는 고정 스텝의 결정적 상태 기계이고 상태가 바이트 하나라서, 이 묶음을 게임의 `IRollbackGame::saveState` / `loadState` 에 싣거나 네트워크 스냅샷의
 *          엔티티(`GimmickSnapshotUtil`, 네트워크 키트)로 보내면 문과 엘리베이터와 타이머가 같은 걸음에서 이어진다.
 *          **키는 소유 오브젝트 이름의 해시**다. 오브젝트 id 는 기계마다 다를 수 있지만, 같은 씬을 읽은 두 기계의 오브젝트 이름은 매니저가 같은 규칙으로 유일하게 만든다.
 *          묶음은 키 오름차순이라 같은 씬이면 같은 바이트다. 회로 구조가 다른 바이트(해시 · 크기가 다름)는 그 항목만 적용하지 않고 센다 — 모르는 오브젝트나 데이터를 고친 회로가 다른 항목을 막지 않는다.
 *
 *          바이트 형식(리틀 엔디언): `u32 magic("GMSB") u32 version u32 count`, 항목마다 `u64 ownerKey u32 length u8 bytes[length]`.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 회로 하나의 상태입니다. */
    struct GimmickStateEntry
    {
        vector<uint8> _bytes{};       ///< `GimmickCircuitComponent::getStateBytes`
        uint64        _ownerKey{ 0 }; ///< 소유 오브젝트 이름의 해시(`GimmickStateBundle::makeOwnerKey`)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GimmickStateBundle
     * @brief 씬의 회로 상태 묶음입니다. 키 오름차순으로 보관합니다.
     */
    struct SW_GF_API GimmickStateBundle
    {
        static constexpr uint32 kMagic         = FourCcUtil::make( "GMSB" );
        static constexpr uint32 kVersion       = 1;
        static constexpr uint32 kMaxEntryBytes = 1u << 20; ///< 항목 하나의 상한. 넘는 길이는 손상된 묶음으로 본다
        static constexpr uint32 kMaxEntryCount = 1u << 16; ///< 항목 수의 상한. 손상된 묶음이 할당을 키우지 않게 한다

        vector<GimmickStateEntry> _listEntry{};

        /** @brief 오브젝트 이름의 키입니다(대소문자를 가리지 않는 해시). */
        static uint64 makeOwnerKey( const hashed_string& objectName ) { return static_cast<uint64>( objectName.getHash() ); }

        /** @brief 씬의 회로(컴포넌트 레지스트리)에서 지금 상태를 모읍니다. 만들어지지 않은 회로(`isBuilt` 가 거짓)는 건너뜁니다. 기존 내용은 지웁니다. */
        void collect( const GameObjectManager& manager );
        /**
         * @brief 묶음의 상태를 씬의 회로에 되돌립니다(액추에이터 자세도 다시 적용). 오브젝트가 없거나 회로가 없거나 구조가 다른 항목은 적용하지 않고 @p pOutSkippedCount 에 셉니다.
         * @return 적용한 회로 수입니다.
         */
        uint32 apply( GameObjectManager& manager, uint32* pOutSkippedCount = nullptr ) const;

        /** @brief 바이트로 씁니다(`_listEntry` 순서 그대로). */
        void write( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 머리 · 길이 · 상한이 틀리거나 남는 바이트가 있으면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool read( const uint8* pData, size_t size );
        [[nodiscard]] bool read( const vector<uint8>& bytes ) { return read( bytes.data(), bytes.size() ); }

        /** @brief 항목들의 해시입니다(키와 바이트). 두 기계의 회로가 같은 상태인지 비교합니다. */
        uint64 computeHash() const;
        /** @brief 키의 항목입니다. 없으면 nullptr 입니다. */
        const GimmickStateEntry* findEntry( uint64 ownerKey ) const;
    };
} // namespace sw
