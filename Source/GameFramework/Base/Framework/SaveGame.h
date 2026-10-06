/**
 * @file SaveGame.h
 * @brief 리플렉션 기반 세이브 베이스(사용자 파일 — 로컬 저장 슬롯 위)와 직렬화 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) SaveGameSerializer — 임의의 REFLECT() 객체를 세이브 바이트로(봉투는 로컬 저장이 씌운다)
    // ------------------------------------------------------------------------------
    /**
     * @brief 임의의 리플렉션 객체(구조체 · 클래스)를 세이브 바이트로 직렬화 · 역직렬화하는 유틸리티입니다.
     * @details 바이트만 만든다 — 형식 판 · 체크섬 · 압축 · 봉인은 로컬 저장의 봉투(`LocalSlotEnvelope`)가 갖는다. 옛 SAV1 파일(머리 · CRC)은 읽지 않는다.
     */
    struct SW_GF_API SaveGameSerializer
    {
        /**
         * @brief 세이브 직렬화 문맥입니다 — `PROPERTY( SaveGame )` 이 하나라도 있는 타입은 그것만 쓰고 읽습니다(없는 타입은 전부).
         * @details 읽을 때 세이브에 없는 칸은 지금 값 그대로입니다(기본값으로 되돌리지 않는다).
         */
        static SerializeContext makeSaveContext()
        {
            SerializeContext ctx = SerializeContext::deriveFromDefault();
            ctx.setSaveGameOnly( true );
            return ctx;
        }

        /** @brief 리플렉션 객체를 세이브 바이트로 씁니다. */
        template <typename T>
        [[nodiscard]] static bool writeBytes( const T& saveObject, vector<uint8>& outBytes )
        {
            Archive archive;
            if ( archive.serializeObject( saveObject, makeSaveContext() ) == false )
                return false;
            outBytes.assign( archive.getData(), archive.getData() + archive.getSize() );
            return true;
        }

        /** @brief 세이브 바이트에서 리플렉션 객체를 복원합니다. */
        template <typename T>
        [[nodiscard]] static bool readBytes( T& outSaveObject, const uint8* pData, size_t size )
        {
            Archive archive( pData, static_cast<uint64>( size ) );
            return archive.deserializeObject( outSaveObject, makeSaveContext() );
        }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) SaveGame —모든 세이브 데이터의 순수 리플렉션 베이스 클래스
    // ------------------------------------------------------------------------------
    /**
     * @brief 사용자 파일(외형 프리셋 · 키 바인딩 · 옵션)의 베이스입니다. 만들 수 없는 기반이라 `REFLECT( Abstract )` 로 등록합니다.
     * @details 저장은 로컬 저장 슬롯(`ILocalStore`) 위다 — `saveToSlot` 이 맡기고, 거둔 완료를 `loadFromCompletion` 이 읽는다(게임 스레드는 디스크를 기다리지 않는다).
     *          바이트 형식은 파생 타입이 정합니다 — 리플렉션 세이브는 `SaveGameSerializer::writeBytes( *this, out )` 처럼 **자기 타입**으로 부릅니다.
     *          여기서 `*this` 로 부르면 템플릿 인자가 `SaveGame` 이 되어 이 타입의 TypeInfo(프로퍼티 0)로 빈 페이로드를 쓰고도 성공을 돌려줍니다.
     *          그래서 기본 구현을 두지 않습니다(순수 가상). 게임 상태(진행 · 세계)는 이것이 아니라 스냅숏 봉투(`GameInstanceBase::saveStateToFile`)다.
     */
    REFLECT( Abstract )
    class SW_GF_API SaveGame
    {
    public:
        REFLECT_BODY();

        SaveGame()                                 = default;
        virtual ~SaveGame()                        = default;
        SaveGame( const SaveGame& )                = default;
        SaveGame& operator=( const SaveGame& )     = default;
        SaveGame( SaveGame&& ) noexcept            = default;
        SaveGame& operator=( SaveGame&& ) noexcept = default;

        /** @brief 이 타입의 바이트 형식 판입니다 — 봉투에 적히고, 읽을 때 다르면 거절한다(옛 판 리더를 두지 않는다). */
        virtual uint32 getFormatVersion() const { return 1; }
        /** @brief 세이브 바이트를 씁니다. 파생 타입이 자기 형식(리플렉션 세이브는 자기 타입)으로 씁니다. */
        [[nodiscard]] virtual bool writeBytes( vector<uint8>& outBytes ) const = 0;
        /** @brief 세이브 바이트를 읽습니다. `writeBytes` 와 같은 형식입니다. */
        [[nodiscard]] virtual bool readBytes( const uint8* pData, size_t size ) = 0;

        /**
         * @brief 슬롯에 쓰기를 맡깁니다. 요청 id(완료를 짝지을 때)이고, 바이트를 만들지 못하면 0 입니다.
         * @param options 압축 · 봉인 — 형식 판은 `getFormatVersion()` 으로 채운다
         */
        uint64 saveToSlot( ILocalStore& store, string_view slot, const LocalStoreWriteOptions& options ) const;
        /** @brief 읽기 완료에서 불러옵니다. 읽기가 실패했거나 형식 판이 다르거나 바이트가 깨졌으면 false(지금 값은 그대로일 수 있다). */
        [[nodiscard]] bool loadFromCompletion( const LocalStoreCompletion& completion );
    };
} // namespace sw
