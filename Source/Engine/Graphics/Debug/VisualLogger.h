/**
 * @file VisualLogger.h
 * @brief 비주얼 로거입니다 — 게임 코드가 판단의 근거를 글과 도형(선, 구, 상자, 화살표)으로 프레임마다 적어 두고, 나중에 프레임을 골라 그 도형을 디버그 드로우로 다시 보는 기록입니다.
 * @details 언리얼의 Visual Logger(`UE_VLOG`)와 같은 자리다. `DebugDrawQueue` 는 지금 프레임의 도형을 그릴 뿐이라 AI 가 왜 그렇게 판단했는지 지난 프레임에서 볼 수 없다 — 여기는 기록이 남는다.
 *          - **끈 채 시작한다**(`setEnabled`). 꺼져 있으면 적는 호출은 첫 줄에서 돌아온다(문자열을 만들기 전에 `isEnabled` 로 먼저 물을 수 있다).
 *          - **고리 버퍼**: 용량(`setCapacity`, 기본 `kDefaultCapacity`)을 넘으면 가장 오래된 기록부터 버린다. 프레임 번호는 `endFrame` 이 올린다(엔진 루프가 프레임마다 한 번).
 *          - 적기는 어느 스레드에서 해도 된다(병렬 틱). 질의 · 다시 보기 · 저장은 게임 스레드에서 부른다.
 *          - **다시 보기**(`replayFrame`)는 그 프레임의 기록을 한 프레임짜리 디버그 드로우로 넣는다 — 에디터가 슬라이더로 프레임을 옮기며 부르면 도형이 따라 움직인다.
 *          - 파일(`*.vlog.json`)로 저장해 다른 사람에게 보낼 수 있다(버그 재현 첨부).
 *          개발 명령은 `visuallog.on` · `visuallog.off` · `visuallog.status` · `visuallog.clear` · `visuallog.save <파일>` · `visuallog.replay <프레임>` 이다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class DebugDrawQueue;

    /** @brief 기록 하나의 모양입니다. */
    enum class VisualLogShape : uint8
    {
        Text = 0, ///< `_a` 위치의 글(`_text`)
        Line,     ///< `_a` → `_b` 선
        Sphere,   ///< 중심 `_a`, 반지름 `_radius`
        Box,      ///< 중심 `_a`, 반 크기 `_b`
        Arrow,    ///< `_a` → `_b` 화살표
    };

    /** @brief 비주얼 로거의 기록 하나입니다. */
    struct VisualLogEntry
    {
        uint64         _frame{ 0 };    ///< 적은 프레임 번호(`VisualLogger::getFrame`)
        uint64         _objectID{ 0 }; ///< 적은 오브젝트 id. 0 이면 오브젝트와 무관한 기록
        hashed_string  _category{};    ///< 분류 이름(AI, 이동, 감지 …). 비면 `Default`
        string         _text{};        ///< 글(`Text`)이거나 도형에 붙은 설명. 비어도 된다
        float3         _a{};
        float3         _b{};
        float4         _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32        _radius{ 0.0f };
        VisualLogShape _shape{ VisualLogShape::Text };
    };
} // namespace sw

namespace sw
{
    /**
     * @class VisualLogger
     * @brief 엔진 서브시스템 하나가 프레임별 기록을 쥡니다. 파일 머리말 참고.
     */
    class SW_API VisualLogger : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "VisualLogger";
        /** @brief 기본 용량(기록 수)입니다. 프레임마다 수십 개를 적는 AI 몇 마리를 몇 분 동안 담는 크기다. */
        static constexpr uint32 kDefaultCapacity = 20000;
        /** @brief 파일 형식 버전입니다. */
        static constexpr uint32 kFileVersion = 1;
        /** @brief 분류를 주지 않은 기록의 분류 이름입니다. */
        static constexpr const utf8* kDefaultCategoryName = "Default";

