/**
 * @file XmlSerializer.h
 * @brief XML 백엔드 인터페이스와 TypeInfo 기반 XML 직렬화 · 역직렬화입니다.
 * @note 리플렉션이 아닌 콘텐츠(테이블, 타일맵, 툴)는 Utility/Xml/XmlDocument 를 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
    struct TypeInfo;

    class Archive;
    /** @brief `XmlDocumentBackend::getDeserializationRoot` 가 반환하는 노드입니다. */
    class XmlNode;

    using XmlArrayItemDelegate = Delegate<void( string_view itemStr )>;
    using XmlMapItemDelegate   = Delegate<void( string_view keyStr, string_view valStr )>;
    /** @brief 자식 요소를 방문하는 콜백입니다. 불리는 동안 그 자식이 백엔드의 현재 노드가 됩니다. */
    using XmlChildVisitDelegate = Delegate<void( string_view tagName )>;

    /**
     * @class IXmlBackend
     * @brief XML 직렬화 · 역직렬화 백엔드 인터페이스입니다.
     */
    class SW_API IXmlBackend
    {
    public:
        /** @brief 가상 소멸자입니다. */
        virtual ~IXmlBackend()                           = default;
        IXmlBackend( const IXmlBackend& )                = default;
        IXmlBackend& operator=( const IXmlBackend& )     = default;
        IXmlBackend( IXmlBackend&& ) noexcept            = default;
        IXmlBackend& operator=( IXmlBackend&& ) noexcept = default;

        // ------------------------------------------------------------------------------
        // 1) 쓰기: 루트, 값/속성, 배열, 맵
        // ------------------------------------------------------------------------------
        /** @brief XML 직렬화를 시작합니다. */
        virtual void initializeXmlSerialization( const utf8* pRootTagName ) = 0;
        /** @brief 값을 XML 자식 요소로 씁니다. */
        virtual void writeValue( const utf8* pTagName, const utf8* pValueString ) = 0;
        /** @brief 값을 현재 부모 요소의 XML 속성(attribute)으로 씁니다. */
        virtual void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) = 0;
        /** @brief 배열 구간을 시작합니다. */
        virtual void beginArray( const utf8* pTagName ) = 0;
        /** @brief 배열 항목을 씁니다. */
        virtual void writeArrayItem( const utf8* pValueString ) = 0;
        /** @brief 배열 구간을 끝냅니다. */
        virtual void endArray() = 0;
        /** @brief 맵 구간을 시작합니다. */
        virtual void beginMap( const utf8* pTagName ) = 0;
        /** @brief 맵 항목을 시작합니다. */
        virtual void beginMapEntry() = 0;
        /** @brief 맵 키를 씁니다. */
        virtual void writeMapKey( const utf8* pKeyString ) = 0;
        /** @brief 맵 값을 씁니다. */
        virtual void writeMapValue( const utf8* pValueString ) = 0;
        /** @brief 맵 항목을 끝냅니다. */
        virtual void endMapEntry() = 0;
        /** @brief 맵 구간을 끝냅니다. */
        virtual void endMap() = 0;
        /** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
        virtual string endSerialize() = 0;

        // ------------------------------------------------------------------------------
        // 2) 읽기: 루트, 값/속성, 배열/맵 순회
        // ------------------------------------------------------------------------------
        /**
         * @brief XML 역직렬화를 시작합니다.
         * @note XML 을 **`string_view` 로** 받습니다. 예전에는 `const utf8*` 였는데, 부르는 쪽이
         *       `string_view::data()` 를 넘기면서 길이가 사라졌습니다. 뷰가 더 큰 버퍼의 일부면
         *       널 종단이 없어 파서가 끝을 넘어 읽습니다. `XmlDocument::parse` 는 원래부터
         *       `string_view` 를 받아 `load_buffer(data, size)` 로 안전하게 읽으므로,
         *       이 중간 계층이 길이를 버리던 것이 유일한 구멍이었습니다.
         */
        virtual bool initializeXmlDeserialization( string_view xmlStr, const utf8* pRootTagName ) = 0;
        /** @brief XML 자식 요소에서 값을 읽습니다. */
        virtual bool readValue( const utf8* pTagName, string& outValue ) = 0;
        /** @brief 현재 부모 요소의 XML 속성을 읽습니다. */
        virtual bool readAttribute( const utf8* pAttrName, string& outValue ) = 0;
        /** @brief 속성을 먼저 보고, 없으면 자식 요소에서 읽습니다(호환 로드). */
        virtual bool readValueOrAttribute( const utf8* pName, string& outValue )
        {
            if ( readAttribute( pName, outValue ) )
                return true;
            return readValue( pName, outValue );
        }

        /** @brief 배열 요소를 순회합니다. pTagName 이 비면 현재 노드에서 `<item>` 을 순회합니다. */
        virtual bool iterateArray( const utf8* pTagName, const XmlArrayItemDelegate& callback ) = 0;
        /** @brief 맵 항목을 순회합니다. pTagName 이 비면 현재 노드의 자식을 순회합니다. */
        virtual bool iterateMap( const utf8* pTagName, const XmlMapItemDelegate& callback ) = 0;
        /** @brief 현재 부모의 자식 요소로 내려갑니다. 없으면 false 입니다. */
        virtual bool pushChild( const utf8* pTagName )
        {
            (void)pTagName;
            return false;
        }
        /** @brief 첫 자식 요소로 내려갑니다. 이름을 모를 때 씁니다. 없으면 false 입니다. */
        virtual bool pushFirstChild() { return false; }
        /** @brief pushChild 로 내려가기 전의 부모로 돌아갑니다. */
        virtual void popChild() {}

        // ------------------------------------------------------------------------------
        // 2-1) 노드 단위 접근: 임의 중첩 컨테이너용
        //      태그 이름에 의존하지 않고 자식을 순서대로 훑으려면 필요하다.
        // ------------------------------------------------------------------------------
        /** @brief 현재 노드의 텍스트를 설정합니다. */
        virtual void writeText( const utf8* pText ) { (void)pText; }
        /** @brief 현재 노드의 텍스트를 읽습니다. */
        virtual bool readText( string& outText )
        {
            (void)outText;
            return false;
        }
        /** @brief 현재 노드의 자식 요소를 이름과 상관없이 순서대로 방문합니다. */
        virtual bool iterateChildren( const XmlChildVisitDelegate& callback )
        {
            (void)callback;
            return false;
        }

        // ------------------------------------------------------------------------------
        // 3) 키 정책: 태그/속성 이름에만 적용하고 값에는 영향이 없다
        // ------------------------------------------------------------------------------
        /** @brief 태그/속성 이름을 찾을 때 대소문자를 무시하는지 반환합니다(기본 true). */
        bool ignoresCaseKeys() const { return _bIgnoreCaseKeys == SW_TRUE; }
        /** @brief 태그/속성 이름을 찾을 때 대소문자를 무시할지 설정합니다. */
        void setIgnoreCaseKeys( bool bIgnoreCaseKeys ) { _bIgnoreCaseKeys = bIgnoreCaseKeys ? SW_TRUE : SW_FALSE; }

    protected:
        /** @brief 키 대소문자를 무시하는 기본 상태로 만듭니다. */
        IXmlBackend() noexcept
            : _bIgnoreCaseKeys{ SW_TRUE }
            , _reservedIgnoreCase{ 0 } {}

        uint8                  _bIgnoreCaseKeys    : 1;
        [[maybe_unused]] uint8 _reservedIgnoreCase : 7;
    };

    /**
     * @class XmlDocumentBackend
     * @brief XmlDocument 를 쓰는 XML 백엔드입니다.
     */
    class SW_API XmlDocumentBackend : public IXmlBackend
    {
    public:
        /** @brief 빈 XmlDocument 백엔드를 만듭니다. */
        XmlDocumentBackend();
        /** @brief 구현을 정리합니다. */
        virtual ~XmlDocumentBackend() override;

        /** @brief XML 직렬화를 시작합니다. */
        void initializeXmlSerialization( const utf8* pRootTagName ) override;
        /** @brief 값을 XML 자식 요소로 씁니다. */
        void writeValue( const utf8* pTagName, const utf8* pValueString ) override;
        /** @brief 값을 현재 부모 요소의 XML 속성으로 씁니다. */
        void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) override;
        /** @brief 배열 구간을 시작합니다. */
        void beginArray( const utf8* pTagName ) override;
        /** @brief 배열 항목을 씁니다. */
        void writeArrayItem( const utf8* pValueString ) override;
        /** @brief 배열 구간을 끝냅니다. */
        void endArray() override;
        /** @brief 맵 구간을 시작합니다. */
        void beginMap( const utf8* pTagName ) override;
        /** @brief 맵 항목을 시작합니다. */
        void beginMapEntry() override;
        /** @brief 맵 키를 씁니다. */
        void writeMapKey( const utf8* pKeyString ) override;
        /** @brief 맵 값을 씁니다. */
        void writeMapValue( const utf8* pValueString ) override;
        /** @brief 맵 항목을 끝냅니다. */
        void endMapEntry() override;
        /** @brief 맵 구간을 끝냅니다. */
        void endMap() override;
        /** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
        string endSerialize() override;

        /** @brief XML 역직렬화를 시작합니다. */
        bool initializeXmlDeserialization( string_view xmlStr, const utf8* pRootTagName ) override;
        /** @brief XML 자식 요소에서 값을 읽습니다. */
        bool readValue( const utf8* pTagName, string& outValue ) override;
        /** @brief 현재 부모 요소의 XML 속성을 읽습니다. */
        bool readAttribute( const utf8* pAttrName, string& outValue ) override;
        /** @brief 배열 요소를 순회합니다. */
        bool iterateArray( const utf8* pTagName, const XmlArrayItemDelegate& callback ) override;
        /** @brief 맵 항목을 순회합니다. */
        bool iterateMap( const utf8* pTagName, const XmlMapItemDelegate& callback ) override;
        /** @brief 현재 부모의 자식 요소로 내려갑니다. 없으면 false 입니다. */
        bool pushChild( const utf8* pTagName ) override;
        /** @brief 첫 자식 요소로 내려갑니다. */
        bool pushFirstChild() override;
        /** @brief pushChild 로 내려가기 전의 부모로 돌아갑니다. */
        void popChild() override;
        /** @brief 현재 노드의 텍스트를 설정합니다. */
        void writeText( const utf8* pText ) override;
        /** @brief 현재 노드의 텍스트를 읽습니다. */
        bool readText( string& outText ) override;
        /** @brief 현재 노드의 자식 요소를 순서대로 방문합니다. */
        bool iterateChildren( const XmlChildVisitDelegate& callback ) override;

        /**
         * @brief 역직렬화 중인 문서의 **루트 노드**입니다. 초기화 전이면 무효 노드입니다.
         * @details 이것이 없어서 `XmlSerializer::deserializeSoft` 가 **같은 문자열을 두 번
         *          파싱했습니다.** 한 번은 자기 `XmlDocument` 로 버전 속성과 orphan 자식을
         *          훑으려고, 또 한 번은 이 백엔드가 값을 읽으려고. 형제인
         *          `JsonSerializer::deserializeSoft` 는 처음부터 문서 하나로 셋을 다 합니다.
         * @warning 반환된 노드는 **이 백엔드가 살아 있는 동안만** 유효합니다(문서를 이쪽이 쥡니다).
         */
        XmlNode getDeserializationRoot() const;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };

    /**
     * @class XmlSerializer
     * @brief TypeInfo 리플렉션으로 XML 을 쓰고 읽습니다.
     */
    class SW_API XmlSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 4) 백엔드 지정 / 기본 XmlDocumentBackend
        // ------------------------------------------------------------------------------
        /** @brief 지정한 백엔드로 객체를 XML 로 직렬화합니다. */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 IXmlBackend&            backend,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief 지정한 백엔드로 XML 에서 객체를 역직렬화합니다. */
        static bool deserialize( void* pInstance, const TypeInfo& typeInfo,
                                 IXmlBackend& backend, string_view xmlStr,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 기본 XmlDocumentBackend 로 객체를 XML 로 직렬화합니다. */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief 기본 XmlDocumentBackend 로 XML 에서 객체를 역직렬화합니다. */
        static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 객체를 XML 로 직렬화해 Archive 에 기록합니다. */
        static bool serializeToArchive( const void* pInstance, const TypeInfo& typeInfo, Archive& outArchive,
                                        const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief Archive 에서 XML 문자열을 읽어 객체로 역직렬화합니다. */
        static bool deserializeFromArchive( void* pInstance, const TypeInfo& typeInfo, Archive& inArchive,
                                            const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief XML 을 절대 경로에 씁니다. */
        static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo,
                              const SerializeContext& ctx = SerializeContext::getDefault() );
        /** @brief 절대 · 리소스 경로에서 XML 을 읽어 역직렬화합니다. */
        static bool loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo,
                              const SerializeContext& ctx = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 5) Soft · 버전: orphan 수집, 루트 _schemaVersion
        // ------------------------------------------------------------------------------
        /** @brief Soft 역직렬화입니다. 변환하지 못한 필드를 orphan 으로 모읍니다. */
        static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                     vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                     const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 루트 속성 `_schemaVersion` 을 붙여 XML 로 직렬화합니다. */
        static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                          const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 이미 열린 부모 요소에 `_schemaVersion` 과 PROPERTY 를 씁니다. 스칼라는 속성으로 씁니다. */
        static void serializeVersionedInto( IXmlBackend& backend, uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 버전을 읽고 soft 역직렬화한 뒤, 필요하면 migrate 를 부릅니다. */
        static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                          uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
                                          const TypeInfo*         pLegacyTypeInfo = nullptr,
                                          const SerializeContext& ctx             = SerializeContext::getDefault() );
    };

} // namespace sw
