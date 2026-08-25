/**
 * @file XmlDocument.h
 * @brief 리플렉션 없는 XML 파싱·탐색 (콘텐츠 테이블, 맵, 툴)
 * @note 리플렉션 객체 그래프는 XmlSerializer / IXmlBackend를 사용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	/**
	 * @class XmlAttribute
	 * @brief XML 속성 경량 핸들
	 */
	class SW_API XmlAttribute
	{
	public:
		/** @brief 빈(무효) 속성 핸들. */
		XmlAttribute() = default;

		/** @brief 속성이 유효하면 true. */
		bool isValid() const { return _pAttr != nullptr; }
		/** @brief isValid()와 동일. */
		explicit operator bool() const { return isValid(); }

		/** @brief 속성 이름을 반환합니다. */
		const utf8* name() const;
		/** @brief 속성 값을 반환합니다. */
		const utf8* value() const;
		/** @brief 다음 속성을 반환합니다. */
		XmlAttribute next() const;

	private:
		friend class XmlNode;
		/** @brief RapidXML 속성 포인터로 핸들을 만듭니다. */
		explicit XmlAttribute( void* pAttr )
			: _pAttr{ pAttr } {}

		void* _pAttr{ nullptr };
	};

	/**
	 * @class XmlNode
	 * @brief XmlDocument 안의 경량 핸들 (clear/destroy 이후 무효)
	 */
	class SW_API XmlNode
	{
	public:
		/** @brief 빈(무효) 노드 핸들. */
		XmlNode() = default;

		/** @brief 노드가 유효하면 true. */
		bool isValid() const { return _pNode != nullptr; }
		/** @brief isValid()와 동일. */
		explicit operator bool() const { return isValid(); }

		// ------------------------------------------------------------------------------
		// 1) 읽기 — 이름, 텍스트, 속성, 자식/형제
		// ------------------------------------------------------------------------------
		/** @brief 엘리먼트 이름을 반환합니다. */
		const utf8* name() const;
		/** @brief 엘리먼트 텍스트. 유효하면 빈 문자열이어도 nullptr이 아님. */
		const utf8* text() const;
		/** @brief 속성 값을 반환합니다. 없으면 nullptr. */
		const utf8* attr( const utf8* pName, bool bIgnoreCaseKeys = true ) const;
		/** @brief 속성 값을 정수로 반환합니다. */
		int32 attrInt( const utf8* pName, int32 fallback = 0, bool bIgnoreCaseKeys = true ) const;
		/** @brief 속성 값을 실수로 반환합니다. */
		float32 attrFloat( const utf8* pName, float32 fallback = 0.f, bool bIgnoreCaseKeys = true ) const;

		/** @brief 자식 노드를 찾습니다. pName==nullptr이면 첫 자식. */
		XmlNode child( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
		/** @brief 다음 형제 노드를 반환합니다. */
		XmlNode next( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;
		/** @brief 지정 이름 자식의 텍스트를 반환합니다. */
		const utf8* childText( const utf8* pName, bool bIgnoreCaseKeys = true ) const;

		/** @brief 비어 있지 않은 자식 텍스트를 dst에 복사합니다. 썼으면 true. */
		bool takeChildText( const utf8* pName, string& dst, bool bIgnoreCaseKeys = true ) const;

		/** @brief 첫 속성을 반환합니다. */
		XmlAttribute firstAttr() const;

		// ------------------------------------------------------------------------------
		// 2) 쓰기 — 메모리는 문서 풀에서 할당
		// ------------------------------------------------------------------------------
		/** @brief 새 자식 노드를 추가합니다. */
		XmlNode appendChild( const utf8* pName ) const;
		/** @brief 새 속성을 추가합니다. */
		void appendAttr( const utf8* pName, const utf8* pValue ) const;
		/** @brief 기존 속성 값을 바꾸거나, 없으면 추가합니다. */
		void setAttr( const utf8* pName, const utf8* pValue ) const;
		/** @brief 노드 이름을 설정합니다. */
		void setName( const utf8* pName ) const;
		/** @brief 노드 값을 설정합니다. */
		void setValue( const utf8* pValue ) const;

		/** @brief 이 서브트리를 XML 문자열로 직렬화합니다 (Prefab/임베드용). */
		string toString() const;

	private:
		friend class XmlDocument;
		/** @brief RapidXML 노드 포인터로 핸들을 만듭니다. */
		explicit XmlNode( void* pNode )
			: _pNode{ pNode } {}

		void* _pNode{ nullptr };
	};

	/**
	 * @class XmlDocument
	 * @brief 파스 버퍼 + RapidXML 트리. TypeInfo 없는 수동 로드용
	 */
	class SW_API XmlDocument
	{
	public:
		// ------------------------------------------------------------------------------
		// 3) 수명 — 복사 금지, 이동 가능
		// ------------------------------------------------------------------------------
		/** @brief 빈 문서를 만듭니다. */
		XmlDocument();
		/** @brief 파스 버퍼를 해제합니다. */
		~XmlDocument();

		/** @brief 복사를 금지합니다. */
		XmlDocument( const XmlDocument& ) = delete;
		/** @brief 대입을 금지합니다. */
		XmlDocument& operator=( const XmlDocument& ) = delete;
		/** @brief 문서를 이동합니다. */
		XmlDocument( XmlDocument&& ) noexcept;
		/** @brief 문서를 이동 대입합니다. */
		XmlDocument& operator=( XmlDocument&& ) noexcept;

		/** @brief 문서를 비우고 파싱된 데이터를 해제합니다. */
		void clear();

		// ------------------------------------------------------------------------------
		// 4) 파싱 · 로드
		// ------------------------------------------------------------------------------
		/** @brief XML 전체 문서를 파싱합니다 (내부 버퍼에 복사). */
		bool parse( string_view xmlText );

		/** @brief 절대 경로를 읽고 파싱합니다. */
		bool loadFile( string_view absPath );

		/** @brief 리소스 상대 경로를 해석한 뒤 읽고 파싱합니다. */
		bool loadResource( string_view relativePath, string* pOutAbsPath = nullptr );

		/** @brief 첫 엘리먼트. pName이 있으면 이름으로 매칭 (기본 대소문자 무시). */
		XmlNode root( const utf8* pName = nullptr, bool bIgnoreCaseKeys = true ) const;

		// ------------------------------------------------------------------------------
		// 5) 쓰기
		// ------------------------------------------------------------------------------
		/** @brief 루트 노드를 만들고 반환합니다. */
		XmlNode appendRoot( const utf8* pName );
		/** @brief 현재 문서를 XML 문자열로 직렬화합니다. */
		string saveToString() const;

	private:
		struct Impl;
		unique_ptr<Impl> _impl;
	};
} // namespace sw
