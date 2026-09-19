/**
 * @file ComponentDefaults.h
 * @brief 게임 gamedata.xml `<Defaults>`를 Component PROPERTY에 주입합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    struct TypeInfo;

    class Component;

    /**
     * @class ComponentDefaults
     * @brief 게임 부트스트랩이 지정한 gamedata.xml의 `<Defaults>`를 리플렉션으로 주입하는 서비스 매니저입니다.
     */
    class SW_API ComponentDefaults
    {
    public:
        ComponentDefaults();
        ~ComponentDefaults();

        ComponentDefaults( const ComponentDefaults& )            = delete;
        ComponentDefaults& operator=( const ComponentDefaults& ) = delete;

        /** @brief 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
        void apply( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
        /** @brief 컴포넌트 인스턴스에 XML 기본값을 리플렉션으로 주입합니다. */
        void apply( Component* pComp, const TypeInfo& typeInfo );

        /** @brief 게임 gamedata.xml 리소스 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
        void setPath( string_view path );

        /** @brief 현재 게임 gamedata.xml 리소스 경로를 반환합니다. */
        string getPath() const;

        /** @brief 캐시된 기본값 XML 문서를 다시 로드합니다. */
        void reload();

        // ----------------------------------------------------------------------
        // Static Facade (EngineServices 바인딩을 통해 위임)
        // ----------------------------------------------------------------------
        static void   applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
        static void   applyDefaults( Component* pComp, const TypeInfo& typeInfo );
        static void   setDefaultsPath( string_view path );
        static string getDefaultsPath();
        static void   reloadDefaults();

    private:
        /** @brief 한 단계(타입 하나)의 `<Defaults>` 노드를 그 타입의 프로퍼티에 주입합니다. */
        static void applyNodeToProperties( void* pInstance, const TypeInfo& typeInfo, const XmlNode& compNode );

        void ensureDefaultsLoaded();

        XmlDocument _defaultsDoc;
        string      _customDefaultsPath;

        mutable mutex _defaultsMutex;
        /**
         * @brief 기본값 문서가 올라와 있는지. **원자적이어야 합니다.**
         * @details `ensureDefaultsLoaded` 는 락을 잡기 전에 이 값을 먼저 본다(이중 검사). 평범한
         *          `bool` 이면 그 읽기가 데이터 레이스이고, 더 나쁜 것은 **순서가 보장되지 않는
         *          다는 점**이다 — 한 스레드가 `true` 를 본 시점에 `_defaultsDoc` 은 아직 다 지어지지
         *          않았을 수 있다. release 로 쓰고 acquire 로 읽어 문서가 먼저 보이게 묶는다.
         */
        atomic<bool> _bDefaultsLoaded;
    };
} // namespace sw
