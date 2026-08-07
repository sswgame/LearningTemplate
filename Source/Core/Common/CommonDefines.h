#pragma once

/**
 * @file CommonDefines.h
 * @brief 엔진 구동 전반에 걸쳐 사용되는 전역 상수 및 인덱스 매직넘버 정의 헤더입니다.
 */
#include "Types.h"

namespace sw
{
	/**
	 * @brief 전역 상수를 모아두는 네임스페이스
	 */
	namespace constant
	{
		/** @brief CPU-GPU 동기화를 위한 렌더링 프레임 최대 큐 크기 (이중 버퍼링 기준) */
		inline static constexpr uint32 kMaxFrameCountInFlight = 2;

		/** @brief 파일 시스템 최대 경로 길이 (Windows MAX_PATH 호환) */
		inline static constexpr uint32 kMaxPathSize			  = 260;

		/** @brief 문자열 포맷팅 등에 사용되는 고정 크기 스택 버퍼들 */
		inline static constexpr uint32 kMaxBuffer128		  = 128;
		inline static constexpr uint32 kMaxBuffer1024		  = 1024;
		inline static constexpr uint32 kMaxBuffer2048		  = 2048;
		inline static constexpr uint32 kMaxBuffer4096		  = 4096;
		inline static constexpr uint32 kMaxBuffer8192		  = 8192;
	}

	/**
	 * @brief 파일 시스템 내 리소스 폴더 기본 상대 경로 정의 네임스페이스
	 */
	namespace path
	{
		inline static constexpr const utf8* kShaderFolder  = "Shader";	/**< 셰이더 소스 폴더명 */
		inline static constexpr const utf8* kTextureFolder = "Texture"; /**< 텍스처 폴더명 */
		inline static constexpr const utf8* kEditorFolder  = "Editor";	/**< 에디터 전용 리소스 폴더명 */
	}

	/**
	 * @brief 배열 순회 및 인덱스 실패 시 반환되는 무효 인덱스(-1) 정의 모음
	 */
	namespace invalid_index
	{
		inline static constexpr uint8  kUint8  = static_cast<uint8>( -1 );
		inline static constexpr uint16 kUint16 = static_cast<uint16>( -1 );
		inline static constexpr uint32 kUint32 = static_cast<uint32>( -1 );
		inline static constexpr uint64 kUint64 = static_cast<uint64>( -1 );

		inline static constexpr int8  kInt8	 = -1;
		inline static constexpr int16 kInt16 = -1;
		inline static constexpr int32 kInt32 = -1;
		inline static constexpr int64 kInt64 = -1;
	}

}
