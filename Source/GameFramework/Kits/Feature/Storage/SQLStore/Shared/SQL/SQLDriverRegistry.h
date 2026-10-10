/**
 * @file SQLDriverRegistry.h
 * @brief 드라이버 이름 → 드라이버. 이 실행 파일에 든 드라이버만 있습니다 — 없는 이름은 분명한 까닭과 함께 nullptr(다른 드라이버로 바꾸지 않는다).
 * @details SQLite 는 이 키트(GF_SQLStore)가 처음 찾을 때 스스로 올리고, PostgreSQL 은 서버 키트(GF_Server_SQLStore)의 `ServiceStoreFactory` 가 처음 쓸 때 올린다.
 *          드라이버 객체는 각 모듈의 함수 안 static 이라, 모듈을 내리기 전에 `unregisterDriver` 한다(`ServiceStoreFactory::shutdown`).
 *          어느 드라이버가 실행 파일에 드는지는 빌드 타깃이 정한다(Client: SQLite 만, Server · Game: 둘 다).
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
     * @brief 드라이버 등록부입니다. 스레드 안전입니다.
     */
    struct SW_GF_API SQLDriverRegistry
    {
        /** @brief 드라이버를 올립니다. 같은 이름이 이미 있으면 오류 로그 + 무시합니다(같은 객체면 조용히 무시). */
        static void registerDriver( ISQLDriver* pDriver );
        static void unregisterDriver( ISQLDriver* pDriver );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 과 @p outError 에 이 빌드 타깃 · 있는 드라이버 목록. */
        static ISQLDriver* findDriver( string_view driverName, string& outError );
        static void        getDriverNames( vector<string>& outListName );
    };
} // namespace sw
