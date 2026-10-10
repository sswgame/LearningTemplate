/**
 * @file AnimGraphPlayer.h
 * @brief 애니메이션 그래프 에셋(`AnimGraphAsset`)을 상태 기계로 돌립니다 — 노드 = 상태, 링크 = 전이(조건 또는 "끝나면 다음"). 2D · 3D 가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimPlayer.h"

namespace sw
{
    struct AnimGraphNode;

    class AnimGraphAsset;

    /**
     * @class IAnimPlayableSource
     * @brief 상태(노드) 이름을 재생할 것으로 풉니다. 스프라이트 애니메이터는 클립의 이름 붙은 구간을, 스켈레탈 애니메이터는 클립 집합을 줍니다.
     */
    class SW_API IAnimPlayableSource
    {
    public:
        IAnimPlayableSource()                                        = default;
        virtual ~IAnimPlayableSource()                               = default;
        IAnimPlayableSource( const IAnimPlayableSource& )            = default;
        IAnimPlayableSource& operator=( const IAnimPlayableSource& ) = default;

        /** @brief 이름의 재생할 것입니다. 없으면 nullptr 입니다. 돌려준 것은 다음 `findPlayable` 까지 살아 있어야 합니다. */
        virtual const IAnimPlayable* findPlayable( const hashed_string& name ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimGraphPlayer
     * @brief 지금 상태 하나와 그 재생(`AnimPlayer`)을 듭니다. 그래프가 없으면 이름으로 바로 재생하는 단순 플레이어입니다.
     * @details 매 갱신의 순서: (1) 지금 상태에서 나가는 조건 링크 중 참인 첫 것으로 넘어갑니다(트리거는 끕니다). (2) 시간을 흘립니다.
     *          (3) 지금 상태가 반복 없이 끝났으면 조건 없는 첫 링크("끝나면 다음")로 넘어갑니다. 넘어갈 때는 링크의 블렌드(없으면 기본값)로
     *          크로스페이드하고, 반복은 노드의 "loop" 입니다. 없으면 조건 전이는 재생할 것의 기본값, "끝나면 다음" 으로 들어온 상태는
     *          반복하지 않음, 직접 재생은 부른 쪽의 값입니다.
     *          재생할 것이 없는 상태로 넘어가면 재생을 비웁니다 — 이름은 새 상태를 말하는데 포즈는 옛 상태인 일이 없게 합니다.
     */
    class SW_API AnimGraphPlayer
    {
    public:
        AnimGraphPlayer();

        /** @brief 그래프를 겁니다(빌립니다 — 부르는 쪽이 수명을 쥡니다). nullptr 이면 이름으로 바로 재생합니다. 재생을 멈춥니다. */
        void setGraph( const AnimGraphAsset* pGraph );
        /** @brief 상태 이름 → 재생할 것 풀이를 겁니다(빌립니다). */
        void setPlayableSource( const IAnimPlayableSource* pSource ) { _pSource = pSource; }
        /** @brief 넘어갈 때 쓰는 기본 크로스페이드 길이(초)입니다. */
        void setDefaultBlendSeconds( float32 seconds ) { _defaultBlendSeconds = seconds > 0.0f ? seconds : 0.0f; }

        /**
         * @brief 상태로 넘어가 재생합니다. 이름이 비었으면 진입 노드, 그래프에 없는(또는 그래프가 없는) 이름이면 그 이름을 전이 없이 바로 재생합니다.
         * @param blendSeconds 0 이면 즉시, 아니면 그만큼 크로스페이드합니다.
         * @return 재생할 것을 찾았으면 true 입니다(못 찾아도 상태 이름은 옮깁니다).
         */
        bool play( const hashed_string& stateName, bool bLoop, float32 blendSeconds );
        /**
         * @brief 재생할 것을 직접 주어 상태로 들어갑니다(풀이를 거치지 않고 크로스페이드 없이). 이름이 그래프 노드면 그 노드의 전이를 따릅니다.
         * @details 이름 하나가 곧 클립 하나가 아닌 쪽(스프라이트의 "클립 전체" 구간 · 시퀀서)이 씁니다.
         */
        void playPlayable( const hashed_string& stateName, const IAnimPlayable* pPlayable, bool bLoop );
        /** @brief 멈추고 상태를 비웁니다. */
        void stop();
        /** @brief 조건 전이 → 시간 → "끝나면 다음" 순서로 한 걸음 갑니다(클래스 설명). */
        void update( float32 deltaSeconds, AnimParameterSet* pParameter, vector<AnimFiredNotify>* pOutListFired );
        /** @brief "끝나면 다음" 링크로 바로 넘어갑니다. 더 없으면 false 입니다. */
        bool advance();

        const hashed_string& getCurrentStateName() const { return _currentStateName; }
        /** @brief 지금 노드 id 입니다. 그래프 밖이면 0 입니다. */
        int32                 getCurrentNodeID() const { return _currentNodeID; }
        AnimPlayer&           getPlayer() { return _player; }
        const AnimPlayer&     getPlayer() const { return _player; }
        const AnimGraphAsset* getGraph() const { return _pGraph; }

    private:
        /** @brief 노드에 "loop" 가 없을 때의 반복 — 아니오 · 예 · 재생할 것의 기본값입니다. */
        static constexpr int8 kLoopNo           = 0;
        static constexpr int8 kLoopYes          = 1;
        static constexpr int8 kLoopFromPlayable = 2;
        /** @brief 노드로 넘어갑니다. @p blendSeconds 가 음수면 기본값입니다. */
        bool enterNode( const AnimGraphNode& node, int8 loopWhenUnspecified, float32 blendSeconds );
        /** @brief 이름으로 재생할 것을 찾아 재생(또는 크로스페이드)합니다. */
        bool startPlayable( const hashed_string& name, bool bLoop, float32 blendSeconds );

        AnimPlayer                 _player;
        hashed_string              _currentStateName;
        const AnimGraphAsset*      _pGraph;
        const IAnimPlayableSource* _pSource;
        int32                      _currentNodeID;
        float32                    _defaultBlendSeconds;
    };
} // namespace sw