        VisualLogger();

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 기록을 켜고 끕니다. 끄면 이미 쌓인 기록은 그대로 둔다. */
        void setEnabled( bool bEnabled );
        bool isEnabled() const { return _bEnabled != SW_FALSE; }
        /** @brief 용량(기록 수, 1 이상)을 바꿉니다. 줄이면 오래된 것부터 버립니다. 0 은 오류를 남기고 false 입니다. */
        [[nodiscard]] bool setCapacity( uint32 capacity );
        /** @brief 프레임 번호를 하나 올립니다. 엔진 루프가 프레임 끝에 한 번 부릅니다. */
        void endFrame();
        /** @brief 지금 프레임 번호입니다(적는 기록이 이 번호를 받는다). */
        uint64 getFrame() const;
        /** @brief 쌓인 기록을 모두 비웁니다. 프레임 번호는 그대로입니다. */
        void   clear();
        size_t getEntryCount() const;

        // --- 적기 (꺼져 있으면 아무것도 하지 않는다) ---------------------------------------
        void logText( uint64 objectID, const hashed_string& category, const float3& position, string_view text, const float4& color );
        void logLine( uint64 objectID, const hashed_string& category, const float3& from, const float3& to, const float4& color, string_view note = {} );
        void logSphere( uint64 objectID, const hashed_string& category, const float3& center, float32 radius, const float4& color, string_view note = {} );
        void logBox( uint64 objectID, const hashed_string& category, const float3& center, const float3& halfExtent, const float4& color, string_view note = {} );
        void logArrow( uint64 objectID, const hashed_string& category, const float3& from, const float3& to, const float4& color, string_view note = {} );

        // --- 질의 ---------------------------------------------------------------------------
        /**
         * @brief 프레임 [@p firstFrame, @p lastFrame] 의 기록을 @p outListEntry 에 채웁니다(먼저 비운다).
         * @param objectID 0 이 아니면 그 오브젝트의 기록만
         * @param category 비어 있지 않으면 그 분류만
         */
        void collect( uint64 firstFrame, uint64 lastFrame, uint64 objectID, const hashed_string& category, vector<VisualLogEntry>& outListEntry ) const;
        /** @brief 기록이 든 가장 이른 · 늦은 프레임입니다. 기록이 없으면 false 입니다. */
        [[nodiscard]] bool findFrameRange( uint64& outFirstFrame, uint64& outLastFrame ) const;

        /**
         * @brief 프레임 @p frame 의 기록(필터 같음)을 한 프레임짜리 디버그 드로우로 넣습니다. 넣은 수를 반환합니다.
         * @details 도형의 분류는 기록의 분류를 따라 에디터의 디버그 드로우 분류 토글이 그대로 먹는다. 글(`Text`)과 도형에 붙은 설명은 글로 그린다.
         */
        uint32 replayFrame( DebugDrawQueue& queue, uint64 frame, uint64 objectID, const hashed_string& category ) const;

        // --- 파일 ---------------------------------------------------------------------------
        /** @brief 쌓인 기록을 `*.vlog.json` 으로 디스크 경로에 씁니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 파일의 기록으로 바꿉니다(프레임 번호도 파일의 마지막 프레임 + 1 로 맞춘다). 읽지 못하면 오류를 남기고 false 이며 지금 기록은 그대로입니다. */
        [[nodiscard]] bool loadFromFile( string_view diskPath );

    private:
        /** @brief 기록 하나를 지금 프레임 번호로 넣습니다(켜져 있을 때만). */
        void append( VisualLogEntry entry );
        /** @brief 용량을 넘은 오래된 기록을 버립니다. 잠근 채 부릅니다. */
        void trimLocked() SW_REQUIRES_LOCK( _mutex );

        mutable mutex                    _mutex;
        deque<VisualLogEntry> _listEntry SW_GUARDED_BY( _mutex );
        uint64 _frame                    SW_GUARDED_BY( _mutex );
        uint32 _capacity                 SW_GUARDED_BY( _mutex );
        uint8                            _bEnabled;
    };
} // namespace sw
