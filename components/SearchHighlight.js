.pragma library

function escaped(value) {
    return String(value || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function marked(value, query, color) {
    const text = String(value || ""), needle = String(query || "").trim().toLowerCase();
    if (!needle) return escaped(text);
    const lower = text.toLowerCase();
    let result = "", offset = 0, count = 0, found;
    while ((found = lower.indexOf(needle, offset)) >= 0 && count++ < 32) {
        result += escaped(text.slice(offset, found)) + '<b><font color="' + color + '">' + escaped(text.slice(found, found + needle.length)) + '</font></b>';
        offset = found + needle.length;
    }
    return result + escaped(text.slice(offset));
}
