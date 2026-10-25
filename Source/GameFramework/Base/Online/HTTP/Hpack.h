/**
 * @file Hpack.h
 * @brief HTTP/2 헤더 압축(HPACK, RFC 7541)의 부호기와 복호기입니다. HTTP/2 클라이언트(APNs 제공자)의 둘째 조각이다(온라인 키트 G4).
 * @details 부호기는 정적 표의 완전 일치를 색인 하나로 쓰고, 이름만 일치하면 이름을 색인으로, 나머지는 문자열 그대로(허프만 없이) "색인에 더하지 않는 글자 그대로" 로 쓴다.
 *          동적 표에 더하지 않으므로 상대의 표 상태에 기대지 않고, 같은 입력은 같은 바이트다.
 *          복호기는 정적 표 · 동적 표 · 표 크기 갱신 · 정수 접두사 · 글자 그대로의 표현을 모두 읽는다.
 *          허프만 문자열은 RFC 7541 부록 B 표(`HpackHuffmanTable.h`, RFC 본문에서 생성)로 푼다. 패딩이 틀리거나 EOS 가 든 문자열은 오류로 멈춘다.
 *          부호기는 허프만을 쓰지 않는다. 이 파일은 바이트만 다루고 네트워크를 모른다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 헤더 하나입니다. 이름은 소문자여야 한다(HTTP/2). */
    struct HpackHeader
    {
        string _name{};
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @class HpackEncoder @brief 헤더 목록을 헤더 블록 바이트로 씁니다. 상태가 없습니다. */
    class SW_GF_API HpackEncoder
    {
    public:
        /** @brief @p listHeader 를 @p outListBlock 뒤에 붙입니다. 이름이 비었거나 대문자가 든 헤더가 있으면 아무것도 쓰지 않고 false 입니다(HTTP/2 는 소문자 이름만 허용한다). */
        [[nodiscard]] static bool encode( const vector<HpackHeader>& listHeader, vector<uint8>& outListBlock );
        /** @brief 접두사 @p prefixBits 비트 정수(RFC 7541 5.1)를 쓰고, 첫 바이트의 윗 비트는 @p firstByteFlags 로 채웁니다. */
        static void writeInteger( uint32 value, int32 prefixBits, uint8 firstByteFlags, vector<uint8>& outListBlock );
    };
} // namespace sw

namespace sw
{
    /** @class HpackDecoder @brief 헤더 블록 바이트를 읽습니다. 동적 표를 연결 하나 동안 들고 있으므로 연결마다 하나를 둡니다. */
    class SW_GF_API HpackDecoder
    {
    public:
        /** @brief 동적 표 크기의 상한입니다(우리가 SETTINGS_HEADER_TABLE_SIZE 로 알린 값, 기본 4096). 상대가 표 크기 갱신으로 이보다 크게 부르면 오류다. */
        explicit HpackDecoder( uint32 maxDynamicTableSize = 4096 );

        /**
         * @brief 헤더 블록 하나를 읽어 @p outListHeader 를 채웁니다. 깨졌거나 허프만 문자열이 틀렸거나 색인이 표 밖이면 false 이고 @p outError 에 이유가 있습니다(목록은 비운다).
         * @details 동적 표는 읽는 도중 바뀌므로 실패한 블록 뒤의 연결은 쓸 수 없다(RFC 7541 은 압축 상태 오류를 연결 오류로 본다) — 부르는 쪽이 연결을 닫는다.
         */
        [[nodiscard]] bool decode( const uint8* pBlock, size_t size, vector<HpackHeader>& outListHeader, string& outError );
        size_t             getDynamicTableCount() const { return _listDynamic.size(); }
        uint32             getDynamicTableSize() const { return _dynamicSize; }

    private:
        [[nodiscard]] bool readInteger( const uint8*& pCursor, const uint8* pEnd, int32 prefixBits, uint32& outValue, string& outError ) const;
        [[nodiscard]] bool readString( const uint8*& pCursor, const uint8* pEnd, string& outText, string& outError ) const;
        /** @brief 허프만 부호화된 문자열(RFC 7541 5.2)을 풉니다. 패딩이 7 비트를 넘거나 1 이 아니면, EOS 가 들었거나 부호표에 없는 부호면 false 입니다. */
        [[nodiscard]] bool decodeHuffman( const uint8* pData, size_t size, string& outText, string& outError ) const;
        [[nodiscard]] bool findEntry( uint32 index, HpackHeader& outHeader, string& outError ) const;
        void               insert( const HpackHeader& header );
        void               shrinkTo( uint32 size );

        deque<HpackHeader> _listDynamic; ///< 앞이 가장 새 항목(색인 62)
        uint32             _dynamicSize;
        uint32             _currentLimit; ///< 상대가 표 크기 갱신으로 정한 지금 상한
        uint32             _maxLimit;
    };
} // namespace sw
