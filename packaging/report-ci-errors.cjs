// Summarize each result file in one annotation (GitHub limits errors per step).
const fs = require("fs");
const pathTools = require("path");
const summaries = [];
const paths = process.argv.slice(2).flatMap(path => fs.existsSync(path) && fs.statSync(path).isDirectory()
    ? fs.readdirSync(path).filter(name => name.endsWith(".txt")).map(name => pathTools.join(path, name)) : [path]);
for (const path of paths) {
    if (!fs.existsSync(path)) continue;
    const bytes = fs.readFileSync(path);
    const text = bytes[0] === 0xff && bytes[1] === 0xfe ? bytes.toString("utf16le") : bytes.toString("utf8");
    const lines = text.split(/\r?\n/);
    const messages = new Set();
    for (let i = 0; i < lines.length; ++i) {
        if (/FAIL!|QFATAL|CMake Error|fatal error:|error C\d+|\berror\b|timed out|No audio output|Failed to connect|failed to load|not installed|not found|cannot|could not/i.test(lines[i])
            && !/QWARN|WARNING:/.test(lines[i]))
            messages.add(lines.slice(Math.max(0, i - 1), i + 4).join(" | "));
    }
    if (path.endsWith("package.log")) {
        const tail = lines.slice(-30).map(line => line.slice(0, 220)).join("\n");
        summaries.push((path + "\nLast output:\n" + tail).slice(-7000));
    } else {
        if (!messages.size && path.endsWith("ctest.log"))
            messages.add("Last output: " + lines.slice(-20).join(" | "));
        if (messages.size) summaries.push((path + "\n" + [...messages].slice(0, 14).join("\n")).slice(0, 7000));
    }
}
for (const text of summaries.slice(0, 10)) {
    const escaped = text.replaceAll("%", "%25").replaceAll("\r", "%0D").replaceAll("\n", "%0A");
    console.log("::error::" + escaped);
}
