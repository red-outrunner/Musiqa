import QtQuick

Rectangle {
    id: root
    property string artwork: ""
    property int artStyle: 0
    radius: 8
    color: "#243a36"
    clip: true
    Image {
        anchors.fill: parent
        sourceSize.width: Math.max(64, width * 2)
        sourceSize.height: Math.max(64, height * 2)
        source: {
            let art = '';
            switch (root.artStyle % 4) {
            case 0:
                art = '<rect width="240" height="240" fill="#24473f"/><circle cx="155" cy="85" r="44" fill="#d9e8ba"/><path d="M-20 170Q70 40 160 165T300 170V260H-20Z" fill="#507f68"/><path d="M-20 205Q90 95 190 200T300 180V260H-20Z" fill="#172f2b"/>';
                break;
            case 1:
                art = '<rect width="240" height="240" fill="#d5a18f"/><circle cx="120" cy="120" r="84" fill="#673f51"/><circle cx="120" cy="120" r="66" fill="none" stroke="#c28b8a"/><circle cx="120" cy="120" r="52" fill="none" stroke="#c28b8a"/><circle cx="120" cy="120" r="36" fill="#e6c2a9"/><circle cx="120" cy="120" r="6" fill="#673f51"/>';
                break;
            case 2:
                art = '<rect width="240" height="240" fill="#242d50"/><circle cx="120" cy="104" r="66" fill="#df977d"/><path d="M0 152h240v88H0Z" fill="#1d2543"/><path d="M0 164h240m-230 14h220M24 193h192M44 209h152M64 225h112" stroke="#b8878b" stroke-width="2"/>';
                break;
            default:
                art = '<rect width="240" height="240" fill="#a8bbc3"/><path d="M20 240 120 0h55L76 240Z" fill="#44567b"/><path d="m100 240 100-240h55L155 240Z" fill="#e0dfc9"/><circle cx="64" cy="62" r="28" fill="#e9b487"/>';
            }
            return "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="240" height="240" viewBox="0 0 240 240">' + art + '</svg>');
        }
    }
    Image {
        anchors.fill: parent
        source: root.artwork
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.max(64, Math.ceil(root.width * 1.5))
        sourceSize.height: Math.max(64, Math.ceil(root.height * 1.5))
        visible: status === Image.Ready
    }
}
