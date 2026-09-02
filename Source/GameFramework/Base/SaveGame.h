/**
 * @file SaveGame.h
 * @brief 리플렉션 기반 세이브 베이스 및 직렬화 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) IFlagStore — 범용 플래그/변수 저장소 인터페이스 (대화, 퀘스트, 이벤트 등)
    // ------------------------------------------------------------------------------
    /** @brief 대화 컴포넌트나 퀘스트 시스템 등에서 사용하는 범용 플래그 조회/설정 인터페이스 */
    class SW_GF_API IFlagStore
    {
    public:
        IFlagStore()                                   = default;
        virtual ~IFlagStore()                          = default;
        IFlagStore( const IFlagStore& )                = default;
        IFlagStore& operator=( const IFlagStore& )     = default;
        IFlagStore( IFlagStore&& ) noexcept            = default;
        IFlagStore& operator=( IFlagStore&& ) noexcept = default;

        virtual int32 getFlag( string_view key, int32 defaultValue = 0 ) const = 0;
        virtual void  setFlag( string_view key, int32 value )                  = 0;
    };

    // ------------------------------------------------------------------------------
    // 2) SaveGameSerializer — 임의의 REFLECT() 객체 바이너리 세이브/로드 유틸리티
    // ------------------------------------------------------------------------------
    /** @brief 임의의 리플렉션 객체(구조체/클래스)를 슬롯 파일에 바이너리로 직렬화/역직렬화하는 유틸리티 */
    struct SW_GF_API SaveGameSerializer
    {
        static constexpr uint32 kSaveBinMagic   = 0x53415631u; // 'SAV1'
        static constexpr uint32 kSaveBinVersion = 1;

        /** @brief 임의의 리플렉션 객체를 SAV1 바이너리 파일로 저장합니다. */
        template <typename T>
        static bool saveGameToSlot( const T& saveObject, string_view path )
        {
            Archive payloadArch;
            if ( payloadArch.serializeObject( saveObject ) == false )
                return false;

            const uint32 crc = payloadArch.calculateChecksum();

            Archive fileArch;
            fileArch << kSaveBinMagic;
            fileArch << kSaveBinVersion;
            fileArch << crc;
            fileArch << static_cast<uint32>( payloadArch.getSize() );
            fileArch.writeBytes( payloadArch.getData(), payloadArch.getSize() );

            FileUtil::createParentDirectory( path );
            return fileArch.saveFile( path );
        }

        /** @brief SAV1 바이너리 파일로부터 임의의 리플렉션 객체를 복원합니다. */
        template <typename T>
        static bool loadGameFromSlot( T& outSaveObject, string_view path )
        {
            Archive fileArch( path, true );
            if ( fileArch.getSize() < 16 )
                return false;

            uint32 magic{ 0 };
            fileArch >> magic;
            if ( magic != kSaveBinMagic )
                return false;

            uint32 version{ 0 };
            fileArch >> version;
            if ( version > kSaveBinVersion )
                return false;

            uint32 expectedCrc{ 0 };
            fileArch >> expectedCrc;

            uint32 payloadSize{ 0 };
            fileArch >> payloadSize;

            if ( fileArch.getOffset() + payloadSize > fileArch.getSize() )
                return false;

            const uint8* pPayload    = fileArch.getData() + fileArch.getOffset();
            const uint32 computedCrc = StringUtil::computeCrc32( pPayload, payloadSize );
            if ( expectedCrc != computedCrc )
                return false;

            Archive payloadArch( pPayload, payloadSize );
            return payloadArch.deserializeObject( outSaveObject );
        }
    };

    // ------------------------------------------------------------------------------
    // 3) SaveGame — 모든 세이브 데이터의 순수 리플렉션 베이스 클래스
    // ------------------------------------------------------------------------------
    /** @brief 장르별/게임별 커스텀 세이브 클래스/구조체의 베이스 */
    REFLECT()
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

        /** @brief 리플렉션 바이너리 포맷으로 파일에 저장합니다. */
        virtual bool saveToFile( string_view path ) const;
        /** @brief 바이너리 파일에서 리플렉션 역직렬화로 불러옵니다. */
        virtual bool loadFromFile( string_view path );
    };
} // namespace sw
