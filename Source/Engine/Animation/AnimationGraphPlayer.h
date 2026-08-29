/**
 * @file AnimationGraphPlayer.h
 * @brief AnimationGraphAsset 노드를 AnimClip에 묶어 AnimPlayer로 재생합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Animation/AnimationGraphAsset.h"

namespace sw
{
	/**
	 * @class AnimationGraphPlayer
	 * @brief 그래프 노드 이름 → AnimClip 매핑. 클립이 끝나면 첫 나가는 링크로 크로스페이드합니다.
	 */
	class SW_API AnimationGraphPlayer
	{
	public:
		/** @brief 빈 플레이어입니다. */
		AnimationGraphPlayer();

		/** @brief JSON 그래프를 로드합니다. */
		bool loadGraph( string_view path );
		/** @brief 이미 파싱된 그래프를 설정합니다. */
		void setGraph( const AnimationGraphAsset& graph );
		/** @brief 노드 이름에 클립을 연결합니다. 같은 이름은 덮어씁니다. */
		void registerClip( string_view nodeName, const AnimClip* pClip );
		/** @brief 등록된 클립을 모두 지웁니다. */
		void clearClips();

		/** @brief 노드 이름(없으면 진입 노드)부터 재생합니다. */
		bool play( string_view nodeName = {}, bool bLoopClip = false );
		/** @brief 재생을 멈춥니다. */
		void stop();
		/** @brief 클립 시각을 갱신하고, 끝나면 그래프를 따라갑니다. */
		void update( float32 deltaSeconds );
		/** @brief 현재(또는 페이드 중 혼합) 샘플입니다. */
		AnimSample evaluate() const;

		/** @brief 크로스페이드 길이(초)를 설정합니다. */
		void setCrossfadeSeconds( float32 seconds );
		/** @brief 현재 노드 이름을 반환합니다. */
		const string& getCurrentNodeName() const { return _currentNodeName; }
		/** @brief 현재 노드 id입니다. 재생 중이 아니면 0입니다. */
		int32 getCurrentNodeId() const { return _currentNodeId; }
		/** @brief 나가는 첫 링크로 넘어갑니다. 더 없으면 false입니다. */
		bool advance();
		/** @brief 내부 AnimPlayer입니다. */
		AnimPlayer&		  getAnimPlayer() { return _player; }
		const AnimPlayer& getAnimPlayer() const { return _player; }
		/** @brief 로드된 그래프입니다. */
		const AnimationGraphAsset& getGraph() const { return _graph; }

	private:
		struct ClipBinding
		{
			string			_nodeName;
			const AnimClip* _pClip;
		};

		const AnimClip* findClip( string_view nodeName ) const;
		bool			playNode( int32 nodeId, bool bLoopClip, bool bCrossfade );

		AnimationGraphAsset _graph;
		AnimPlayer			_player;
		vector<ClipBinding> _listClip;
		string				_currentNodeName;
		int32				_currentNodeId;
		float32				_crossfadeSeconds;
	};
} // namespace sw
