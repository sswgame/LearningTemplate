/**
 * @file ComponentDefaults.h
 * @brief 게임 gamedata.xml `<Defaults>`를 Component PROPERTY에 주입합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
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
        string_view getPath() const;

        /** @brief 캐시된 기본값 XML 문서를 다시 로드합니다. */
        void reload();

        // ----------------------------------------------------------------------
        // Static Facade (EngineServices 바인딩을 통해 위임)
        // ----------------------------------------------------------------------
        static void        applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo = nullptr );
        static void        applyDefaults( Component* pComp, const TypeInfo& typeInfo );
        static void        setDefaultsPath( string_view path );
        static string_view getDefaultsPath();
        static void        reloadDefaults();

    private:
        void ensureDefaultsLoaded();

        XmlDocument   _defaultsDoc;
        string        _customDefaultsPath;
        mutable mutex _defaultsMutex;
        bool          _bDefaultsLoaded;
    };
} // namespace sw
