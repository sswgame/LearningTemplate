/**
 * @file SequenceAsset.h
 * @brief 에디터/런타임이 공유하는 시퀀서 타임라인 JSON 애셋
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
	/** @brief 시퀀서 트랙 항목 (클립 또는 이벤트) */
	struct SequenceTrackItem
	{
		string _name;
		string _targetObject;
		float3 _translation{};
		float3 _rotation{};
		float3 _scale{ 1.0f, 1.0f, 1.0f };
		int32  _start{ 0 };
		int32  _end{ 10 };
		int32  _type{ 0 };
		uint32 _color{ 0xFFAA8080 };
	};

	/**
	 * @class SequenceAsset
	 * @brief 카메라/오브젝트 트랙을 담는 타임라인 애셋
	 */
	class SW_API SequenceAsset
	{
	public:
		/** @brief 빈 시퀀스를 만듭니다. */
		SequenceAsset() = default;

		/** @brief JSON 파일을 읽습니다. */
		bool loadFromFile( string_view path );
		/** @brief JSON 파일을 씁니다. */
		bool saveToFile( string_view path ) const;
		/** @brief JSON 본문을 파싱합니다. */
		bool parseJson( string_view json );
		/** @brief JSON 본문을 만듭니다. */
		string toJson() const;
		/** @brief 해당 프레임에 걸쳐 있는 트랙을 채웁니다. */
		void collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outItemList ) const;

		int32					  _frameMin{ 0 };
		int32					  _frameMax{ 100 };
		string					  _note;
		vector<SequenceTrackItem> _listItem;
	};
} // namespace sw
