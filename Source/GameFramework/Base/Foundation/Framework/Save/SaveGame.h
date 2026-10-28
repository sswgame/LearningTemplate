/**
 * @file SaveGame.h
 * @brief 리플렉션 기반 세이브 베이스와 직렬화 유틸리티를 정의합니다.
 * @details 세이브는 사용자 파일이며 로컬 저장 슬롯 위에 저장한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) SaveGameSerializer: 임의의 REFLECT() 객체를 세이브 바이트로 바꾼다(봉투는 로컬 저장이 씌운다)
    // ------------------------------------------------------------------------------
    /**
     * @brief 임의의 리플렉션 객체(구조체와 클래스)를 세이브 바이트로 직렬화하고 역직렬화하는 유틸리티입니다.
     * @details 바이트만 만든다. 형식 버전, 체크섬, 압축, 봉인은 로컬 저장의 봉투(`LocalSlotEnvelope`)가 맡는다. 옛 SAV1 파일(머리와 CRC)은 읽지 않는다.
     */
    struct SW_GF_API SaveGameSerializer
    {
        /**
         * @brief 세이브 직렬화 문맥입니다. `PROPERTY( SaveGame )` 이 하나라도 있는 타입은 그것만 쓰고 읽으며, 없는 타입은 전부 씁니다.
         * @details 읽을 때 세이브에 없는 필드는 지금 값 그대로 둔다. 기본값으로 되돌리지 않는다.
         */
        static SerializeContext makeSaveContext()
        {
            return SerializeContext::deriveFromDefault( SerializePurpose::SaveGame );
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
     * @brief 사용자 파일(외형 프리셋, 키 바인딩, 옵션)의 베이스입니다. 직접 만들 수 없는 기반이라 `REFLECT( Abstract )` 로 등록합니다.
     * @details 저장은 로컬 저장 슬롯(`ILocalStore`) 위에서 한다. `saveToSlot` 이 요청하고, 회수한 완료를 `loadFromCompletion` 이 읽는다. 게임 스레드는 디스크를 기다리지 않는다.
     *          바이트 형식은 파생 타입이 정한다. 리플렉션 세이브는 `SaveGameSerializer::writeBytes( *this, out )` 처럼 **자기 타입**으로 불러야 한다.
     *          여기서 `*this` 로 부르면 템플릿 인자가 `SaveGame` 이 되어, 이 타입의 TypeInfo(프로퍼티 0)로 빈 페이로드를 쓰고도 성공을 반환한다.
     *          그래서 기본 구현을 두지 않고 순수 가상으로 둔다.
     *          게임 상태(진행과 세계)는 이것이 아니라 스냅샷 봉투(`GameInstance::saveStateToFile`)에 저장한다.
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

        /** @brief 이 타입의 바이트 형식 버전입니다. 봉투에 적히며, 읽을 때 다르면 거절합니다. 옛 버전 리더는 두지 않습니다. */
        virtual uint32 getFormatVersion() const { return 1; }
        /** @brief 세이브 바이트를 씁니다. 파생 타입이 자기 형식(리플렉션 세이브는 자기 타입)으로 씁니다. */
        [[nodiscard]] virtual bool writeBytes( vector<uint8>& outBytes ) const = 0;
        /** @brief 세이브 바이트를 읽습니다. `writeBytes` 와 같은 형식입니다. */
        [[nodiscard]] virtual bool readBytes( const uint8* pData, size_t size ) = 0;

        /**
         * @brief 슬롯에 쓰기를 맡기고 요청 id 를 반환합니다. 완료를 짝지을 때 이 id 를 쓰며, 바이트를 만들지 못하면 0 입니다.
         * @param options 압축과 봉인 옵션이다. 형식 버전은 `getFormatVersion()` 으로 채운다.
         */
        uint64 saveToSlot( ILocalStore& store, string_view slot, const LocalStoreWriteOptions& options ) const;
        /** @brief 읽기 완료에서 불러옵니다. 읽기가 실패했거나 형식 버전이 다르거나 바이트가 깨졌으면 false 이며, 그때 지금 값은 그대로일 수 있습니다. */
        [[nodiscard]] bool loadFromCompletion( const LocalStoreCompletion& completion );
    };
} // namespace sw
