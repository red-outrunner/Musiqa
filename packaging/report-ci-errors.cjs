// Summarize failures as check annotations as well as downloadable test logs.
const fs = require("fs");
const pathTools = require("path");
const messages = new Set();
const paths = process.argv.slice(2).flatMap(path => fs.existsSync(path) && fs.statSync(path).isDirectory()
    ? fs.readdirSync(path).filter(name => name.endsWith(".txt")).map(name => pathTools.join(path, name)) : [path]);
for (const path of paths) {
    if (!fs.existsSync(path)) continue;
    const lines = fs.readFileSync(path, "utf8").split(/\r?\n/);
    const before = messages.size;
    for (let i = 0; i < lines.length; ++i) {
        if (/FAIL!|QFATAL|CMake Error|fatal error:|error C\d+|\berror\b|timed out|No audio output|Failed to connect|failed to load|not installed|not found|cannot|could not/i.test(lines[i]))
            messages.add(lines.slice(Math.max(0, i - 1), i + 4).join(" | "));
    }
    if (messages.size === before && /(?:ctest|package)\.log$/.test(path))
        messages.add(path + ": " + lines.slice(-30).join(" | "));
}
for (const text of [...messages].slice(0, 40)) {
    const escaped = text.replaceAll("%", "%25").replaceAll("\r", "%0D").replaceAll("\n", "%0A");
    console.log("::error::" + escaped);
}
