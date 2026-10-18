/**
 * @file SQLLocalSlotStorage.h
 * @brief 로컬 저장소 백엔드의 SQLite 구현입니다. 테이블 `sw_local_slot( slot, bytes, written_at_ms )` 을 쓰며, 쓰기는 upsert 문 하나로 처리해 SQLite 의 원자성에 맡깁니다.
 * @details 마이그레이션은 `Resource/common/sql/localstore/` 에 있다. 서버 저장소와는 다른 DB 이고 번호 체계도 다르다. DB 파일은 `<루트>/localstore.db` 이다.
 *          기반 계층의 `LocalStoreFactory` 에 "sqlite" 이름으로 등록한다. 등록은 모듈 수명을 따른다(`.cpp` 의 정적 `LocalStoreBackendRegistration`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ISQLConnection;

    /**
     * @class SQLLocalSlotStorage
     * @brief SQLite 백엔드입니다. `ThreadedLocalStore` 의 스레드에서만 호출합니다.
     */
    class SW_KIT_API SQLLocalSlotStorage final : public ILocalSlotStorage
    {
    public:
        static constexpr const utf8* kBackendName      = "sqlite";
        static constexpr const utf8* kMigrationFolder  = "common/sql/localstore";
        static constexpr const utf8* kDatabaseFileName = "localstore.db";

        SQLLocalSlotStorage();
        ~SQLLocalSlotStorage() override;

        /** @brief @p databasePath(파일 경로나 `:memory:`)를 열고 마이그레이션을 적용합니다. 실패하면 false 와 실패 이유를 반환합니다. */
        [[nodiscard]] bool initialize( string_view databasePath, string& outError );

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        /** @brief 팩토리가 부르는 생성 함수입니다. `<rootPath>/localstore.db` 를 엽니다. */
        static unique_ptr<ILocalSlotStorage> createForRoot( string_view rootPath, string& outError );

    private:
        unique_ptr<ISQLConnection> _connection;
    };
} // namespace sw
