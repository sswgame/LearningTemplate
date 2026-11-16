/**
 * @file WebAudioBackend.js
 * @brief 웹 오디오 백엔드의 브라우저 쪽(Emscripten JS 라이브러리) — C++ 쪽은 Engine/Audio/Web/WebAudioBackend.cpp(방식 설명도 거기).
 * @details 워클릿 처리기는 Engine/Audio/CircularAudioBuffer 의 소비자다 — 읽기 · 쓰기 위치는 늘기만 하는 uint32, 칸은 `위치 & (용량 - 1)`.
 *          C++ 쪽 위치 규칙을 바꾸면 아래 `process` 도 함께 바꾼다. 모든 함수는 메인(브라우저) 스레드에서 돈다(`__proxy: 'sync'`) — AudioContext 는 창에만 있다.
 */
addToLibrary({
    $swWebAudio: {
        nextID: 1,
        mapStream: {},
        processorURL: null,
        // AudioWorkletGlobalScope 에서 도는 처리기. 링이 모자라면 남은 칸은 0(브라우저가 0 으로 채워 준다 — 언더런은 무음)이다.
        processorSource: `
class SwRingOutputProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const o = options.processorOptions;
        this.words = new Int32Array(o.memory);
        this.samples = new Float32Array(o.memory);
        this.readSlot = o.readIndexAddress >> 2;
        this.writeSlot = o.writeIndexAddress >> 2;
        this.sampleSlot = o.sampleAddress >> 2;
        this.mask = o.capacity - 1;
        this.channelCount = o.channelCount;
        this.bStarted = false;
    }
    process(inputs, outputs) {
        const output = outputs[0];
        const frameCount = output[0].length;
        const channelCount = this.channelCount;
        const writtenChannelCount = Math.min(output.length, channelCount);
        const read = Atomics.load(this.words, this.readSlot) >>> 0;
        const write = Atomics.load(this.words, this.writeSlot) >>> 0;
        const availableFrames = Math.floor(((write - read) >>> 0) / channelCount);
        const frames = Math.min(availableFrames, frameCount);
        for (let frame = 0; frame < frames; ++frame) {
            const base = read + frame * channelCount;
            for (let channel = 0; channel < writtenChannelCount; ++channel) {
                output[channel][frame] = this.samples[this.sampleSlot + ((base + channel) & this.mask)];
            }
        }
        if (frames > 0) {
            Atomics.store(this.words, this.readSlot, (read + frames * channelCount) | 0);
            Atomics.notify(this.words, this.readSlot); // 자리를 기다리는 믹서 스레드를 깨운다
            if (!this.bStarted) {
                this.bStarted = true;
                this.port.postMessage('started');
            }
        }
        return true;
    }
}
registerProcessor('sw-ring-output', SwRingOutputProcessor);
`,
    },

    swWebAudioIsAvailable__proxy: 'sync',
    swWebAudioIsAvailable: () =>
        typeof AudioContext !== 'undefined' && typeof AudioWorkletNode !== 'undefined' && typeof SharedArrayBuffer !== 'undefined' && globalThis.crossOriginIsolated === true ? 1 : 0,

    swWebAudioStart__proxy: 'sync',
    swWebAudioStart__deps: ['$swWebAudio', '$wasmMemory'],
    swWebAudioStart: (pReadIndex, pWriteIndex, pSample, capacity, channelCount, sampleRate, pStatus, pState) => {
        let context;
        try {
            context = new AudioContext({ sampleRate: sampleRate, latencyHint: 'interactive' });
        } catch (error) {
            err('[WebAudio] AudioContext could not be created at ' + sampleRate + ' Hz: ' + error);
            return 0;
        }
        if (context.sampleRate !== sampleRate) {
            err('[WebAudio] AudioContext runs at ' + context.sampleRate + ' Hz instead of ' + sampleRate + ' Hz');
            context.close().catch(() => {});
            return 0;
        }
        // 메모리 버퍼는 이 순간의 것을 넘긴다 — 공유 메모리는 늘어나도 옛 버퍼가 그 길이만큼 유효하고, 링은 이미 그 안에 있다.
        const memory = wasmMemory.buffer;
        const words = new Int32Array(memory);
        const id = swWebAudio.nextID++;
        const stream = { context: context, node: null, pState: pState, bStopped: false, resume: null };
        const markLost = (why) => {
            if (stream.bStopped) return;
            err('[WebAudio] output lost: ' + why);
            Atomics.store(words, pStatus >> 2, 1);
            Atomics.notify(words, pReadIndex >> 2); // 기다리는 믹서가 유실을 보게 깨운다
        };
        // 자동 재생 정책: 사용자 몸짓 전에는 AudioContext 가 멈춰 있다. 첫 몸짓에서 다시 튼다.
        stream.resume = () => {
            if (context.state === 'suspended') context.resume().catch((error) => console.warn('[WebAudio] resume refused: ' + error));
        };
        for (const type of ['pointerdown', 'keydown', 'touchend']) window.addEventListener(type, stream.resume, true);
        if (swWebAudio.processorURL === null) swWebAudio.processorURL = URL.createObjectURL(new Blob([swWebAudio.processorSource], { type: 'text/javascript' }));
        context.audioWorklet
            .addModule(swWebAudio.processorURL)
            .then(() => {
                if (stream.bStopped) return;
                const node = new AudioWorkletNode(context, 'sw-ring-output', {
                    numberOfInputs: 0,
                    numberOfOutputs: 1,
                    outputChannelCount: [channelCount],
                    processorOptions: {
                        memory: memory,
                        readIndexAddress: pReadIndex,
                        writeIndexAddress: pWriteIndex,
                        sampleAddress: pSample,
                        capacity: capacity,
                        channelCount: channelCount,
                    },
                });
                node.onprocessorerror = () => markLost('the worklet processor threw');
                node.port.onmessage = () => out('[WebAudio] output started (' + context.sampleRate + ' Hz, ' + channelCount + ' channels)');
                node.connect(context.destination);
                stream.node = node;
            })
            .catch((error) => markLost('the worklet module failed: ' + error));
        swWebAudio.mapStream[id] = stream;
        return id;
    },

    swWebAudioStop__proxy: 'sync',
    swWebAudioStop__deps: ['$swWebAudio', 'swWebAudioReleaseStream'],
    swWebAudioStop: (id) => {
        const stream = swWebAudio.mapStream[id];
        if (!stream) return;
        delete swWebAudio.mapStream[id];
        stream.bStopped = true;
        for (const type of ['pointerdown', 'keydown', 'touchend']) window.removeEventListener(type, stream.resume, true);
        if (stream.node) stream.node.disconnect();
        // 닫기가 끝나야 워클릿이 링을 더 만지지 않는다 — 그 뒤에 C++ 공유 상태를 놓는다(닫기가 져도 놓는다: 컨텍스트가 이미 닫혔다).
        const release = () => _swWebAudioReleaseStream(stream.pState);
        stream.context.close().then(release, release);
    },
});
