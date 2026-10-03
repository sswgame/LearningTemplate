/**
 * @file SerializeContext.h
 * @brief 타입별 커스텀 바이너리/텍스트 직렬화 핸들러와 직렬화 설정을 담는 SerializeContext 입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class SerializeContext
     * @brief 직렬화 한 번에 쓰는 설정 묶음입니다. 타입별 커스텀 바이너리/텍스트 핸들러, 키 정책,
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
            Xml,
            Json,
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
            OpaqueFormat _format{ OpaqueFormat::Xml };
            string_view  _text;              ///< XML 원소 · JSON 원소(`{ "타입": { … } }`) 원문
            const uint8* _pBytes{ nullptr }; ///< 바이너리 본문(이름 · 크기 머리 뒤)
            size_t       _byteCount{ 0 };
        };
        /** @brief 모르는 원소를 맡습니다. 맡았으면 true 입니다. */
        using OpaqueElementKeepFn = bool ( * )( void* pOuter, const OpaqueElementView& element );
        /** @brief 이 원소가 맡아 둔 원소인지 묻습니다. 그렇다면 원문을 채우고 true 입니다. */
        using OpaqueElementQueryFn = bool ( * )( const void* pElement, OpaqueElementView& outElement );

        // ------------------------------------------------------------------------------
        // 1) 수명: 기본은 키 대소문자 무시
        // ------------------------------------------------------------------------------
        /** @brief 기본 설정(키 대소문자 무시)으로 만듭니다. */
        SerializeContext() noexcept
            : _mapBinaryWriter{}
            , _mapBinaryReader{}
            , _mapTextWriter{}
            , _mapTextReader{}
            , _pOuterInstance{ nullptr }
            , _pOwnedPointerCreateFn{ nullptr }
            , _pRuntimeTypeInfoFn{ nullptr }
            , _pOpaqueKeepFn{ nullptr }
            , _pOpaqueQueryFn{ nullptr }
            , _bIgnoreCaseKeys{ SW_TRUE }
            , _bAllowUnknownProperties{ SW_FALSE }
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

        /** @brief 바이너리/텍스트 역직렬화에서 스키마에 없는 프로퍼티를 건너뛰고 계속할지 설정합니다. */
        SerializeContext& setAllowUnknownProperties( bool bAllow )
        {
            _bAllowUnknownProperties = bAllow ? SW_TRUE : SW_FALSE;
            return *this;
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
         * @brief Xml/Json 의 키 · 태그 · 속성 이름을 찾을 때 대소문자를 무시하는지 반환합니다(기본 true). 값 비교에는 영향이 없습니다.
         * @details XmlSerializer::deserialize 가 이 값을 IXmlBackend 에 넘깁니다.
         *          끄려면: `SerializeContext ctx = SerializeContext::deriveFromDefault(); ctx.setIgnoreCaseKeys( false );`
         */
        bool ignoresCaseKeys() const { return _bIgnoreCaseKeys == SW_TRUE; }
        /** @brief 스키마에 없는 프로퍼티를 건너뛰고 계속하는지 반환합니다. */
        bool allowsUnknownProperties() const { return _bAllowUnknownProperties == SW_TRUE; }

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

        /** @brief 기본 전역 직렬화 컨텍스트를 반환합니다. */
        static const SerializeContext& getDefault();

        /**
         * @brief 기본 컨텍스트의 **핸들러를 빌려 쓰는** 빈 컨텍스트를 만듭니다.
         *
         * `SerializeContext ctx = getDefault();` 는 등록된 핸들러 표 네 벌(`unordered_map`)을 통째로
         * 복사합니다. 객체 하나당 659 ns 였고, 씬 로드는 그것을 엔티티마다 합니다(4000 개면 2.6 ms).
         * 표는 만들어진 뒤 바뀌지 않으므로 복사할 이유가 없습니다. 이쪽은 표를 가리키기만 하고,
         * 필요하면 자기 표에 더 등록합니다(조회는 자기 것 먼저, 없으면 빌려 온 쪽).
         */
        static SerializeContext deriveFromDefault();

    private:
        unordered_map<hashed_string, BinaryWriteFn> _mapBinaryWriter;
        unordered_map<hashed_string, BinaryReadFn>  _mapBinaryReader;
        unordered_map<hashed_string, TextWriteFn>   _mapTextWriter;
        unordered_map<hashed_string, TextReadFn>    _mapTextReader;
        /** @brief 자기 표에 없을 때 물어볼 곳입니다. 전역 기본 컨텍스트라 수명은 프로그램 전체입니다. */
        const SerializeContext* _pHandlerFallback{ nullptr };
        void*                   _pOuterInstance;
        OwnedPointerCreateFn    _pOwnedPointerCreateFn;
        RuntimeTypeInfoFn       _pRuntimeTypeInfoFn;
        OpaqueElementKeepFn     _pOpaqueKeepFn;
        OpaqueElementQueryFn    _pOpaqueQueryFn;
        uint8                   _bIgnoreCaseKeys         : 1;
        uint8                   _bAllowUnknownProperties : 1;
        [[maybe_unused]] uint8  _reservedFlags           : 6;
    };

} // namespace sw
