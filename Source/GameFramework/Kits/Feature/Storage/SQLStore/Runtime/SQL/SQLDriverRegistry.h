/**
 * @file SQLDriverRegistry.h
 * @brief 드라이버 이름으로 드라이버를 찾는 레지스트리입니다. 이 실행 파일에 포함된 드라이버만 있으며, 없는 이름은 분명한 이유와 함께 nullptr 을 반환합니다. 다른 드라이버로 대신하지 않습니다.
 * @details 드라이버는 등록을 손으로 부르지 않고 그 드라이버 `.cpp` 의 정적 `SQLDriverRegistration` 하나로 오른다. 모듈이 로드되면 오르고
 *          (SQLite 는 이 키트 GF_SQLStore, PostgreSQL 은 서버 키트 GF_Server_SQLStore), 이미지를 내릴 때 그 정적 등록 객체(구독)가
 *          그 이미지의 드라이버를 지운다. 핫 리로드가 옛 이미지의 언로드를 미뤄도 옛 드라이버가 남거나 새 등록을 지우지 않는다.
 *          어느 드라이버가 실행 파일에 포함되는지는 빌드 타깃이 정한다. Client 는 SQLite 만, Server 와 Game 은 둘 다 포함한다.
 *          모듈 서브시스템으로 옮기지 않는다 — 표는 실행 상태가 아니라 "이 이미지들에 컴파일된 상태 없는 드라이버" 의 링크 목록이라(`SW_DEV_COMMAND` · `SW_BUILT_IN_ASSET_CACHE` 등록과 같은 단위,
 *          Godot `ClassDB` 의 모듈 등록 자리) 모듈 범위가 서기 전 · 없는 호스트(스키마 테스트의 데이터 종류 검사 · 서버 조립)에서도 같은 답을 내야 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ISQLDriver;
    class SQLDriverRegistration;

    /**
     * @struct SQLDriverRegistry
     * @brief 드라이버 레지스트리입니다. 스레드 안전합니다.
     */
    struct SW_KIT_API SQLDriverRegistry
    {
        friend class SQLDriverRegistration;

        /** @brief 이름으로 찾습니다. 없으면 nullptr 을 반환하고 @p outError 에 이 빌드 타깃과 있는 드라이버 목록을 채웁니다. */
        static ISQLDriver* findDriver( string_view driverName, string& outError );
        static void        getDriverNames( vector<string>& outListName );

    private:
        /** @brief 드라이버를 올립니다. 같은 이름이 이미 있으면 오류다(앞 것을 덮지 않는다). */
        static void registerDriver( ISQLDriver& driver );
        /** @brief @p driver 를 내립니다. 언로드 리스너가 이미 지웠으면 할 일이 없다. */
        static void unregisterDriver( ISQLDriver& driver );
    };
} // namespace sw

namespace sw
{
    /**
     * @class SQLDriverRegistration
     * @brief 드라이버 하나를 모듈 수명 동안 레지스트리에 올리는 정적 객체입니다. 드라이버 `.cpp` 의 `namespace sw` 에 하나 둡니다.
     * @details 드라이버 객체는 등록 객체보다 먼저 만들어져 나중에 사라져야 한다 — 함수 안 정적 객체(`getInstance`)를 생성자 인자로 넘기면 그렇다.
     */
    class SQLDriverRegistration
    {
    public:
        explicit SQLDriverRegistration( ISQLDriver& driver )
            : _driver{ driver }
        {
            SQLDriverRegistry::registerDriver( _driver );
        }
        ~SQLDriverRegistration() { SQLDriverRegistry::unregisterDriver( _driver ); }

        SQLDriverRegistration( const SQLDriverRegistration& )            = delete;
        SQLDriverRegistration& operator=( const SQLDriverRegistration& ) = delete;

    private:
        ISQLDriver& _driver;
    };
} // namespace sw
