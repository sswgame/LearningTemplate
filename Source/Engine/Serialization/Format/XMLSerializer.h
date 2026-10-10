/**
 * @file XMLSerializer.h
 * @brief XML 백엔드 인터페이스와 TypeInfo 기반 XML 직렬화 · 역직렬화입니다.
 * @note 리플렉션이 아닌 콘텐츠(테이블, 타일맵, 툴)는 Serialization/XML/XMLDocument 를 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializeContext.h"

namespace sw
{
    struct TypeInfo;

    /** @brief `XMLDocumentBackend::getDeserializationRoot` · `IXMLBackend::getCurrentNode` 가 반환하는 노드입니다. */
    class XMLNode;

    /** @brief 자식 요소를 방문하는 콜백입니다. 불리는 동안 그 자식이 백엔드의 현재 노드가 됩니다. */
    using XMLChildVisitDelegate = Delegate<void( string_view tagName )>;

    /**
     * @class IXMLBackend
     * @brief XML 직렬화 · 역직렬화 백엔드 인터페이스입니다.
     */
    class SW_API IXMLBackend
    {
    public:
        /** @brief 가상 소멸자입니다. */
        virtual ~IXMLBackend()                           = default;
        IXMLBackend( const IXMLBackend& )                = default;
        IXMLBackend& operator=( const IXMLBackend& )     = default;
        IXMLBackend( IXMLBackend&& ) noexcept            = default;
        IXMLBackend& operator=( IXMLBackend&& ) noexcept = default;

        // ------------------------------------------------------------------------------
        // 1) 쓰기: 루트, 값/속성, 자식 요소
        // ------------------------------------------------------------------------------
        /** @brief XML 직렬화를 시작합니다. */
        virtual void initializeXMLSerialization( const utf8* pRootTagName ) = 0;
        /** @brief 값을 XML 자식 요소로 씁니다. */
        virtual void writeValue( const utf8* pTagName, const utf8* pValueString ) = 0;
        /** @brief 값을 현재 부모 요소의 XML 속성(attribute)으로 씁니다. */
        virtual void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) = 0;
        /** @brief 자식 요소를 열고 그 안으로 들어갑니다. 이후 쓰기는 그 요소에 붙습니다. */
        virtual void beginMap( const utf8* pTagName ) = 0;
        /** @brief beginMap 으로 연 요소를 닫고 부모로 돌아갑니다. */
        virtual void endMap() = 0;
        /** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
        virtual string endSerialize() = 0;

        // ------------------------------------------------------------------------------
        // 2) 읽기: 루트, 값/속성, 자식 요소
        // ------------------------------------------------------------------------------
        /**
         * @brief XML 역직렬화를 시작합니다.
         * @note XML 을 **`string_view` 로** 받습니다. `const utf8*` 로 받으면 부르는 쪽이 `string_view::data()` 를 넘기며
         *       길이가 사라지고, 뷰가 더 큰 버퍼의 일부면 널 종단이 없어 파서가 끝을 넘어 읽습니다. `XMLDocument::parse` 도
         *       `string_view` 를 받아 `load_buffer(data, size)` 로 읽습니다.
         */
        virtual bool initializeXMLDeserialization( string_view xmlStr, const utf8* pRootTagName ) = 0;
        /** @brief XML 자식 요소에서 값을 읽습니다. */
        [[nodiscard]] virtual bool readValue( const utf8* pTagName, string& outValue ) = 0;
        /** @brief 현재 부모 요소의 XML 속성을 읽습니다. */
        [[nodiscard]] virtual bool readAttribute( const utf8* pAttrName, string& outValue ) = 0;
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
        [[nodiscard]] virtual bool readText( string& outText )
        {
            (void)outText;
            return false;
        }
        /** @brief 현재 노드(그 서브트리)를 XML 원문으로 씁니다. 지원하지 않으면 false 입니다(모르는 원소를 맡길 때). */
        [[nodiscard]] virtual bool readCurrentNodeXML( string& outXML )
        {
            (void)outXML;
            return false;
        }
        /** @brief XML 원문 원소 하나를 현재 부모의 자식으로 그대로 붙입니다(맡아 둔 원소를 다시 쓸 때). */
        virtual void writeRawElement( string_view xml ) { (void)xml; }
        /** @brief 현재 노드의 자식 요소를 이름과 상관없이 순서대로 방문합니다. */
        virtual bool iterateChildren( const XMLChildVisitDelegate& callback )
        {
            (void)callback;
            return false;
        }
        /**
         * @brief 읽는 중인 현재 노드입니다. 노드를 내줄 수 없는 백엔드는 무효 노드를 돌려줍니다.
         * @details 안쪽 원소의 모르는 속성 · 자식을 orphan 으로 남길 때 씁니다. 무효 노드면 그 검사를 건너뜁니다.
         */
        [[nodiscard]] virtual XMLNode getCurrentNode() const;

        // ------------------------------------------------------------------------------
        // 3) 키 정책: 태그/속성 이름에만 적용하고 값에는 영향이 없다
        // ------------------------------------------------------------------------------
        /** @brief 태그/속성 이름을 찾을 때 대소문자를 무시하는지 반환합니다(기본 true). */
        bool ignoresCaseKeys() const { return _bIgnoreCaseKeys == SW_TRUE; }
        /** @brief 태그/속성 이름을 찾을 때 대소문자를 무시할지 설정합니다. */
        void setIgnoreCaseKeys( bool bIgnoreCaseKeys ) { _bIgnoreCaseKeys = bIgnoreCaseKeys ? SW_TRUE : SW_FALSE; }

    protected:
        /** @brief 키 대소문자를 무시하는 기본 상태로 만듭니다. */
        IXMLBackend() noexcept
            : _bIgnoreCaseKeys{ SW_TRUE }
            , _reservedIgnoreCase{ 0 } {}

        uint8                  _bIgnoreCaseKeys    : 1;
        [[maybe_unused]] uint8 _reservedIgnoreCase : 7;
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLDocumentBackend
     * @brief XMLDocument 를 쓰는 XML 백엔드입니다.
     */
    class SW_API XMLDocumentBackend : public IXMLBackend
    {
    public:
        /** @brief 빈 XMLDocument 백엔드를 만듭니다. */
        XMLDocumentBackend();
        /** @brief 구현을 정리합니다. */
        virtual ~XMLDocumentBackend() override;

        /** @brief XML 직렬화를 시작합니다. */
        void initializeXMLSerialization( const utf8* pRootTagName ) override;
        /** @brief 값을 XML 자식 요소로 씁니다. */
        void writeValue( const utf8* pTagName, const utf8* pValueString ) override;
        /** @brief 값을 현재 부모 요소의 XML 속성으로 씁니다. */
        void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) override;
        /** @brief 자식 요소를 열고 그 안으로 들어갑니다. */
        void beginMap( const utf8* pTagName ) override;
        /** @brief beginMap 으로 연 요소를 닫고 부모로 돌아갑니다. */
        void endMap() override;
        /** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
        string endSerialize() override;

        /** @brief XML 역직렬화를 시작합니다. */
        bool initializeXMLDeserialization( string_view xmlStr, const utf8* pRootTagName ) override;
        /** @brief XML 자식 요소에서 값을 읽습니다. */
        [[nodiscard]] bool readValue( const utf8* pTagName, string& outValue ) override;
        /** @brief 현재 부모 요소의 XML 속성을 읽습니다. */
        [[nodiscard]] bool readAttribute( const utf8* pAttrName, string& outValue ) override;
        /** @brief 현재 부모의 자식 요소로 내려갑니다. 없으면 false 입니다. */
        bool pushChild( const utf8* pTagName ) override;
        /** @brief 첫 자식 요소로 내려갑니다. */
        bool pushFirstChild() override;
        /** @brief pushChild 로 내려가기 전의 부모로 돌아갑니다. */
        void popChild() override;
        /** @brief 현재 노드의 텍스트를 설정합니다. */
        void writeText( const utf8* pText ) override;
        /** @brief 현재 노드(그 서브트리)를 XML 원문으로 씁니다. */
        [[nodiscard]] bool readCurrentNodeXML( string& outXML ) override;
        /** @brief XML 원문 원소 하나를 현재 부모의 자식으로 붙입니다. 원문을 읽지 못하면 아무것도 붙이지 않습니다. */
        void writeRawElement( string_view xml ) override;
        /** @brief 현재 노드의 텍스트를 읽습니다. */
        [[nodiscard]] bool readText( string& outText ) override;
        /** @brief 현재 노드의 자식 요소를 순서대로 방문합니다. */
        bool iterateChildren( const XMLChildVisitDelegate& callback ) override;
        /** @brief 읽는 중인 현재 노드입니다. 이 백엔드가 살아 있는 동안만 유효합니다. */
        [[nodiscard]] XMLNode getCurrentNode() const override;

        /**
         * @brief 역직렬화 중인 문서의 **루트 노드**입니다. 초기화 전이면 무효 노드입니다.
         * @details `XMLSerializer::deserializeSoft` 가 이것으로 버전 속성과 orphan 자식을 훑습니다 — 같은 문자열을 두 번
         *          파싱하지 않게(형제 `JSONSerializer::deserializeSoft` 도 문서 하나로 셋을 다 합니다).
         * @warning 반환된 노드는 **이 백엔드가 살아 있는 동안만** 유효합니다(문서를 이쪽이 쥡니다).
         */
        XMLNode getDeserializationRoot() const;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLSerializer
     * @brief TypeInfo 리플렉션으로 XML 을 쓰고 읽습니다.
     */
    class SW_API XMLSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 4) 백엔드 지정 / 기본 XMLDocumentBackend
        // ------------------------------------------------------------------------------
        /** @brief 지정한 백엔드로 객체를 XML 로 직렬화합니다. */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 IXMLBackend&            backend,
                                 const SerializeContext& context = SerializeContext::getDefault() );
        /** @brief 지정한 백엔드로 XML 에서 객체를 역직렬화합니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo,
                                               IXMLBackend& backend, string_view xmlStr,
                                               const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 기본 XMLDocumentBackend 로 객체를 XML 로 직렬화합니다. */
        static string serialize( const void* pInstance, const TypeInfo& typeInfo,
                                 const SerializeContext& context = SerializeContext::getDefault() );
        /** @brief 기본 XMLDocumentBackend 로 XML 에서 객체를 역직렬화합니다. */
        [[nodiscard]] static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                               const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief XML 을 절대 경로에 씁니다. */
        [[nodiscard]] static bool saveFile( string_view absPath, const void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault() );
        /** @brief 절대 · 리소스 경로에서 XML 을 읽어 역직렬화합니다. */
        [[nodiscard]] static bool loadFile( string_view path, void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault() );

        // ------------------------------------------------------------------------------
        // 5) Soft · 버전: orphan 수집, 루트 _schemaVersion
        // ------------------------------------------------------------------------------
        /** @brief Soft 역직렬화입니다. 변환하지 못한 필드를 orphan 으로 모읍니다. */
        [[nodiscard]] static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                                   vector<SchemaOrphanValue>* pOutListOrphan = nullptr, uint32* pOutVersion = nullptr,
                                                   const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 루트 속성 `_schemaVersion` 을 붙여 XML 로 직렬화합니다. */
        static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                          const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 이미 열린 부모 요소에 `_schemaVersion` 과 PROPERTY 를 씁니다. 스칼라는 속성으로 씁니다. */
        static void serializeVersionedInto( IXMLBackend& backend, uint32 version, const void* pInstance, const TypeInfo& typeInfo,
                                            const SerializeContext& context = SerializeContext::getDefault() );

        /** @brief 버전을 읽고 soft 역직렬화한 뒤, 필요하면 migrate 를 부릅니다. */
        [[nodiscard]] static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
                                                        uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
                                                        const TypeInfo*         pLegacyTypeInfo = nullptr,
                                                        const SerializeContext& context         = SerializeContext::getDefault() );
    };

} // namespace sw
