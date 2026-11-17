.pragma library

function duration(ms) {
    const seconds = Number.isFinite(ms) ? Math.max(0, Math.floor(ms / 1000)) : 0;
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor(seconds / 60) % 60;
    const tail = String(seconds % 60).padStart(2, "0");
    return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + tail
                     : minutes + ":" + tail;
}
