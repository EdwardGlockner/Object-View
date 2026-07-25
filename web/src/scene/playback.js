export function createPlayback(duration, onUpdate) {
  let time = 0;
  let playing = false;

  function emit() {
    onUpdate(time, duration, playing);
  }

  function seek(newTime) {
    time = Math.min(Math.max(newTime, 0), duration);
    emit();
  }

  function play() {
    if (duration <= 0) return;
    playing = true;
    emit();
  }

  function pause() {
    playing = false;
    emit();
  }

  function toggle() {
    playing ? pause() : play();
  }

  function tick(deltaSeconds) {
    if (!playing) return;
    let next = time + deltaSeconds;
    if (next >= duration) next = 0;
    time = next;
    emit();
  }

  emit();

  return {
    play,
    pause,
    toggle,
    seek,
    tick,
    get time() { return time; },
    get duration() { return duration; },
    get playing() { return playing; },
  };
}
