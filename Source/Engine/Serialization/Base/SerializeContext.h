/**
 * @file SerializeContext.h
 * @brief 타입별 커스텀 바이너리/텍스트 직렬화 핸들러와 직렬화 설정을 담는 SerializeContext 입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Base/SerializePurpose.h"

namespace sw
{
    struct TypeInfo;

    class TypeRegistry;

    /**
     * @class SerializeContext
     * @brief 직렬화 한 번에 쓰는 설정 묶음입니다. 저장 목적(`SerializePurpose`, 필수), 타입별 커스텀 바이너리/텍스트 핸들러, 키 정책,
     *        소유 포인터 팩토리, 모르는 원소를 맡는 함수를 듭니다.
     */
    class SW_API SerializeContext
    {
    public:
        using BinaryWriteFn = Delegate<void( const void* pValPtr, vector<uint8>& outListBuffer )>;
        using BinaryReadFn  = Delegate<bool( void* pValPtr, const uint8* pData, size_t size, size_t& offset )>;

        using TextWriteFn = Delegate<string( const void* pValPtr )>;
        using TextReadFn  = Delegate<bool( void* pValPtr, string_view valStr )>;

        using OwnedPointerCreateFn = void* (*)( void* pOuter, hashed_string typeName );
        using RuntimeTypeInfoFn    = const TypeInfo* (*)( const void* pInstance );

        /** @brief 맡겨 둔 원소의 원문 형식입니다(`OpaqueElementView`). */
        enum class OpaqueFormat : uint8
        {
            XML,
            JSON,
            Binary
        };
        /**
         * @brief 등록되지 않은 타입의 다형 원소 하나의 **원문**입니다(모듈이 안 뜬 컴포넌트 · 지운 타입).
         * @details 읽을 때 맡기고(`keepOpaqueElement`), 쓸 때 같은 형식이면 원문 그대로 다시 쓴다(`queryOpaqueElement`) — 건너뛰면 다음 저장에서
         *          영영 사라진다(유니티는 "Missing Script" 로 들고 있다 그대로 쓴다).
         */
        struct OpaqueElementView
        {
            string_view  _typeName;
            OpaqueFormat _format{ OpaqueFormat::XML };
            string_view  _text;              ///< XML 원소 · JSON 원소(`{ "타입": { … } }`) 원문
            const uint8* _pBytes{ nullptr }; ///< 바이너리 본문(이름 · 크기 머리 뒤)
            size_t       _byteCount{ 0 };
        };
        /** @brief 모르는 원소를 맡습니다. 맡았으면 true 입니다. */
        using OpaqueElementKeepFn = bool ( * )( void* pOuter, const OpaqueElementView& element );
        /** @brief 이 원소가 맡아 둔 원소인지 묻습니다. 그렇다면 원문을 채우고 true 입니다. */
        using OpaqueElementQueryFn = bool ( * )( const void* pElement, OpaqueElementView& outElement );

        // ------------------------------------------------------------------------------
        // 1) 수명: 목적은 필수, 키 대소문자는 기본 무시
        // ------------------------------------------------------------------------------
        /** @brief 목적 @p purpose 의 빈 문맥(핸들러 없음, 키 대소문자 무시)을 만듭니다. 내장 핸들러가 필요하면 `getDefault` · `deriveFromDefault` 를 씁니다. */
        explicit SerializeContext( SerializePurpose purpose ) noexcept
            : _mapBinaryWriter{}
            , _mapBinaryReader{}
            , _mapTextWriter{}
            , _mapTextReader{}
            , _pTypeRegistry{ nullptr }
            , _pOuterInstance{ nullptr }
            , _pOwnedPointerCreateFn{ nullptr }
            , _pRuntimeTypeInfoFn{ nullptr }
            , _pOpaqueKeepFn{ nullptr }
            , _pOpaqueQueryFn{ nullptr }
            , _purpose{ purpose }
            , _bIgnoreCaseKeys{ SW_TRUE }
            , _bSkipUnknownProperties{ SW_FALSE }
            , _bSkipUnreadableProperties{ SW_FALSE }
            , _reservedFlags{ 0 } {}

        // ------------------------------------------------------------------------------
        // 2) 핸들러 등록 · 키 정책
        // ------------------------------------------------------------------------------
        /** @brief 바이너리 읽기/쓰기 커스텀 핸들러를 등록합니다. */
        void registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn );
        /** @brief 텍스트 읽기/쓰기 커스텀 핸들러를 등록합니다. */
        void registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn );

        /** @brief 키 대소문자를 무시할지 설정합니다(자신을 반환하므로 이어 부를 수 있습니다). */
        SerializeContext& setIgnoreCaseKeys( bool bIgnoreCaseKeys )
        {
            _bIgnoreCaseKeys = bIgnoreCaseKeys ? SW_TRUE : SW_FALSE;
            return *this;
        }

        /**
         * @brief 바이너리 엄격 읽기가 **타입에 없는 이름**의 필드를 건너뛰고 계속할지 설정합니다. 켜면 (타입, 이름)마다 한 번 경고하고, 끄면(기본) 읽기가 실패합니다.
         * @details 같은 컨텍스트가 중첩 구조체 · 컨테이너 원소 · 맵 키까지 내려가므로 모든 깊이에서 같은 정책입니다. 혼합 빌드 사이의 메시지와 캐시는 이것만 켭니다
         *          (`MixedBuildCodec::getReadContext`, docs/09_Decisions.md 5-9 서버 버스 읽기 정책).
         */
        SerializeContext& setSkipUnknownProperties( bool bSkip )
        {
            _bSkipUnknownProperties = bSkip ? SW_TRUE : SW_FALSE;
            return *this;
        }

        /**
         * @brief 바이너리 엄격 읽기가 **타입에 있는데 값을 읽지 못한** 필드(모르는 열거자, 변환할 수 없게 바뀐 타입)를 건너뛰고 계속할지 설정합니다.
         * @details 켜면 그 필드는 지금 값 그대로 두고 (타입, 필드)마다 한 번 경고합니다. 끄면(기본) 읽기가 실패합니다. 값이 기본값으로 조용히 남으므로
         *          지운 필드를 버려도 되는 저장 상태(오브젝트 상태)만 켭니다. 혼합 빌드 사이의 메시지는 켜지 않습니다(모르는 열거자를 기본값으로 받게 됩니다).
         */
        SerializeContext& setSkipUnreadableProperties( bool bSkip )
        {
            _bSkipUnreadableProperties = bSkip ? SW_TRUE : SW_FALSE;
            return *this;
        }

        /**
         * @brief `setSkipUnknownProperties` 와 `setSkipUnreadableProperties` 를 함께 정하는 축약입니다(오브젝트 상태).
         * @details 바이너리의 버전 붙은 읽기에서는 두 플래그가 모두 켜졌을 때만 같은 버전의 orphan 정책이 `SchemaOrphanPolicy::Ignore` 가 됩니다. 버전이 다른 데이터는 켜도 거절합니다.
         */
        SerializeContext& setAllowUnknownProperties( bool bAllow )
        {
            setSkipUnknownProperties( bAllow );
            return setSkipUnreadableProperties( bAllow );
        }

        // ------------------------------------------------------------------------------
        // 3) 조회: 타입 이름 → 등록된 writer/reader
        // ------------------------------------------------------------------------------
        /** @brief 등록된 바이너리 writer 를 찾습니다. */
        const BinaryWriteFn* findBinaryWriter( hashed_string typeName ) const;
        /** @brief 등록된 바이너리 reader 를 찾습니다. */
        const BinaryReadFn* findBinaryReader( hashed_string typeName ) const;
        /** @brief 등록된 텍스트 writer 를 찾습니다. */
        const TextWriteFn* findTextWriter( hashed_string typeName ) const;
        /** @brief 등록된 텍스트 reader 를 찾습니다. */
        const TextReadFn* findTextReader( hashed_string typeName ) const;
        /**
         * @brief XML/JSON 의 키 · 태그 · 속성 이름을 찾을 때 대소문자를 무시하는지 반환합니다(기본 true). 값 비교에는 영향이 없습니다.
         * @details XMLSerializer::deserialize 가 이 값을 IXMLBackend 에 넘깁니다.
         *          끄려면: `SerializeContext context = SerializeContext::deriveFromDefault( purpose ); context.setIgnoreCaseKeys( false );`
         */
        bool ignoresCaseKeys() const { return _bIgnoreCaseKeys == SW_TRUE; }
        /** @brief 타입에 없는 이름의 필드를 건너뛰고 계속하는지(`setSkipUnknownProperties`) 반환합니다. */
        bool skipsUnknownProperties() const { return _bSkipUnknownProperties == SW_TRUE; }
        /** @brief 타입에 있는데 값을 읽지 못한 필드를 건너뛰고 계속하는지(`setSkipUnreadableProperties`) 반환합니다. */
        bool skipsUnreadableProperties() const { return _bSkipUnreadableProperties == SW_TRUE; }
        /**
         * @brief 저장 목적입니다. 만들 때 정하고 바꾸지 않습니다.
         * @details 목적이 쓰지 않는 필드는 읽을 때도 지금 값 그대로다(기본값을 채우지 않는다). 세이브는 옵트인 타입의 나머지 필드를, 저작 읽기는 `RuntimeState` 를 덮지 않는다.
         */
        SerializePurpose getPurpose() const { return _purpose; }

        /**
         * @brief 타입 · enum 을 찾을 레지스트리를 정합니다. nullptr 이면 엔진 전역 레지스트리입니다(기본).
         * @details 직렬화의 깊은 경로(`SerializerUtil`)는 전역을 직접 부르지 않고 이 컨텍스트에게 묻는다. 테스트는 자기 레지스트리를
         *          넣어 전역 등록 상태와 떨어진 채로 직렬화를 돌린다. 레지스트리는 이 컨텍스트로 직렬화하는 동안 살아 있어야 한다.
         */
        SerializeContext& setTypeRegistry( const TypeRegistry* pTypeRegistry )
        {
            _pTypeRegistry = pTypeRegistry;
            return *this;
        }

        /** @brief 이 컨텍스트가 타입 · enum 을 찾는 레지스트리입니다(`setTypeRegistry`, 없으면 엔진 전역). */
        const TypeRegistry& getTypeRegistry() const;

        /** @brief 소유 포인터 팩토리에 넘길 outer(소유자) 인스턴스를 설정합니다. */
        SerializeContext& setOuterInstance( void* pOuter )
        {
            _pOuterInstance = pOuter;
            return *this;
        }

        /** @brief `vector<T*>` 등 소유 포인터 원소를 만들 팩토리를 설정합니다. */
        SerializeContext& setOwnedPointerFactory( OwnedPointerCreateFn createFn )
        {
            _pOwnedPointerCreateFn = createFn;
            return *this;
        }

        /** @brief 소유 포인터 인스턴스의 런타임 TypeInfo 를 조회할 함수를 설정합니다. */
        SerializeContext& setRuntimeTypeInfoFn( RuntimeTypeInfoFn typeInfoFn )
        {
            _pRuntimeTypeInfoFn = typeInfoFn;
            return *this;
        }

        /** @brief 팩토리로 소유 포인터 인스턴스를 만듭니다. 팩토리가 없으면 nullptr 입니다. */
        void* createOwnedPointer( hashed_string typeName ) const
        {
            if ( _pOwnedPointerCreateFn == nullptr )
                return nullptr;
            return _pOwnedPointerCreateFn( _pOuterInstance, typeName );
        }

        /** @brief 인스턴스의 런타임 TypeInfo 입니다. 조회 함수를 설정하지 않았거나 함수가 건너뛰기로 답하면 nullptr 입니다. */
        const TypeInfo* getRuntimeTypeInfo( const void* pInstance ) const
        {
            if ( _pRuntimeTypeInfoFn == nullptr || pInstance == nullptr )
                return nullptr;
            return _pRuntimeTypeInfoFn( pInstance );
        }

        /** @brief 모르는 다형 원소를 맡고(읽기) 다시 쓰는(쓰기) 함수 쌍을 설정합니다(`OpaqueElementView`). */
        SerializeContext& setOpaqueElementHandlers( OpaqueElementKeepFn keepFn, OpaqueElementQueryFn queryFn )
        {
            _pOpaqueKeepFn  = keepFn;
            _pOpaqueQueryFn = queryFn;
            return *this;
        }

        /** @brief 모르는 원소를 맡깁니다. 맡을 곳이 없거나 거절하면 false — 부르는 쪽은 건너뜁니다. */
        bool keepOpaqueElement( const OpaqueElementView& element ) const
        {
            return _pOpaqueKeepFn != nullptr && _pOpaqueKeepFn( _pOuterInstance, element );
        }

        /** @brief @p pElement 가 맡아 둔 원소면 원문을 채우고 true 입니다. */
        bool queryOpaqueElement( const void* pElement, OpaqueElementView& outElement ) const
        {
            return _pOpaqueQueryFn != nullptr && pElement != nullptr && _pOpaqueQueryFn( pElement, outElement );
        }

        /** @brief 목적 @p purpose 의 기본 전역 직렬화 컨텍스트(내장 핸들러를 모두 등록함)를 반환합니다. 목적마다 하나씩 있습니다. */
        static const SerializeContext& getDefault( SerializePurpose purpose );

        /**
         * @brief 목적 @p purpose 의 기본 컨텍스트의 **핸들러를 빌려 쓰는** 빈 컨텍스트를 만듭니다.
         *
         * `SerializeContext context = getDefault( purpose );` 는 등록된 핸들러 표 네 벌(`unordered_map`)을 통째로
         * 복사합니다. 객체 하나당 659 ns 였고, 씬 로드는 그것을 엔티티마다 합니다(4000 개면 2.6 ms).
         * 표는 만들어진 뒤 바뀌지 않으므로 복사할 이유가 없습니다. 이쪽은 표를 가리키기만 하고,
         * 필요하면 자기 표에 더 등록합니다(조회는 자기 것 먼저, 없으면 빌려 온 쪽).
         */
        static SerializeContext deriveFromDefault( SerializePurpose purpose );

    private:
        /** @brief 목적 @p purpose 의 문맥에 내장 핸들러를 모두 등록해 만듭니다(`getDefault` 가 목적마다 한 번). */
        static SerializeContext makeDefault( SerializePurpose purpose );

        unordered_map<hashed_string, BinaryWriteFn> _mapBinaryWriter;
        unordered_map<hashed_string, BinaryReadFn>  _mapBinaryReader;
        unordered_map<hashed_string, TextWriteFn>   _mapTextWriter;
        unordered_map<hashed_string, TextReadFn>    _mapTextReader;
        /** @brief 자기 표에 없을 때 물어볼 곳입니다. 전역 기본 컨텍스트라 수명은 프로그램 전체입니다. */
        const SerializeContext* _pHandlerFallback{ nullptr };
        /** @brief 타입 · enum 을 찾을 곳입니다. nullptr 이면 엔진 전역 레지스트리입니다. */
        const TypeRegistry*    _pTypeRegistry;
        void*                  _pOuterInstance;
        OwnedPointerCreateFn   _pOwnedPointerCreateFn;
        RuntimeTypeInfoFn      _pRuntimeTypeInfoFn;
        OpaqueElementKeepFn    _pOpaqueKeepFn;
        OpaqueElementQueryFn   _pOpaqueQueryFn;
        SerializePurpose       _purpose;
        uint8                  _bIgnoreCaseKeys           : 1;
        uint8                  _bSkipUnknownProperties    : 1;
        uint8                  _bSkipUnreadableProperties : 1;
        [[maybe_unused]] uint8 _reservedFlags             : 5;
    };

} // namespace sw
