/**
 * @file SqlLocalSlotStorage.h
 * @brief 로컬 저장 바닥의 SQLite 구현 — 표 `sw_local_slot( slot, bytes, written_at_ms )`, 쓰기는 upsert 한 문(SQLite 의 원자성).
 * @details 마이그레이션은 `Resource/common/sql/localstore/`(서버 저장소와 다른 DB · 다른 번호 줄). DB 파일은 `<루트>/localstore.db`.
 *          기반 `LocalStoreFactory` 에 "sqlite" 이름으로 올린다(`registerLocalStoreBackend` — 이 키트를 쓰는 게임이 시작 때 부르고, 내릴 때 `unregister…`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ISqlConnection;

    /**
     * @class SqlLocalSlotStorage
     * @brief SQLite 바닥입니다(`ThreadedLocalStore` 의 스레드에서만 불린다).
     */
    class SW_GF_API SqlLocalSlotStorage final : public ILocalSlotStorage
    {
    public:
        static constexpr const utf8* kBackendName      = "sqlite";
        static constexpr const utf8* kMigrationFolder  = "common/sql/localstore";
        static constexpr const utf8* kDatabaseFileName = "localstore.db";

        SqlLocalSlotStorage();
        ~SqlLocalSlotStorage() override;

        /** @brief @p databasePath(파일 · `:memory:`)를 열고 마이그레이션을 적용합니다. 실패하면 false 와 까닭. */
        [[nodiscard]] bool initialize( string_view databasePath, string& outError );

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        /** @brief 기반 공장에 "sqlite" 바닥을 올리고 · 내립니다. */
        [[nodiscard]] static bool registerLocalStoreBackend();
        static void               unregisterLocalStoreBackend();
        /** @brief 공장이 부르는 만들기 — `<rootPath>/localstore.db`. */
        static unique_ptr<ILocalSlotStorage> createForRoot( string_view rootPath, string& outError );

    private:
        unique_ptr<ISqlConnection> _connection;
    };
} // namespace sw
