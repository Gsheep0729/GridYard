/**
 * @file    FileTypeIcon.qml
 * @version 7.15.18
 * @date    2026-10-04
 * @author  GridYard Team
 * @brief   根据文件类型显示项目内置图标
 *
 * 根据文件名选择项目内置图标，避免依赖系统图标主题。
 *
 * Change Log:
 * [v7.15.18] GY   2026-10-04
 * * 版本头对齐到 v7.15.18
 * [v7.15.17] GY   2026-10-04
 * * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
 * [v7.15.13] GY   2026-10-04
 * * 版本头对齐到 v7.15.13
 * [v7.15.12] GY   2026-10-03
 * 版本头对齐到 v7.15.12
 * [v7.15.11] GY   2026-10-03
 * * 版本头对齐到 v7.15.11
 * [v7.15.10] GY   2026-10-03
 * * 版本头对齐到 v7.15.10
 * [v7.15.6] GY   2026-10-03
 * * 版本头对齐到 v7.15.6
 * [v6.6.2] GY   2026-06-25
 * * 同步文件头版本与当前主版本
 * [v4.13.1] GY   2026-06-15
 * * 初始版本，支持常见文件类型和文件夹图标
 */

import QtQuick

Image {
    id: fileTypeIcon

    property string fileName: ""
    property bool isDirectory: false

    // 提取文件扩展名：取路径最后一段，转小写，截取最后一个点号之后的部分
    readonly property string extension: {
        const leafName = fileName.split("/").pop().toLowerCase()
        const dotIndex = leafName.lastIndexOf(".")
        return dotIndex > 0 ? leafName.substring(dotIndex + 1) : ""
    }

    // 根据扩展名选择项目内置 SVG 图标，避免依赖系统图标主题导致跨平台不一致
    readonly property url iconSource: {
        if (isDirectory || fileName.endsWith("/")) return "../icons/folder.svg"
        if (["png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico", "heic"].includes(extension)) return "../icons/file-image.svg"
        if (["mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "mpeg", "mpg"].includes(extension)) return "../icons/file-video.svg"
        if (["mp3", "wav", "flac", "aac", "ogg", "m4a", "wma"].includes(extension)) return "../icons/file-audio.svg"
        if (["zip", "7z", "rar", "tar", "gz", "bz2", "xz", "zst"].includes(extension)) return "../icons/file-archive.svg"
        if (["cpp", "c", "h", "hpp", "qml", "js", "py", "java", "rs", "go", "html", "css", "json", "xml", "yaml", "yml", "cmake", "sh"].includes(extension)) return "../icons/file-code.svg"
        if (["pdf", "doc", "docx", "odt", "xls", "xlsx", "ods", "ppt", "pptx", "odp", "txt", "md", "rtf"].includes(extension)) return "../icons/file-document.svg"
        if (["exe", "msi", "appimage", "apk", "deb", "rpm", "dmg", "pkg", "bin", "run"].includes(extension)) return "../icons/file-executable.svg"
        return "../icons/file-generic.svg"  // 未知类型使用通用文件图标
    }

    source: iconSource
    sourceSize.width: width
    sourceSize.height: height
    fillMode: Image.PreserveAspectFit
    smooth: true
}
