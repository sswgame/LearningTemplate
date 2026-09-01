/**
 * @file TypeRegistry.h
 * @brief TypeRegistry와 정적 Type/Enum registrar 연결
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{

	/**
	 * @brief 컴파일러 함수 시그니처에서 템플릿 인자 E 의 타입 이름만 뽑아냅니다.
	 * @details `[E = Foo::Bar]`(clang/gcc)와 `typeFqn<enum Foo::Bar>`(MSVC) 두 형태를 모두 받습니다.
	 *          반환 타입에도 `<>` 가 들어 있으므로 함수 이름을 기준점으로 삼아야 합니다.
	 */
	inline hashed_string parseTypeFromSignature( string_view sig )
	{
		string_view inner;

		const size_t eq = sig.find( constants::reflection::kSignatureEq );
		if ( eq != string_view::npos )
		{
			const size_t eqLength = StringUtil::strlen( constants::reflection::kSignatureEq );
			inner				  = sig.substr( eq + eqLength );
			const size_t semi	  = inner.find( ';' );
			const size_t br		  = inner.find( ']' );
			size_t		 end	  = inner.size();
			if ( semi != string_view::npos )
				end = semi;
			if ( br != string_view::npos && br < end )
				end = br;
			inner = inner.substr( 0, end );
		}
		else
		{
			constexpr string_view kMarker = constants::reflection::kTypeFqnPrefix;
			const size_t		  lt	  = sig.find( kMarker );
			const size_t		  gt	  = sig.rfind( '>' );
			if ( lt != string_view::npos && gt != string_view::npos && gt > lt + kMarker.size() )
				inner = sig.substr( lt + kMarker.size(), gt - lt - kMarker.size() );
		}

		inner											= StringUtil::trim( inner );
		static constexpr const utf8* kArrTypePrefixes[] = {
			constants::reflection::kEnumClassPrefix,
			constants::reflection::kEnumStructPrefix,
			constants::reflection::kEnumPrefix,
			constants::reflection::kClassPrefix,
			constants::reflection::kStructPrefix,
		};
		for ( const utf8* prefix : kArrTypePrefixes )
		{
			if ( StringUtil::startsWith( inner, prefix ) )
			{
				inner.remove_prefix( StringUtil::strlen( prefix ) );
				break;
			}
		}
		inner = StringUtil::trim( inner );
		if ( inner.empty() )
			return {};
		return hashed_string( inner.data(), static_cast<uint32>( inner.size() ) );
	}

	template <typename E>
	/** @brief 컴파일러 시그니처에서 enum FQN을 추출합니다. */
	hashed_string typeFqn()
	{
		static const hashed_string kFqn = parseTypeFromSignature( SW_FUNCTION_SIGNATURE );
		return kFqn;
	}

	/**
	 * @class TypeRegistry
	 * @brief 리플렉션 TypeInfo / EnumInfo 등록·조회·별칭
	 */
	class SW_API TypeRegistry
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — 모듈별 등록/해제, 복제 금지
		// ------------------------------------------------------------------------------
		/** @brief 빈 레지스트리를 만듭니다. */
		TypeRegistry();
		/** @brief 등록된 타입을 정리합니다. */
		~TypeRegistry();

		/** @brief 복사를 금지합니다. */
		TypeRegistry( const TypeRegistry& ) = delete;
		/** @brief 대입을 금지합니다. */
		TypeRegistry& operator=( const TypeRegistry& ) = delete;

		// ------------------------------------------------------------------------------
		// 2) 모듈 일괄 등록 — registrar 체인 → 맵, 핫리로드 시 모듈 단위 해제
		// ------------------------------------------------------------------------------
		/** @brief 모듈의 pending Type/Enum registrar 체인을 등록합니다. */
		void registerPendingTypes( string_view moduleName, struct TypeRegistrar* pClassHead, struct EnumRegistrar* pEnumHead );
		/** @brief 해당 모듈이 등록한 타입을 모두 해제합니다. */
		void unregisterTypesByModule( string_view moduleName );

		// ------------------------------------------------------------------------------
		// 3) 단건 등록 · 별칭 — REFLECT(Alias=…) / ReflectBuiltins codegen
		// ------------------------------------------------------------------------------
		/** @brief TypeInfo를 등록합니다. */
		void registerClass( const TypeInfo& info );
		/** @brief EnumInfo를 등록합니다. */
		void registerEnum( const EnumInfo& info );

		/**
		 * @brief 옛 이름 → 이미 등록된 canonical TypeInfo (직렬화/컴포넌트 키 호환).
		 * @details REFLECT(Alias=…) / ReflectBuiltins 별칭 codegen이 호출한다.
		 */
		void registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName );
		/** @brief 옛 이름 → 이미 등록된 canonical EnumInfo. */
		void registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName );

		// ------------------------------------------------------------------------------
		// 4) 조회 — 이름/FQN/해시, 별칭 포함
		// ------------------------------------------------------------------------------
		/** @brief 이름 또는 FQN으로 TypeInfo를 찾습니다. */
		const TypeInfo* findType( const hashed_string& nameOrFqn ) const;
		/** @brief 이름 또는 FQN으로 EnumInfo를 찾습니다. */
		const EnumInfo* findEnum( const hashed_string& nameOrFqn ) const;

		template <typename T>
		/** @brief 템플릿 인자 타입 T의 TypeInfo를 조회합니다. */
		const TypeInfo* findType() const
		{
			if constexpr ( HasStaticType_v<T> )
				return T::StaticType();
			else if constexpr ( HasReflectStaticType_v<T> )
				return ReflectTypeTraits<T>::StaticType();
			else
				return findType( typeFqn<T>() );
		}

		template <typename E>
		/** @brief 템플릿 enum 타입 E의 EnumInfo를 조회합니다. */
		const EnumInfo* findEnum() const
		{
			return findEnumOf<E>();
		}

		template <typename Func>
		/** @brief 등록된 모든 고유 TypeInfo를 순회합니다. */
		void forEachType( Func&& func ) const
		{
			std::shared_lock<std::shared_mutex> lock( _mutex );
			for ( const auto& [key, typeInfo] : _mapNameToClassType )
			{
				if ( key == typeInfo._fullyQualifiedName )
					func( typeInfo );
			}
		}

		template <typename Func>
		/** @brief 등록된 모든 고유 EnumInfo를 순회합니다. */
		void forEachEnum( Func&& func ) const
		{
			std::shared_lock<std::shared_mutex> lock( _mutex );
			for ( const auto& [key, enumInfo] : _mapNameToEnum )
			{
				if ( key == enumInfo._fullyQualifiedName )
					func( enumInfo );
			}
		}

		template <typename BaseType>
		/** @brief BaseType으로부터 파생된 모든 등록 TypeInfo 목록을 반환합니다. */
		vector<const TypeInfo*> getDerivedTypes() const
		{
			const TypeInfo* pBaseType = findType<BaseType>();
			if ( pBaseType == nullptr )
				return {};

			const hashed_string&	baseFqn = pBaseType->_fullyQualifiedName;
			vector<const TypeInfo*> listResult;

			std::shared_lock<std::shared_mutex> lock( _mutex );
			for ( const auto& [key, typeInfo] : _mapNameToClassType )
			{
				if ( key == typeInfo._fullyQualifiedName && &typeInfo != pBaseType && typeInfo.isDerivedFrom( baseFqn ) )
				{
					listResult.push_back( &typeInfo );
				}
			}
			return listResult;
		}

		/** @brief 별칭 포함 조회 후 canonical `_name` (미등록이면 입력 그대로). */
		hashed_string canonicalTypeName( const hashed_string& nameOrFqn ) const;
		/** @brief 맵 키 해시가 일치하는 항목의 canonical `_name`. */
		hashed_string canonicalTypeNameByHash( uint32 nameHash ) const;

		/** @brief nameOrFqn이 canonicalName과 같은 등록 타입인지 (별칭 포함). */
		bool isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const;
		/** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
		bool isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const;
		/** @brief string_view 이름으로 타입 일치 여부를 검사합니다. */
		bool isType( string_view nameOrFqn, const utf8* pCanonicalName ) const;

		// ------------------------------------------------------------------------------
		// 5) Enum 변환 · 비트플래그
		// ------------------------------------------------------------------------------
		/** @brief enumerator 이름. Invalid/Count이거나 미등록이면 nullptr. */
		const utf8* enumToString( const hashed_string& enumName, int64 value ) const;
		/** @brief 이름 → 값. 실패 시 false이고 outValue는 Invalid(없으면 0). */
		bool enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const;
		/** @brief ENUM(Flags) / 비트플래그 EnumInfo에 대해 `(flags & contains) == contains`. */
		bool hasFlag( const hashed_string& enumName, int64 flags, int64 contains ) const;

		template <typename E>
		/** @brief 템플릿 enum 값을 등록된 이름으로 변환합니다. */
		const utf8* enumToString( E value ) const
		{
			static_assert( std::is_enum_v<E>, "enumToString requires an enum type" );
			const EnumInfo* pInfo = findEnumOf<E>();
			return pInfo != nullptr ? pInfo->valueToCString( static_cast<int64>( value ) ) : nullptr;
		}

		template <typename E>
		/** @brief 문자열을 템플릿 enum 값으로 파싱합니다. */
		bool enumFromString( string_view name, E& outValue ) const
		{
			static_assert( std::is_enum_v<E>, "enumFromString requires an enum type" );
			const EnumInfo* pInfo = findEnumOf<E>();
			int64			raw{ 0 };
			if ( pInfo == nullptr )
			{
				outValue = E{};
				return false;
			}
			if ( pInfo->tryParse( name, raw ) && pInfo->isValidValue( raw ) )
			{
				outValue = static_cast<E>( raw );
				return true;
			}
			outValue = static_cast<E>( ( pInfo->_bHasInvalid != 0 ) ? pInfo->_invalidValue : 0 );
			return false;
		}

		template <typename E>
		/** @brief 파싱에 실패하면 Invalid(없으면 0)를 돌려줍니다. */
		E enumFromString( string_view name ) const
		{
			E value{};
			enumFromString( name, value );
			return value;
		}

		template <typename E>
		/** @brief 템플릿 비트플래그 enum에 대해 포함 여부를 검사합니다. */
		bool hasFlag( E flags, E contains ) const
		{
			static_assert( std::is_enum_v<E>, "hasFlag requires an enum type" );
			const EnumInfo* pInfo = findEnumOf<E>();
			if ( pInfo == nullptr || pInfo->_bIsBitFlag == 0 )
				return false;
			using U = std::underlying_type_t<E>;
			return ( static_cast<U>( flags ) & static_cast<U>( contains ) ) == static_cast<U>( contains );
		}

		// ------------------------------------------------------------------------------
		// 6) 리플렉션 호출
		// ------------------------------------------------------------------------------
		/** @brief 등록된 메서드를 인자 목록으로 호출합니다. */
		TaskValue invokeMethod( void* pInstance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args = {} ) const;

	private:
		template <typename E>
		/** @brief 템플릿 enum의 FQN/리프로 EnumInfo를 찾습니다. */
		const EnumInfo* findEnumOf() const
		{
			const hashed_string fqn	  = typeFqn<E>();
			const EnumInfo*		pInfo = findEnum( fqn );
			if ( pInfo != nullptr )
				return pInfo;
			const utf8* pCstr = fqn.c_str();
			if ( pCstr == nullptr || *pCstr == 0 )
				return nullptr;
			const string_view fqnView{ pCstr };
			const size_t	  lastScope = fqnView.rfind( constants::reflection::kScopeDelimiter );
			if ( lastScope != string_view::npos )
			{
				const size_t	  delimiterLength = StringUtil::strlen( constants::reflection::kScopeDelimiter );
				const string_view leaf			  = fqnView.substr( lastScope + delimiterLength );
				return findEnum( hashed_string( leaf.data(), static_cast<uint32>( leaf.size() ) ) );
			}
			return nullptr;
		}

		mutable std::shared_mutex			   _mutex;
		unordered_map<hashed_string, TypeInfo> _mapNameToClassType;
		unordered_map<hashed_string, EnumInfo> _mapNameToEnum;
		unordered_map<uint32, hashed_string>   _mapHashToCanonicalName;
		hashed_string						   _activeModuleName;
	};

	// ------------------------------------------------------------------------------
	// 7) TypeRegistrar — 정적 초기화로 Type 등록 함수를 체인에 연결
	//    Core TU는 getHead(), 핫리로드 모듈은 모듈 로컬 헤드
	// ------------------------------------------------------------------------------
	struct SW_API TypeRegistrar
	{
		void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 TypeInfo를 넣는 함수
		TypeRegistrar* _pNext;					  ///< 같은 헤드의 다음 registrar

		/** @brief Engine.dll(Core OBJECT) 전용 registrar 리스트 헤드. */
		static TypeRegistrar*& getHead();

		/** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
		TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) );
		/** @brief 핫리로드 모듈 등 외부 registrar 등록에 사용합니다. */
		TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead );
	};

	// ------------------------------------------------------------------------------
	// 8) EnumRegistrar — 정적 초기화로 Enum 등록 함수를 체인에 연결
	//    Core TU는 getHead(), 핫리로드 모듈은 모듈 로컬 헤드
	// ------------------------------------------------------------------------------
	struct SW_API EnumRegistrar
	{
		void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 EnumInfo를 넣는 함수
		EnumRegistrar* _pNext;					  ///< 같은 헤드의 다음 registrar

		/** @brief Engine.dll(Core OBJECT) 전용 enum registrar 리스트 헤드. */
		static EnumRegistrar*& getHead();

		/** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
		EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) );
		/** @brief 핫리로드 모듈 등 외부 registrar 등록에 사용합니다. */
		EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead );
	};

	// ------------------------------------------------------------------------------
	// 9) 인라인 구현 — 조회 / 검사
	// ------------------------------------------------------------------------------
	inline hashed_string TypeRegistry::canonicalTypeName( const hashed_string& nameOrFqn ) const
	{
		const TypeInfo* pInfo = findType( nameOrFqn );
		if ( pInfo != nullptr )
		{
			if ( pInfo->_name.empty() == false )
				return pInfo->_name;
			return pInfo->_fullyQualifiedName;
		}
		return nameOrFqn;
	}

	/** @brief nameOrFqn이 canonicalName과 같은 등록 타입인지 (별칭 포함). */
	inline bool TypeRegistry::isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const
	{
		const TypeInfo* pLhs = findType( nameOrFqn );
		const TypeInfo* pRhs = findType( canonicalName );
		if ( pLhs != nullptr && pRhs != nullptr )
			return pLhs->_typeId == pRhs->_typeId;
		return canonicalTypeName( nameOrFqn ) == canonicalTypeName( canonicalName );
	}

	/** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
	inline bool TypeRegistry::isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const { return pCanonicalName != nullptr && isType( nameOrFqn, hashed_string( pCanonicalName ) ); }

	/** @brief string_view 이름으로 타입 일치 여부를 검사합니다. */
	inline bool TypeRegistry::isType( string_view nameOrFqn, const utf8* pCanonicalName ) const
	{
		if ( nameOrFqn.empty() || pCanonicalName == nullptr )
			return false;
		return isType( hashed_string{ nameOrFqn }, pCanonicalName );
	}

	/** @brief 부모 FQN을 따라가며 targetFqn에서 파생됐는지 검사합니다. */
} // namespace sw

namespace sw::engine
{
	/** @brief 전역 TypeRegistry 인스턴스를 반환합니다. */
	SW_API TypeRegistry& getTypeRegistry();
} // namespace sw::engine

#ifndef SW_TYPE_MODULE_HEAD
	#define SW_TYPE_MODULE_HEAD() ( ::sw::TypeRegistrar::getHead() )
#endif
#ifndef SW_ENUM_MODULE_HEAD
	#define SW_ENUM_MODULE_HEAD() ( ::sw::EnumRegistrar::getHead() )
#endif
