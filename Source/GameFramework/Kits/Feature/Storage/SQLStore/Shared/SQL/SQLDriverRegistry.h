/**
 * @file SQLDriverRegistry.h
 * @brief 드라이버 이름으로 드라이버를 찾는 레지스트리입니다. 이 실행 파일에 포함된 드라이버만 있으며, 없는 이름은 분명한 이유와 함께 nullptr 을 반환합니다. 다른 드라이버로 대신하지 않습니다.
 * @details SQLite 는 이 키트(GF_SQLStore)가 처음 찾을 때 스스로 등록하고, PostgreSQL 은 서버 키트(GF_Server_SQLStore)의 `ServiceStoreFactory` 가 처음 쓸 때 등록한다.
 *          드라이버 객체는 각 모듈의 함수 안 static 이므로 모듈을 언로드하기 전에 `unregisterDriver` 로 해제해야 한다(`ServiceStoreFactory::shutdown`).
 *          어느 드라이버가 실행 파일에 포함되는지는 빌드 타깃이 정한다. Client 는 SQLite 만, Server 와 Game 은 둘 다 포함한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ISQLDriver;

    /**
     * @struct SQLDriverRegistry
     * @brief 드라이버 레지스트리입니다. 스레드 안전합니다.
     */
    struct SW_KIT_API SQLDriverRegistry
    {
        /** @brief 드라이버를 등록합니다. 같은 이름이 이미 있으면 오류 로그를 남기고 무시하며, 같은 객체이면 조용히 무시합니다. */
        static void registerDriver( ISQLDriver* pDriver );
        static void unregisterDriver( ISQLDriver* pDriver );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 을 반환하고 @p outError 에 이 빌드 타깃과 있는 드라이버 목록을 채웁니다. */
        static ISQLDriver* findDriver( string_view driverName, string& outError );
        static void        getDriverNames( vector<string>& outListName );
    };
} // namespace sw
