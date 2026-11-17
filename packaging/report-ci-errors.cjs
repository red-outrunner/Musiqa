// Summarize failures as check annotations as well as downloadable test logs.
const fs = require("fs");
const messages = new Set();
for (const path of process.argv.slice(2)) {
    if (!fs.existsSync(path)) continue;
    const lines = fs.readFileSync(path, "utf8").split(/\r?\n/);
    for (let i = 0; i < lines.length; ++i) {
        if (/FAIL!|QFATAL|CMake Error|fatal error:|error C\d+|error:|ERROR:|timed out|No audio output|Failed to connect|failed to load|not installed/i.test(lines[i]))
            messages.add(lines.slice(Math.max(0, i - 1), i + 4).join(" | "));
    }
    if (!messages.size) messages.add(lines.slice(-20).join(" | "));
}
for (const text of [...messages].slice(0, 40)) {
    const escaped = text.replaceAll("%", "%25").replaceAll("\r", "%0D").replaceAll("\n", "%0A");
    console.log("::error::" + escaped);
}
