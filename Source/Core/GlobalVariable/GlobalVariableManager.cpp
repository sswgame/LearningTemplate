#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/Defines.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	namespace
	{
		struct GlobalVariableInternal
		{
			static void writeEnumValue( void* pData, uint32 typeSize, int32 val )
			{
				if ( pData == nullptr )
					return;
				if ( typeSize == 1 )
					*static_cast<uint8*>( pData ) = static_cast<uint8>( val );
				else if ( typeSize == 2 )
					*static_cast<uint16*>( pData ) = static_cast<uint16>( val );
				else if ( typeSize == 8 )
					*static_cast<int64*>( pData ) = static_cast<int64>( val );
				else
					*static_cast<int32*>( pData ) = val;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "GlobalVariableManager" );

	// ============================================================================
	// GlobalVariableInfo 구현부
	// ============================================================================

	/**
	 * @brief 전역 변수의 현재 값을 Boolean 타입으로 안전하게 반환합니다.
	 * @return 변수 타입이 Boolean인 경우 실제 값, 그 외에는 false
	 */
	bool GlobalVariableInfo::getValueAsBool() const
	{
		if ( _pData != nullptr && _type == GlobalVariableType::Boolean )
			return *static_cast<bool*>( _pData );
		return false;
	}

	/**
	 * @brief 전역 변수의 현재 값을 32비트 정수형(Int32 또는 Enum)으로 반환합니다.
	 * @return 변수 타입이 Int32 또는 Enum인 경우 실제 값, 그 외에는 0
	 */
	int32 GlobalVariableInfo::getValueAsInt() const
	{
		if ( _pData == nullptr )
			return 0;

		if ( _type == GlobalVariableType::Int32 )
			return *static_cast<const int32*>( _pData );

		if ( _type == GlobalVariableType::Enum )
		{
			if ( _typeSize == 1 )
				return *static_cast<const uint8*>( _pData );
			if ( _typeSize == 2 )
				return *static_cast<const uint16*>( _pData );
			if ( _typeSize == 8 )
				return static_cast<int32>( *static_cast<const int64*>( _pData ) );
			return *static_cast<const int32*>( _pData );
		}
		return 0;
	}

	/**
	 * @brief 전역 변수의 현재 값을 32비트 부동소수점(Float)으로 반환합니다.
	 * @return 변수 타입이 Float인 경우 실제 값, 그 외에는 0.0f
	 */
	float32 GlobalVariableInfo::getValueAsFloat() const
	{
		if ( _pData != nullptr && _type == GlobalVariableType::Float )
			return *static_cast<float32*>( _pData );
		return 0.0f;
	}

	/**
	 * @brief 전역 변수의 현재 값을 문자열 형태로 직렬화하여 반환합니다.
	 *
	 * [초심자 가이드 / 성능 최적화]:
	 * std::to_string()은 힙 메모리를 동적으로 할당하므로, 스택 기반의 StringBuilder<constant::kMaxBuffer32>를
	 * 사용하여 임시 힙 메모리 할당(Zero-Allocation) 없이 고속으로 포맷팅합니다.
	 */
	string GlobalVariableInfo::getValueAsString() const
	{
		if ( _pData == nullptr )
			return "";

		switch ( _type )
		{
			case GlobalVariableType::Boolean:
				return *static_cast<bool*>( _pData ) ? "true" : "false";
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
			{
				// 스택 버퍼를 활용한 정수 문자열 직결 변환
				StringBuilder<constant::kMaxBuffer32> sb;
				sb.append( getValueAsInt() );
				return string( sb.view() );
			}
			case GlobalVariableType::Float:
			{
				// 스택 버퍼를 활용한 실수 문자열 직결 변환
				StringBuilder<constant::kMaxBuffer32> sb;
				sb.append( *static_cast<float32*>( _pData ) );
				return string( sb.view() );
			}
			case GlobalVariableType::String:
				return *static_cast<string*>( _pData );
			default:
				break;
		}
		return "";
	}

	/**
	 * @brief 문자열 입력을 파싱하여 전역 변수의 원시 메모리(*_pData)에 값을 설정합니다.
	 *
	 * 값이 성공적으로 변경되면 등록된 변경 감지 콜백(_onValueChanged)을 호출합니다.
	 * [성능 최적화]: StringUtil::parse*를 사용하여 0-Allocation으로 고속 파싱합니다.
	 */
	bool GlobalVariableInfo::setValueFromString( string_view strValue )
	{
		if ( _pData == nullptr )
			return false;

		switch ( _type )
		{
			case GlobalVariableType::Boolean:
			{
				const bool bVal = StringUtil::parseBool( strValue, false );

				*static_cast<bool*>( _pData ) = bVal;
				if ( _onValueChanged.isBound() )
					_onValueChanged( this );
				return true;
			}
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
			{
				int32 val{ 0 };
				if ( StringUtil::parseInt( strValue, val ) )
				{
					if ( _type == GlobalVariableType::Enum )
						GlobalVariableInternal::writeEnumValue( _pData, _typeSize, val );
					else
						*static_cast<int32*>( _pData ) = val;

					if ( _onValueChanged.isBound() )
						_onValueChanged( this );
					return true;
				}
				return false;
			}
			case GlobalVariableType::Float:
			{
				float32 val{ 0.0f };
				if ( StringUtil::parseFloat( strValue, val ) )
				{
					*static_cast<float32*>( _pData ) = val;
					if ( _onValueChanged.isBound() )
						_onValueChanged( this );
					return true;
				}
				return false;
			}
			case GlobalVariableType::String:
			{
				*static_cast<string*>( _pData ) = string{ strValue };
				if ( _onValueChanged.isBound() )
					_onValueChanged( this );
				return true;
			}
			default:
				break;
		}
		return false;
	}

	/**
	 * @brief 변수 값을 등록 시 지정했던 기본값(_defaultValue)으로 되돌립니다.
	 */
	void GlobalVariableInfo::resetToDefault()
	{
		if ( _pData == nullptr )
			return;

		switch ( _type )
		{
			case GlobalVariableType::Boolean:
				if ( std::holds_alternative<bool>( _defaultValue ) )
					*static_cast<bool*>( _pData ) = std::get<bool>( _defaultValue );
				break;
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
				if ( std::holds_alternative<int32>( _defaultValue ) )
				{
					const int32 val = std::get<int32>( _defaultValue );
					if ( _type == GlobalVariableType::Enum )
						GlobalVariableInternal::writeEnumValue( _pData, _typeSize, val );
					else
						*static_cast<int32*>( _pData ) = val;
				}
				break;
			case GlobalVariableType::Float:
				if ( std::holds_alternative<float32>( _defaultValue ) )
					*static_cast<float32*>( _pData ) = std::get<float32>( _defaultValue );
				break;
			case GlobalVariableType::String:
				if ( std::holds_alternative<string>( _defaultValue ) )
					*static_cast<string*>( _pData ) = std::get<string>( _defaultValue );
				break;
			default:
				break;
		}

		if ( _onValueChanged.isBound() )
			_onValueChanged( this );
	}

	// ============================================================================
	// GlobalVariableManager 구현부
	// ============================================================================

	/**
	 * @brief 등록된 모든 전역 변수를 CommandLineManager의 허용 인자 목록에 등록합니다.
	 *
	 * 이를 통해 콘솔 명령줄에서 -g_MyVar=123 형태로 CLI 옵션을 전달받을 수 있게 됩니다.
	 */
	void GlobalVariableManager::registerToCommandLine( CommandLineManager* pCmdLineManager )
	{
		if ( pCmdLineManager == nullptr )
			return;

		for ( const auto& [name, info] : _mapVariable )
		{
			if ( std::holds_alternative<int32>( info._defaultValue ) )
				pCmdLineManager->addArgument<int32>( { info._name }, false, std::get<int32>( info._defaultValue ), true );
			else if ( std::holds_alternative<float32>( info._defaultValue ) )
				pCmdLineManager->addArgument<float32>( { info._name }, false, std::get<float32>( info._defaultValue ), true );
			else if ( std::holds_alternative<bool>( info._defaultValue ) )
				pCmdLineManager->addArgument<bool>( { info._name }, false, std::get<bool>( info._defaultValue ), true );
			else if ( std::holds_alternative<string>( info._defaultValue ) )
				pCmdLineManager->addArgument<string>( { info._name }, false, string( std::get<string>( info._defaultValue ) ), true );
		}
	}

	/**
	 * @brief 파싱된 커맨드라인 인자 값들을 조회하여 등록된 전역 변수들의 실제 값에 직접 동기화합니다.
	 *
	 * [초심자 가이드 / 성능 최적화]:
	 * 문자열로 재직렬화 후 재파싱하는 오버헤드를 배제하고, 원시 포인터(*_pData)에 직접 타입별 값을
	 * 대입한 뒤 값 변경 알림 콜백을 디스패치합니다.
	 */
	void GlobalVariableManager::updateFromCommandLine( const CommandLineManager* pCmdLineManager )
	{
		if ( pCmdLineManager == nullptr )
			return;

		for ( auto& [name, info] : _mapVariable )
		{
			if ( info._pData == nullptr )
				continue;

			if ( info._type == GlobalVariableType::Int32 || info._type == GlobalVariableType::Enum )
			{
				int32 val{ 0 };
				if ( pCmdLineManager->getArgument( name, val ) )
				{
					if ( info._type == GlobalVariableType::Enum )
						GlobalVariableInternal::writeEnumValue( info._pData, info._typeSize, val );
					else
						*static_cast<int32*>( info._pData ) = val;

					if ( info._onValueChanged.isBound() )
						info._onValueChanged( &info );
				}
			}
			else if ( info._type == GlobalVariableType::Float )
			{
				float32 val{ 0.0f };
				if ( pCmdLineManager->getArgument( name, val ) )
				{
					*static_cast<float32*>( info._pData ) = val;
					if ( info._onValueChanged.isBound() )
						info._onValueChanged( &info );
				}
			}
			else if ( info._type == GlobalVariableType::Boolean )
			{
				bool val{ false };
				if ( pCmdLineManager->getArgument( name, val ) )
				{
					*static_cast<bool*>( info._pData ) = val;
					if ( info._onValueChanged.isBound() )
						info._onValueChanged( &info );
				}
			}
			else if ( info._type == GlobalVariableType::String )
			{
				string val;
				if ( pCmdLineManager->getArgument( name, val ) )
				{
					*static_cast<string*>( info._pData ) = std::move( val );
					if ( info._onValueChanged.isBound() )
						info._onValueChanged( &info );
				}
			}
		}
	}

	/**
	 * @brief 새로운 전역 변수를 매니저에 등록합니다.
	 *
	 * [스레드 안전성]: std::unique_lock을 획득하여 동시 등록 레이스 컨디션을 방지합니다.
	 * [0-Alloc 검색]: std::string_view를 통해 불필요한 string 생성 없이 사전 존재 여부를 검사합니다.
	 */
	bool GlobalVariableManager::registerVariable( string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, string_view description, string_view enumType, string_view moduleName, uint32 typeSize )
	{
		if ( name.empty() || pData == nullptr )
			return false;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _mapVariable.find( name ) != _mapVariable.end() )
		{
			SW_LOG_WARNING( "Variable %# is already registered.", string( name ).c_str() );
			return false;
		}

		string strName{ name };

		GlobalVariableInfo info;
		info._name		   = strName;
		info._type		   = type;
		info._pData		   = pData;
		info._defaultValue = defaultValue;
		info._description  = string{ description };
		info._enumType	   = string{ enumType };
		info._moduleName   = string{ moduleName };
		info._typeSize	   = typeSize;

		_mapVariable.emplace( strName, std::move( info ) );
		return true;
	}

	/**
	 * @brief 특정 DLL/모듈 로드 시 정적 초기화로 연결된 체인(pHead)의 변수들을 일괄 등록합니다.
	 */
	void GlobalVariableManager::registerPendingVariables( string_view moduleName, const GlobalVariableRegistrar* pHead )
	{
		const GlobalVariableRegistrar* pCurrent = pHead;
		while ( pCurrent != nullptr )
		{
			registerVariable( pCurrent->_name,
							  pCurrent->_type,
							  pCurrent->_pData,
							  pCurrent->_defaultValue,
							  pCurrent->_description,
							  pCurrent->_enumType,
							  moduleName,
							  pCurrent->_typeSize );
			pCurrent = pCurrent->_pNext;
		}
	}

	/**
	 * @brief 특정 모듈(예: 언로드되는 SWGame.dll, EditorModule.dll)에 속한 전역 변수들을 일괄 해제합니다.
	 *
	 * [핫 리로드 안전성]: 모듈 언로드 시 댕글링 포인터 접근을 방지하기 위해 필수적으로 호출됩니다.
	 */
	void GlobalVariableManager::unregisterVariablesByModule( string_view moduleName )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		string								strModule{ moduleName };
		for ( auto it = _mapVariable.begin(); it != _mapVariable.end(); )
		{
			if ( it->second._moduleName == strModule )
				it = _mapVariable.erase( it );
			else
				++it;
		}
	}

	/**
	 * @brief 문자열 키로 변수를 찾아 문자열 값을 파싱/설정합니다.
	 */
	bool GlobalVariableManager::setValueFromString( string_view name, string_view strValue )
	{
		GlobalVariableInfo* pInfo = findVariable( name );
		if ( pInfo != nullptr )
			return pInfo->setValueFromString( strValue );
		return false;
	}

	/**
	 * @brief 특정 변수를 기본값으로 리셋합니다.
	 */
	bool GlobalVariableManager::resetToDefault( string_view name )
	{
		GlobalVariableInfo* pInfo = findVariable( name );
		if ( pInfo != nullptr )
		{
			pInfo->resetToDefault();
			return true;
		}
		return false;
	}

	/**
	 * @brief 등록된 모든 변수를 기본값으로 리셋합니다.
	 */
	void GlobalVariableManager::resetAllToDefault()
	{
		// Step 1: unique_lock 안에서 값만 직접 리셋, 콜백 목록을 추출
		vector<pair<GlobalVariableChangedDelegate, GlobalVariableInfo*>> listPendingCallback;
		{
			std::unique_lock<std::shared_mutex> lock{ _mutex };
			for ( auto& [name, info] : _mapVariable )
			{
				if ( info._pData == nullptr )
					continue;

				switch ( info._type )
				{
					case GlobalVariableType::Boolean:
						if ( std::holds_alternative<bool>( info._defaultValue ) )
							*static_cast<bool*>( info._pData ) = std::get<bool>( info._defaultValue );
						break;
					case GlobalVariableType::Int32:
					case GlobalVariableType::Enum:
						if ( std::holds_alternative<int32>( info._defaultValue ) )
						{
							const int32 val = std::get<int32>( info._defaultValue );
							if ( info._type == GlobalVariableType::Enum )
								GlobalVariableInternal::writeEnumValue( info._pData, info._typeSize, val );
							else
								*static_cast<int32*>( info._pData ) = val;
						}
						break;
					case GlobalVariableType::Float:
						if ( std::holds_alternative<float32>( info._defaultValue ) )
							*static_cast<float32*>( info._pData ) = std::get<float32>( info._defaultValue );
						break;
					case GlobalVariableType::String:
						if ( std::holds_alternative<string>( info._defaultValue ) )
							*static_cast<string*>( info._pData ) = std::get<string>( info._defaultValue );
						break;
					default:
						break;
				}

				if ( info._onValueChanged.isBound() )
					listPendingCallback.push_back( { info._onValueChanged, &info } );
			}
		} // unique_lock 해제

		// Step 2: 락 밖에서 콜백 호출 (재진입 안전)
		for ( auto& [delegate, pInfo] : listPendingCallback )
			delegate( pInfo );
	}

	/**
	 * @brief 이름(string_view)으로 전역 변수 정보를 고속 검색합니다.
	 *
	 * [동시성 및 성능]:
	 * std::shared_lock을 통한 다중 스레드 동시 읽기를 지원하며, Heterogeneous Lookup을 통해
	 * string 임시 객체 생성 없이 0-Alloc으로 즉시 검색합니다.
	 */
	GlobalVariableInfo* GlobalVariableManager::findVariable( string_view name )
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		const auto							iter = _mapVariable.find( name );
		if ( iter != _mapVariable.end() )
			return const_cast<GlobalVariableInfo*>( &iter->second );
		return nullptr;
	}

	/**
	 * @brief 등록된 변수 이름 목록을 스냅샷으로 반환합니다. (thread-safe)
	 *
	 * 패널 등에서 반복 후 findVariable 로 편집 가능한 포인터를 얻으려는 용도로 설계되었습니다.
	 */
	vector<string> GlobalVariableManager::collectVariableNames() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		vector<string>						listName;
		listName.reserve( static_cast<uint32>( _mapVariable.size() ) );
		for ( const auto& [name, info] : _mapVariable )
			listName.push_back( name );
		return listName;
	}

	/**
	 * @brief 등록된 변수 수를 반환합니다. (thread-safe)
	 */
	uint32 GlobalVariableManager::getVariableCount() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		return static_cast<uint32>( _mapVariable.size() );
	}

	// ============================================================================
	// GlobalVariableRegistrar 구현부
	// ============================================================================

	/**
	 * @brief Core 모듈 내부 번역 단위 전용 정적 등록자 생성자 (Core::getHead()에 연결)
	 */
	GlobalVariableRegistrar::GlobalVariableRegistrar( const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType, const utf8* pModuleName, uint32 typeSize )
		: _name{ pName }
		, _type{ type }
		, _pData{ pData }
		, _defaultValue{ defaultValue }
		, _description{ pDescription }
		, _enumType{ pEnumType }
		, _moduleName{ pModuleName }
		, _typeSize{ typeSize }
		, _pNext{ nullptr } { linkTo( getHead() ); }

	/**
	 * @brief DLL 동적 모듈 전용 정적 등록자 생성자 (모듈별 로컬 체인 헤드에 연결)
	 */
	GlobalVariableRegistrar::GlobalVariableRegistrar( GlobalVariableRegistrar*& pModuleHead, const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType, const utf8* pModuleName, uint32 typeSize )
		: _name{ pName }
		, _type{ type }
		, _pData{ pData }
		, _defaultValue{ defaultValue }
		, _description{ pDescription }
		, _enumType{ pEnumType }
		, _moduleName{ pModuleName }
		, _typeSize{ typeSize }
		, _pNext{ nullptr } { linkTo( pModuleHead ); }

	GlobalVariableRegistrar*& GlobalVariableRegistrar::getHead()
	{
		static GlobalVariableRegistrar* s_pHead{ nullptr };
		return s_pHead;
	}

	void GlobalVariableRegistrar::linkTo( GlobalVariableRegistrar*& pModuleHead )
	{
		_pNext		= pModuleHead;
		pModuleHead = this;
	}
} // namespace sw
