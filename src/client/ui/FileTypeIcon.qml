/**
 * @file    FileTypeIcon.qml
 * @version 4.13.1
 * @date    2026-06-15
 * @author  GridYard Team
 * @brief   根据文件类型显示项目内置图标
 *
 * 根据文件名选择项目内置图标，避免依赖系统图标主题。
 *
 * Change Log:
 * [v4.13.1] GY   2026-06-15
 * * 初始版本，支持常见文件类型和文件夹图标
 */

import QtQuick

Image {
    id: fileTypeIcon

    property string fileName: ""
    property bool isDirectory: false

    readonly property string extension: {
        const leafName = fileName.split("/").pop().toLowerCase()
        const dotIndex = leafName.lastIndexOf(".")
        return dotIndex > 0 ? leafName.substring(dotIndex + 1) : ""
    }

    readonly property url iconSource: {
        if (isDirectory || fileName.endsWith("/")) return "../icons/folder.svg"
        if (["png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico", "heic"].includes(extension)) return "../icons/file-image.svg"
        if (["mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "mpeg", "mpg"].includes(extension)) return "../icons/file-video.svg"
        if (["mp3", "wav", "flac", "aac", "ogg", "m4a", "wma"].includes(extension)) return "../icons/file-audio.svg"
        if (["zip", "7z", "rar", "tar", "gz", "bz2", "xz", "zst"].includes(extension)) return "../icons/file-archive.svg"
        if (["cpp", "c", "h", "hpp", "qml", "js", "py", "java", "rs", "go", "html", "css", "json", "xml", "yaml", "yml", "cmake", "sh"].includes(extension)) return "../icons/file-code.svg"
        if (["pdf", "doc", "docx", "odt", "xls", "xlsx", "ods", "ppt", "pptx", "odp", "txt", "md", "rtf"].includes(extension)) return "../icons/file-document.svg"
        if (["exe", "msi", "appimage", "apk", "deb", "rpm", "dmg", "pkg", "bin", "run"].includes(extension)) return "../icons/file-executable.svg"
        return "../icons/file-generic.svg"
    }

    source: iconSource
    sourceSize.width: width
    sourceSize.height: height
    fillMode: Image.PreserveAspectFit
    smooth: true
}
