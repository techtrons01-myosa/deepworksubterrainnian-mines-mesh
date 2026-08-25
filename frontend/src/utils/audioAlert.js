/**
 * audioAlert.js — Web Audio API acoustic buzzer and alarm synthesizer
 */

let audioCtx = null;

function getAudioContext() {
  if (!audioCtx) {
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (AudioContextClass) {
      audioCtx = new AudioContextClass();
    }
  }
  if (audioCtx && audioCtx.state === 'suspended') {
    audioCtx.resume().catch(() => {});
  }
  return audioCtx;
}

/**
 * Play a single acoustic buzzer beep
 */
export function playBuzzerBeep({ frequency = 950, duration = 0.2, volume = 0.4, type = 'square' } = {}) {
  try {
    const ctx = getAudioContext();
    if (!ctx) return;

    const osc = ctx.createOscillator();
    const gain = ctx.createGain();

    osc.type = type;
    osc.frequency.setValueAtTime(frequency, ctx.currentTime);

    // Envelope
    gain.gain.setValueAtTime(0, ctx.currentTime);
    gain.gain.linearRampToValueAtTime(volume, ctx.currentTime + 0.02);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);

    osc.connect(gain);
    gain.connect(ctx.destination);

    osc.start(ctx.currentTime);
    osc.stop(ctx.currentTime + duration);
  } catch (err) {
    console.warn('[AudioAlert] Web Audio error:', err);
  }
}

/**
 * Play an alarm burst (e.g. 3 high-pitch warning pulses for perimeter breach)
 */
export function playAlarmBurst({ pulses = 3, frequency = 1100, volume = 0.5 } = {}) {
  for (let i = 0; i < pulses; i++) {
    setTimeout(() => {
      playBuzzerBeep({ frequency: frequency + (i % 2 === 0 ? 0 : 250), duration: 0.15, volume, type: 'sawtooth' });
    }, i * 180);
  }
}

/**
 * Test buzzer sound
 */
export function testBuzzerSound(volume = 0.5) {
  playAlarmBurst({ pulses: 2, frequency: 950, volume });
}
