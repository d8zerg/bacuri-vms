class SpeechCapture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.samples = new Int16Array(1600);
    this.used = 0;
    this.weight = 0;
    this.sum = 0;
  }

  process(inputs) {
    // Average the native microphone samples covered by each 8 kHz output sample.
    for (const sample of inputs[0]?.[0] ?? []) {
      let ticks = 8000;
      while (ticks > 0) {
        const portion = Math.min(ticks, sampleRate - this.weight);
        this.sum += sample * portion;
        this.weight += portion;
        ticks -= portion;
        if (this.weight === sampleRate) {
          const value = Math.max(-1, Math.min(1, this.sum / sampleRate));
          this.samples[this.used++] = Math.round(value * 32767);
          this.weight = this.sum = 0;
          if (this.used === this.samples.length) {
            this.port.postMessage(this.samples.buffer, [this.samples.buffer]);
            this.samples = new Int16Array(1600);
            this.used = 0;
          }
        }
      }
    }
    return true;
  }
}
registerProcessor("speech-capture", SpeechCapture);
