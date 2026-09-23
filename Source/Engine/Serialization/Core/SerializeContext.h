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
     *        소유 포인터 팩토리, 객체 중복 제거 표를 듭니다.
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

        // ------------------------------------------------------------------------------
        // 1) 수명: 기본은 키 대소문자 무시
        // ------------------------------------------------------------------------------
        /** @brief 기본 설정(키 대소문자 무시)으로 만듭니다. */
        SerializeContext() noexcept
            : _mapBinaryWriter{}
            , _mapBinaryReader{}
            , _mapTextWriter{}
            , _mapTextReader{}
            , _mapObjectToId{}
            , _mapIdToObject{}
            , _pOuterInstance{ nullptr }
            , _pOwnedPointerCreateFn{ nullptr }
            , _pRuntimeTypeInfoFn{ nullptr }
            , _bIgnoreCaseKeys{ SW_TRUE }
            , _bAllowUnknownProperties{ SW_FALSE }
            , _bEnableObjectDeduplication{ SW_FALSE }
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

        /** @brief 객체 중복 제거(포인터 표)를 켤지 설정합니다. */
        SerializeContext& setEnableObjectDeduplication( bool bEnable )
        {
            _bEnableObjectDeduplication = bEnable ? SW_TRUE : SW_FALSE;
            return *this;
        }

        bool isObjectDeduplicationEnabled() const { return _bEnableObjectDeduplication == SW_TRUE; }

        /** @brief 포인터 객체를 표에 등록하거나, 이미 등록돼 있으면 그 ID 를 반환합니다. */
        uint32 registerOrFindObjectId( const void* pInstance ) const
        {
            if ( pInstance == nullptr )
                return 0;
            const auto it = _mapObjectToId.find( pInstance );
            if ( it != _mapObjectToId.end() )
                return it->second;
            const uint32 newId = static_cast<uint32>( _mapObjectToId.size() + 1 );
            _mapObjectToId.emplace( pInstance, newId );
            return newId;
        }

        /** @brief 포인터 객체가 이미 등록된 ID 를 찾습니다. */
        bool findObjectId( const void* pInstance, uint32& outId ) const
        {
            if ( pInstance == nullptr )
                return false;
            const auto it = _mapObjectToId.find( pInstance );
            if ( it == _mapObjectToId.end() )
                return false;
            outId = it->second;
            return true;
        }

        /** @brief ID 에 대응하는 역직렬화 인스턴스 주소를 등록합니다. */
        void registerObjectWithId( uint32 objectId, void* pInstance ) const
        {
            if ( objectId != 0 && pInstance != nullptr )
                _mapIdToObject[objectId] = pInstance;
        }

        /** @brief ID 로 역직렬화된 인스턴스 주소를 찾습니다. */
        void* findObjectById( uint32 objectId ) const
        {
            const auto it = _mapIdToObject.find( objectId );
            if ( it != _mapIdToObject.end() )
                return it->second;
            return nullptr;
        }

        /** @brief 객체 포인터 표를 비웁니다. */
        void clearObjectTable() const
        {
            _mapObjectToId.clear();
            _mapIdToObject.clear();
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
        mutable unordered_map<const void*, uint32>  _mapObjectToId;
        mutable unordered_map<uint32, void*>        _mapIdToObject;
        /** @brief 자기 표에 없을 때 물어볼 곳입니다. 전역 기본 컨텍스트라 수명은 프로그램 전체입니다. */
        const SerializeContext* _pHandlerFallback{ nullptr };
        void*                   _pOuterInstance;
        OwnedPointerCreateFn    _pOwnedPointerCreateFn;
        RuntimeTypeInfoFn       _pRuntimeTypeInfoFn;
        uint8                   _bIgnoreCaseKeys            : 1;
        uint8                   _bAllowUnknownProperties    : 1;
        uint8                   _bEnableObjectDeduplication : 1;
        [[maybe_unused]] uint8  _reservedFlags              : 5;
    };

} // namespace sw
